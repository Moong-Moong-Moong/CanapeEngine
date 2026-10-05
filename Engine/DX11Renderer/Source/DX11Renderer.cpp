#include "DX11Renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <wrl/client.h>

#include <iterator>
#include <sstream>
#include <unordered_map>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace Canape
{
    namespace
    {
        constexpr char kVertexShader[] = R"(
            cbuffer ObjectBuffer : register(b0)
            {
                float4x4 WorldViewProjection;
                float4x4 World;
            };

            struct VertexInput
            {
                float3 Position : POSITION;
            };

            struct VertexOutput
            {
                float4 Position : SV_POSITION;
                float3 WorldPosition : WORLDPOS;
            };

            VertexOutput main(VertexInput input)
            {
                VertexOutput output;
                output.Position = mul(float4(input.Position, 1.0f), WorldViewProjection);
                output.WorldPosition = mul(float4(input.Position, 1.0f), World).xyz;
                return output;
            }
        )";

        constexpr char kPixelShader[] = R"(
            struct PixelInput
            {
                float4 Position : SV_POSITION;
                float3 WorldPosition : WORLDPOS;
            };

            float4 main(PixelInput input) : SV_TARGET
            {
                float3 normal = normalize(cross(ddy(input.WorldPosition), ddx(input.WorldPosition)));
                float3 lightDirection = normalize(float3(0.4f, 1.0f, -0.6f));
                float diffuse = abs(dot(normal, lightDirection)) * 0.75f + 0.25f;
                return float4(diffuse.xxx * float3(0.85f, 0.85f, 0.9f), 1.0f);
            }
        )";

        std::string HResultMessage(const char* operation, HRESULT result)
        {
            std::ostringstream stream;
            stream << operation << " failed (HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(result) << ')';
            return stream.str();
        }
    }

    struct DX11Renderer::Impl
    {
        struct ObjectConstants
        {
            DirectX::XMFLOAT4X4 WorldViewProjection;
            DirectX::XMFLOAT4X4 World;
        };

        struct GpuMesh
        {
            ComPtr<ID3D11Buffer> VertexBuffer;
            ComPtr<ID3D11Buffer> IndexBuffer;
            uint32_t IndexCount = 0;
        };

        HWND Window = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool FrameActive = false;
        std::string LastError;

        DirectX::XMFLOAT4X4 View{};
        DirectX::XMFLOAT4X4 Projection{};

        std::unordered_map<MeshHandle, GpuMesh> Meshes;
        MeshHandle NextMeshHandle = 1;

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> RenderTargetView;
        ComPtr<ID3D11Texture2D> DepthTexture;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;
        ComPtr<ID3D11VertexShader> VertexShader;
        ComPtr<ID3D11PixelShader> PixelShader;
        ComPtr<ID3D11InputLayout> InputLayout;
        ComPtr<ID3D11Buffer> ObjectBuffer;
        ComPtr<ID3D11RasterizerState> RasterizerState;

        Impl()
        {
            DirectX::XMStoreFloat4x4(&View, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&Projection, DirectX::XMMatrixIdentity());
        }

        bool Fail(const char* operation, HRESULT result)
        {
            LastError = HResultMessage(operation, result);
            return false;
        }

        bool CreateTargets(uint32_t width, uint32_t height)
        {
            ComPtr<ID3D11Texture2D> backBuffer;
            HRESULT result = SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
            if (FAILED(result))
            {
                return Fail("IDXGISwapChain::GetBuffer", result);
            }

            result = Device->CreateRenderTargetView(backBuffer.Get(), nullptr, &RenderTargetView);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateRenderTargetView", result);
            }

            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
            depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            result = Device->CreateTexture2D(&depthDesc, nullptr, &DepthTexture);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateTexture2D(depth)", result);
            }

            result = Device->CreateDepthStencilView(DepthTexture.Get(), nullptr, &DepthStencilView);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateDepthStencilView", result);
            }

            Width = width;
            Height = height;
            return true;
        }

        bool ResizeIfNeeded(uint32_t width, uint32_t height)
        {
            if (width == Width && height == Height)
            {
                return true;
            }

            Context->OMSetRenderTargets(0, nullptr, nullptr);
            RenderTargetView.Reset();
            DepthStencilView.Reset();
            DepthTexture.Reset();

            const HRESULT result = SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(result))
            {
                return Fail("IDXGISwapChain::ResizeBuffers", result);
            }

            return CreateTargets(width, height);
        }

        bool CreatePipeline()
        {
            ComPtr<ID3DBlob> vertexBytecode;
            ComPtr<ID3DBlob> pixelBytecode;
            ComPtr<ID3DBlob> errors;

            HRESULT result = D3DCompile(
                kVertexShader, sizeof(kVertexShader) - 1, nullptr, nullptr, nullptr,
                "main", "vs_5_0", 0, 0, &vertexBytecode, &errors);
            if (FAILED(result))
            {
                LastError = errors
                    ? static_cast<const char*>(errors->GetBufferPointer())
                    : HResultMessage("D3DCompile(vertex)", result);
                return false;
            }

            errors.Reset();
            result = D3DCompile(
                kPixelShader, sizeof(kPixelShader) - 1, nullptr, nullptr, nullptr,
                "main", "ps_5_0", 0, 0, &pixelBytecode, &errors);
            if (FAILED(result))
            {
                LastError = errors
                    ? static_cast<const char*>(errors->GetBufferPointer())
                    : HResultMessage("D3DCompile(pixel)", result);
                return false;
            }

            result = Device->CreateVertexShader(
                vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
                nullptr, &VertexShader);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateVertexShader", result);
            }

            result = Device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
                nullptr, &PixelShader);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreatePixelShader", result);
            }

            constexpr D3D11_INPUT_ELEMENT_DESC inputElements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                  D3D11_INPUT_PER_VERTEX_DATA, 0 },
            };

            result = Device->CreateInputLayout(
                inputElements, static_cast<UINT>(std::size(inputElements)),
                vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
                &InputLayout);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateInputLayout", result);
            }

            D3D11_BUFFER_DESC constantBufferDesc{};
            constantBufferDesc.ByteWidth = sizeof(ObjectConstants);
            constantBufferDesc.Usage = D3D11_USAGE_DEFAULT;
            constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &ObjectBuffer);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateBuffer(constants)", result);
            }

            D3D11_RASTERIZER_DESC rasterizerDesc{};
            rasterizerDesc.FillMode = D3D11_FILL_SOLID;
            rasterizerDesc.CullMode = D3D11_CULL_NONE;
            rasterizerDesc.DepthClipEnable = TRUE;
            result = Device->CreateRasterizerState(&rasterizerDesc, &RasterizerState);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateRasterizerState", result);
            }

            return true;
        }
    };

    DX11Renderer::DX11Renderer()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    DX11Renderer::~DX11Renderer() = default;

    bool DX11Renderer::Initialize(void* nativeWindow, uint32_t width, uint32_t height)
    {
        Shutdown();
        m_Impl = std::make_unique<Impl>();
        m_Impl->Window = static_cast<HWND>(nativeWindow);

        if (!m_Impl->Window || !IsWindow(m_Impl->Window))
        {
            m_Impl->LastError = "Initialize received an invalid native window";
            return false;
        }

        DXGI_SWAP_CHAIN_DESC swapChainDesc{};
        swapChainDesc.BufferDesc.Width = width;
        swapChainDesc.BufferDesc.Height = height;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.OutputWindow = m_Impl->Window;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL selectedFeatureLevel{};

        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
            &swapChainDesc, &m_Impl->SwapChain, &m_Impl->Device,
            &selectedFeatureLevel, &m_Impl->Context);

        if (FAILED(result))
        {
            result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                &swapChainDesc, &m_Impl->SwapChain, &m_Impl->Device,
                &selectedFeatureLevel, &m_Impl->Context);
        }

        if (FAILED(result))
        {
            return m_Impl->Fail("D3D11CreateDeviceAndSwapChain", result);
        }

        return m_Impl->CreateTargets(width, height) && m_Impl->CreatePipeline();
    }

    void DX11Renderer::Shutdown()
    {
        m_Impl.reset();
    }

    DX11Renderer::MeshHandle DX11Renderer::CreateMesh(
        std::span<const Vertex> vertices,
        std::span<const uint32_t> indices)
    {
        if (!m_Impl || !m_Impl->Device)
        {
            if (m_Impl)
            {
                m_Impl->LastError = "Initialize must be called before CreateMesh";
            }
            return InvalidMesh;
        }
        if (vertices.empty() || indices.empty())
        {
            m_Impl->LastError = "CreateMesh requires non-empty vertex and index data";
            return InvalidMesh;
        }

        D3D11_BUFFER_DESC vertexBufferDesc{};
        vertexBufferDesc.ByteWidth = static_cast<UINT>(vertices.size_bytes());
        vertexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA vertexData{ vertices.data() };

        Impl::GpuMesh mesh;
        HRESULT result = m_Impl->Device->CreateBuffer(&vertexBufferDesc, &vertexData, &mesh.VertexBuffer);
        if (FAILED(result))
        {
            m_Impl->Fail("ID3D11Device::CreateBuffer(vertices)", result);
            return InvalidMesh;
        }

        D3D11_BUFFER_DESC indexBufferDesc{};
        indexBufferDesc.ByteWidth = static_cast<UINT>(indices.size_bytes());
        indexBufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
        indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        D3D11_SUBRESOURCE_DATA indexData{ indices.data() };

        result = m_Impl->Device->CreateBuffer(&indexBufferDesc, &indexData, &mesh.IndexBuffer);
        if (FAILED(result))
        {
            m_Impl->Fail("ID3D11Device::CreateBuffer(indices)", result);
            return InvalidMesh;
        }

        mesh.IndexCount = static_cast<uint32_t>(indices.size());

        const MeshHandle handle = m_Impl->NextMeshHandle++;
        m_Impl->Meshes.emplace(handle, std::move(mesh));
        m_Impl->LastError.clear();
        return handle;
    }

    void DX11Renderer::DestroyMesh(MeshHandle mesh)
    {
        if (m_Impl)
        {
            m_Impl->Meshes.erase(mesh);
        }
    }

    void DX11Renderer::SetCamera(const float* view, const float* projection)
    {
        if (!m_Impl)
        {
            return;
        }

        DirectX::XMStoreFloat4x4(&m_Impl->View, DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(view)));
        DirectX::XMStoreFloat4x4(&m_Impl->Projection, DirectX::XMLoadFloat4x4(reinterpret_cast<const DirectX::XMFLOAT4X4*>(projection)));
    }

    bool DX11Renderer::BeginFrame()
    {
        if (!m_Impl || !m_Impl->Context || !m_Impl->SwapChain)
        {
            return false;
        }

        RECT clientRect{};
        if (!GetClientRect(m_Impl->Window, &clientRect))
        {
            return false;
        }

        const uint32_t width = static_cast<uint32_t>(clientRect.right - clientRect.left);
        const uint32_t height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
        if (width == 0 || height == 0 || !m_Impl->ResizeIfNeeded(width, height))
        {
            return false;
        }

        ID3D11DeviceContext* context = m_Impl->Context.Get();

        const float clearColor[] = { 0.025f, 0.035f, 0.055f, 1.0f };
        context->ClearRenderTargetView(m_Impl->RenderTargetView.Get(), clearColor);
        context->ClearDepthStencilView(m_Impl->DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        ID3D11RenderTargetView* renderTarget = m_Impl->RenderTargetView.Get();
        context->OMSetRenderTargets(1, &renderTarget, m_Impl->DepthStencilView.Get());

        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(m_Impl->Width);
        viewport.Height = static_cast<float>(m_Impl->Height);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        context->RSSetState(m_Impl->RasterizerState.Get());

        context->IASetInputLayout(m_Impl->InputLayout.Get());
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11Buffer* objectBuffer = m_Impl->ObjectBuffer.Get();
        context->VSSetShader(m_Impl->VertexShader.Get(), nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &objectBuffer);
        context->PSSetShader(m_Impl->PixelShader.Get(), nullptr, 0);

        m_Impl->FrameActive = true;
        return true;
    }

    void DX11Renderer::DrawMesh(MeshHandle mesh, const float* world)
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

        const auto found = m_Impl->Meshes.find(mesh);
        if (found == m_Impl->Meshes.end())
        {
            return;
        }

        using namespace DirectX;

        const XMMATRIX worldMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(world));
        const XMMATRIX viewMatrix = XMLoadFloat4x4(&m_Impl->View);
        const XMMATRIX projectionMatrix = XMLoadFloat4x4(&m_Impl->Projection);

        Impl::ObjectConstants constants{};
        XMStoreFloat4x4(&constants.WorldViewProjection, XMMatrixTranspose(worldMatrix * viewMatrix * projectionMatrix));
        XMStoreFloat4x4(&constants.World, XMMatrixTranspose(worldMatrix));

        ID3D11DeviceContext* context = m_Impl->Context.Get();
        context->UpdateSubresource(m_Impl->ObjectBuffer.Get(), 0, nullptr, &constants, 0, 0);

        const Impl::GpuMesh& gpuMesh = found->second;
        constexpr UINT stride = sizeof(Vertex);
        constexpr UINT offset = 0;
        ID3D11Buffer* vertexBuffer = gpuMesh.VertexBuffer.Get();
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->IASetIndexBuffer(gpuMesh.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        context->DrawIndexed(gpuMesh.IndexCount, 0, 0);
    }

    void DX11Renderer::EndFrame()
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

        m_Impl->FrameActive = false;

        const HRESULT result = m_Impl->SwapChain->Present(1, 0);
        if (FAILED(result))
        {
            m_Impl->Fail("IDXGISwapChain::Present", result);
        }
    }

    uint32_t DX11Renderer::GetWidth() const
    {
        return m_Impl ? m_Impl->Width : 0;
    }

    uint32_t DX11Renderer::GetHeight() const
    {
        return m_Impl ? m_Impl->Height : 0;
    }

    const std::string& DX11Renderer::GetLastError() const
    {
        static const std::string emptyError;
        return m_Impl ? m_Impl->LastError : emptyError;
    }
}
