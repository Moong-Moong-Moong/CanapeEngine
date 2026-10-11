#include "DX11Renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d11.h>
#include "PipelineState.h"
#include "GPUResourceCache.h"
#include <DirectXMath.h>
#include <wrl/client.h>

#include <sstream>
#include <unordered_map>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace Canape
{
    namespace
    {
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
            DirectX::XMFLOAT4X4 WorldNormal;
        };

        struct MaterialConstants
        {
            Vector4 BaseColor;
            uint32_t Mode;
            float AlphaCutoff;
            float Padding[2]{};
        };
        static_assert(sizeof(MaterialConstants) == 32);
        static_assert(offsetof(MaterialConstants, Mode) == 16 && offsetof(MaterialConstants, AlphaCutoff) == 20);

        struct MaterialEntry
        {
            MaterialDesc Desc;
            TextureHandle BaseColorTexture;
        };

        HWND Window = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool FrameActive = false;
        std::string LastError;

        DirectX::XMFLOAT4X4 View{};
        DirectX::XMFLOAT4X4 Projection{};

        // Descriptor snapshots only. No per-material GPU resource is needed for BaseColor.
        std::unordered_map<uint64_t, MaterialEntry> Materials;
        std::unordered_map<uint64_t, MaterialHandle> RegisteredMaterials;

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        // Reverse member destruction releases cached resources before Context and Device.
        // The cache retains neither pointer; all creation calls borrow this renderer's pair.
        GPUResourceCache Resources;
        PipelineState Pipelines;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> RenderTargetView;
        ComPtr<ID3D11Texture2D> DepthTexture;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;
        ComPtr<ID3D11Buffer> ObjectBuffer;
        ComPtr<ID3D11Buffer> MaterialBuffer;

        Impl()
        {
            DirectX::XMStoreFloat4x4(&View, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&Projection, DirectX::XMMatrixIdentity());
        }

        ~Impl()
        {
            // Drop context-held buffer/SRV references before releasing the owned GPU objects.
            if (Context) Context->ClearState();
        }

        bool Fail(const char* operation, HRESULT result)
        {
            LastError = HResultMessage(operation, result);
            return false;
        }

        void BindPipeline(const PipelineSelection& pipeline)
        {
            Context->IASetInputLayout(pipeline.InputLayout);
            Context->VSSetShader(pipeline.VS, nullptr, 0);
            Context->PSSetShader(pipeline.PS, nullptr, 0);
            Context->RSSetState(pipeline.Rasterizer);
            Context->OMSetBlendState(pipeline.Blend, nullptr, UINT32_MAX);
            Context->OMSetDepthStencilState(pipeline.Depth, 0);
            Context->PSSetSamplers(0, 1, &pipeline.Sampler);
        }

        bool CreateTargets(uint32_t width, uint32_t height)
        {
            ComPtr<ID3D11Texture2D> backBuffer;
            HRESULT result = SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
            if (FAILED(result))
            {
                return Fail("IDXGISwapChain::GetBuffer", result);
            }

            // Keep the UNORM swap-chain storage; encode linear RGB once through its sRGB view.
            // CreateTargets is also used after ResizeBuffers, so resize preserves this policy.
            D3D11_RENDER_TARGET_VIEW_DESC renderTargetDesc{};
            renderTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            renderTargetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            result = Device->CreateRenderTargetView(backBuffer.Get(), &renderTargetDesc, &RenderTargetView);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateRenderTargetView(back buffer, RGBA8_UNORM_SRGB)", result);
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
            if (!Pipelines.Initialize(Device.Get(), LastError)) return false;

            D3D11_BUFFER_DESC constantBufferDesc{};
            constantBufferDesc.ByteWidth = sizeof(ObjectConstants);
            constantBufferDesc.Usage = D3D11_USAGE_DEFAULT;
            constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            HRESULT result = Device->CreateBuffer(&constantBufferDesc, nullptr, &ObjectBuffer);
            if (FAILED(result))
            {
                return Fail("ID3D11Device::CreateBuffer(constants)", result);
            }

            static_assert(sizeof(Vector4) == 16);
            constantBufferDesc.ByteWidth = sizeof(MaterialConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &MaterialBuffer);
            if (FAILED(result)) return Fail("ID3D11Device::CreateBuffer(material constants)", result);

            return Resources.CreateWhiteTexture(Device.Get(), Context.Get(), LastError);
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
        return CreateMeshData(vertices.data(), vertices.size(), sizeof(Vertex), VertexFormat::Position, indices, {});
    }

    DX11Renderer::MeshHandle DX11Renderer::CreateMesh(std::span<const StaticVertex> vertices,
        std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        return CreateMeshData(vertices.data(), vertices.size(), sizeof(StaticVertex), VertexFormat::Static, indices, submeshes);
    }

    DX11Renderer::MeshHandle DX11Renderer::CreateMeshData(const void* vertices, size_t vertexCount,
        uint32_t stride, VertexFormat format, std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.CreateMesh(m_Impl->Device.Get(), vertices, vertexCount, stride, format,
            indices, submeshes, m_Impl->LastError);
    }

    DX11Renderer::MeshHandle DX11Renderer::RegisterMesh(
        MeshAssetId assetId, std::span<const Vertex> vertices, std::span<const uint32_t> indices)
    {
        return RegisterMeshData(assetId, vertices.data(), vertices.size(), sizeof(Vertex), VertexFormat::Position, indices, {});
    }

    DX11Renderer::MeshHandle DX11Renderer::RegisterMesh(MeshAssetId assetId, std::span<const StaticVertex> vertices,
        std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        return RegisterMeshData(assetId, vertices.data(), vertices.size(), sizeof(StaticVertex), VertexFormat::Static, indices, submeshes);
    }

    DX11Renderer::MeshHandle DX11Renderer::RegisterMeshData(MeshAssetId assetId, const void* vertices,
        size_t vertexCount, uint32_t stride, VertexFormat format, std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.RegisterMesh(m_Impl->Device.Get(), assetId, vertices, vertexCount,
            stride, format, indices, submeshes, m_Impl->LastError);
    }

    DX11Renderer::MeshHandle DX11Renderer::FindMesh(MeshAssetId assetId) const
    {
        return m_Impl ? m_Impl->Resources.FindMesh(assetId) : InvalidMesh;
    }

    void DX11Renderer::DestroyMesh(MeshHandle mesh)
    {
        if (m_Impl) m_Impl->Resources.DestroyMesh(mesh);
    }

    bool DX11Renderer::IsMeshValid(MeshHandle mesh) const
    {
        return m_Impl && m_Impl->Resources.IsMeshValid(mesh);
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

    MaterialHandle DX11Renderer::RegisterMaterial(MaterialAssetId assetId, const MaterialDesc& desc, TextureHandle baseColorTexture)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        if (!assetId.IsValid() || !m_Impl->Device)
        {
            m_Impl->LastError = "RegisterMaterial requires a valid asset ID and initialized renderer";
            return {};
        }
        if (const MaterialHandle existing = FindMaterial(assetId); existing.IsValid())
        {
            m_Impl->LastError.clear();
            return existing; // First registration wins until explicit removal/reset.
        }
        const Vector4& color = desc.baseColor;
        if (!std::isfinite(color.X) || !std::isfinite(color.Y) || !std::isfinite(color.Z) || !std::isfinite(color.W))
        {
            m_Impl->LastError = "RegisterMaterial requires a finite BaseColor";
            return {};
        }
        if ((desc.alphaMode != AlphaMode::Opaque && desc.alphaMode != AlphaMode::Masked && desc.alphaMode != AlphaMode::Transparent)
            || !std::isfinite(desc.alphaCutoff) || desc.alphaCutoff < 0.0f || desc.alphaCutoff > 1.0f)
        {
            m_Impl->LastError = "RegisterMaterial requires a supported AlphaMode and finite AlphaCutoff in [0, 1]";
            return {};
        }
        static uint64_t nextHandle = 1; // Never reuse a removed/pre-reset handle during this process.
        if (nextHandle == 0)
        {
            m_Impl->LastError = "Material handle space exhausted";
            return {};
        }
        const MaterialHandle handle{ nextHandle++ };
        m_Impl->RegisteredMaterials.erase(assetId.Value);
        m_Impl->Materials.emplace(handle.Value, Impl::MaterialEntry{ desc, baseColorTexture });
        m_Impl->RegisteredMaterials.emplace(assetId.Value, handle);
        m_Impl->LastError.clear();
        return handle;
    }

    MaterialHandle DX11Renderer::FindMaterial(MaterialAssetId assetId) const
    {
        if (!m_Impl || !assetId.IsValid()) return {};
        const auto found = m_Impl->RegisteredMaterials.find(assetId.Value);
        return found != m_Impl->RegisteredMaterials.end() && IsMaterialValid(found->second) ? found->second : MaterialHandle{};
    }

    MaterialDesc DX11Renderer::GetMaterial(MaterialHandle material) const
    {
        if (m_Impl && material.IsValid())
        {
            const auto found = m_Impl->Materials.find(material.Value);
            if (found != m_Impl->Materials.end()) return found->second.Desc;
        }
        return {}; // Default white material, including nonexistent or stale handles.
    }

    void DX11Renderer::DestroyMaterial(MaterialHandle material)
    {
        if (!m_Impl) return;
        std::erase_if(m_Impl->RegisteredMaterials, [material](const auto& entry) { return entry.second == material; });
        m_Impl->Materials.erase(material.Value);
    }

    bool DX11Renderer::IsMaterialValid(MaterialHandle material) const
    {
        return m_Impl && material.IsValid() && m_Impl->Materials.contains(material.Value);
    }

    TextureHandle DX11Renderer::RegisterTexture(TextureAssetId assetId, uint32_t width, uint32_t height,
        TextureFormat format, std::span<const uint8_t> pixels)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.RegisterTexture(m_Impl->Device.Get(), m_Impl->Context.Get(),
            assetId, width, height, format, pixels, m_Impl->LastError);
    }

    TextureHandle DX11Renderer::FindTexture(TextureAssetId assetId) const
    {
        return m_Impl ? m_Impl->Resources.FindTexture(assetId) : TextureHandle{};
    }

    bool DX11Renderer::IsTextureValid(TextureHandle texture) const
    {
        return m_Impl && m_Impl->Resources.IsTextureValid(texture);
    }

    void DX11Renderer::DestroyTexture(TextureHandle texture)
    {
        if (!IsTextureValid(texture)) return;
        // Explicit removal invalidates dependent material snapshots so Application can rebuild them.
        std::erase_if(m_Impl->RegisteredMaterials, [&](const auto& entry)
        {
            const auto material = m_Impl->Materials.find(entry.second.Value);
            return material != m_Impl->Materials.end() && material->second.BaseColorTexture == texture;
        });
        std::erase_if(m_Impl->Materials, [texture](const auto& entry) { return entry.second.BaseColorTexture == texture; });
        ID3D11ShaderResourceView* unbound = nullptr;
        m_Impl->Context->PSSetShaderResources(0, 1, &unbound);
        m_Impl->Resources.DestroyTexture(texture);
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

        // Linear RGB and linear alpha. The sRGB RTV encodes RGB, including clear values.
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

        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        ID3D11Buffer* objectBuffer = m_Impl->ObjectBuffer.Get();
        context->VSSetConstantBuffers(0, 1, &objectBuffer);
        // Reset the complete default pipeline, including frames ending in transparency.
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque));

        m_Impl->FrameActive = true;
        return true;
    }

    void DX11Renderer::DrawFrame(const RenderFrame& frame)
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

        SetCamera(frame.camera.view.Data(), frame.camera.projection.Data());
        std::vector<const RenderItem*> opaque, masked;
        struct TransparentDraw { const RenderItem* Item; float Depth; };
        std::vector<TransparentDraw> transparent;
        for (const RenderItem& item : frame.items)
        {
            if (!IsMeshValid(item.mesh)) continue;
            switch (GetMaterial(item.material).alphaMode)
            {
            case AlphaMode::Masked: masked.push_back(&item); break;
            case AlphaMode::Transparent:
            {
                // Approximate camera-space depth from the world origin; no bounds/triangle sorting.
                float depth = frame.camera.view.TransformPoint(item.world.GetTranslation()).Z;
                if (!std::isfinite(depth)) depth = 0.0f; // Keep the comparator ordered for malformed input.
                transparent.push_back({ &item, depth });
                break;
            }
            default: opaque.push_back(&item); break;
            }
        }
        std::stable_sort(transparent.begin(), transparent.end(), [](const auto& a, const auto& b) { return a.Depth > b.Depth; });
        auto draw = [&](const RenderItem* item) { DrawMesh(item->mesh, item->world.Data(), item->submeshIndex, item->material); };
        for (const auto* item : opaque) draw(item);
        for (const auto* item : masked) draw(item);
        for (const auto& entry : transparent) draw(entry.Item);
    }

    void DX11Renderer::DrawMesh(MeshHandle mesh, const float* world, uint32_t submeshIndex, MaterialHandle material)
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

        const auto* resource = m_Impl->Resources.GetMesh(mesh);
        if (!resource)
        {
            return;
        }
        const GPUResourceCache::GpuMesh& gpuMesh = *resource;
        if (submeshIndex >= gpuMesh.SubMeshes.size())
        {
            m_Impl->LastError = "DrawMesh received an invalid submeshIndex";
            return;
        }

        using namespace DirectX;

        const XMMATRIX worldMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(world));
        const XMMATRIX viewMatrix = XMLoadFloat4x4(&m_Impl->View);
        const XMMATRIX projectionMatrix = XMLoadFloat4x4(&m_Impl->Projection);

        Impl::ObjectConstants constants{};
        XMStoreFloat4x4(&constants.WorldViewProjection, worldMatrix * viewMatrix * projectionMatrix);
        XMStoreFloat4x4(&constants.World, worldMatrix);
        XMVECTOR determinant;
        const XMMATRIX inverseWorld = XMMatrixInverse(&determinant, worldMatrix);
        const float det = XMVectorGetX(determinant);
        // Mathematical inverse-transpose for normals, not an upload/packing transpose.
        // Singular transforms use zero normals and the geometric fallback in the pixel shader.
        if (std::isfinite(det) && det != 0.0f && !XMMatrixIsInfinite(inverseWorld) && !XMMatrixIsNaN(inverseWorld))
            XMStoreFloat4x4(&constants.WorldNormal, XMMatrixTranspose(inverseWorld));

        ID3D11DeviceContext* context = m_Impl->Context.Get();
        context->UpdateSubresource(m_Impl->ObjectBuffer.Get(), 0, nullptr, &constants, 0, 0);
        const MaterialDesc materialDesc = GetMaterial(material);
        const Impl::MaterialConstants materialConstants{ materialDesc.baseColor, static_cast<uint32_t>(materialDesc.alphaMode), materialDesc.alphaCutoff };
        context->UpdateSubresource(m_Impl->MaterialBuffer.Get(), 0, nullptr, &materialConstants, 0, 0);
        // Bind every draw, including direct DrawMesh calls, so prior pipeline state cannot leak.
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(gpuMesh.Format, materialDesc.alphaMode));
        ID3D11Buffer* materialBuffer = m_Impl->MaterialBuffer.Get();
        context->PSSetConstantBuffers(1, 1, &materialBuffer);
        ID3D11ShaderResourceView* textureView = m_Impl->Resources.GetWhiteTextureView();
        if (const auto entry = m_Impl->Materials.find(material.Value); entry != m_Impl->Materials.end())
        {
            if (const auto* texture = m_Impl->Resources.GetTexture(entry->second.BaseColorTexture))
                textureView = texture->ShaderResourceView.Get();
        }
        // Always bind, including white fallback, so previous draws cannot leak a texture.
        context->PSSetShaderResources(0, 1, &textureView);
        const UINT stride = gpuMesh.Stride;
        constexpr UINT offset = 0;
        ID3D11Buffer* vertexBuffer = gpuMesh.VertexBuffer.Get();
        context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context->IASetIndexBuffer(gpuMesh.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
        const SubMesh& part = gpuMesh.SubMeshes[submeshIndex];
        context->DrawIndexed(part.indexCount, part.startIndex, part.baseVertex);
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
