#include "DX11Renderer/DX11Renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d11.h>
#include "DX11Renderer/Pipeline/DX11PipelineState.h"
#include "DX11Renderer/Resource/DX11ResourceCache.h"
#include <DirectXMath.h>
#include <wrl/client.h>

#include <sstream>
#include <cmath>
#include <algorithm>

#include "CNDebug/DebugLog.h"

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
        struct alignas(16) ObjectConstants
        {
            DirectX::XMFLOAT4X4 World;
            DirectX::XMFLOAT4X4 WorldInverseTranspose;
        };
        static_assert(sizeof(ObjectConstants) == 128);
        static_assert(offsetof(ObjectConstants, WorldInverseTranspose) == 64);

        struct alignas(16) ViewConstants
        {
            DirectX::XMFLOAT4X4 View{};
            DirectX::XMFLOAT4X4 Projection{};
            DirectX::XMFLOAT4X4 ViewProjection{};
            Vector3 CameraPosition{};
            float Padding = 0.0f;
        };
        static_assert(sizeof(ViewConstants) == 208);
        static_assert(offsetof(ViewConstants, Projection) == 64);
        static_assert(offsetof(ViewConstants, ViewProjection) == 128);
        static_assert(offsetof(ViewConstants, CameraPosition) == 192);

        // Initial fixed capacities. Keep these in sync with Constants.hlsli.
        static constexpr uint32_t MaxDirectionalLights = 4;
        static constexpr uint32_t MaxLocalLights = 64;
        static constexpr uint32_t MaxShadowViews = 64;
        static constexpr uint32_t InvalidShadowView = UINT32_MAX;

        enum class LocalLightType : uint32_t { Point = 0, Spot = 1 };

        struct alignas(16) DirectionalLightGpu
        {
            Vector3 Direction{ 0.0f, -1.0f, 0.0f }; // Normalized world travel direction.
            uint32_t ShadowViewOffset = InvalidShadowView;
            Vector3 ColorIntensity{}; // Linear color multiplied by intensity.
            uint32_t ShadowViewCount = 0; // No shadow: 0. Directional shadow: 1.
        };
        static_assert(sizeof(DirectionalLightGpu) == 32);
        static_assert(offsetof(DirectionalLightGpu, ShadowViewOffset) == 12);
        static_assert(offsetof(DirectionalLightGpu, ColorIntensity) == 16);
        static_assert(offsetof(DirectionalLightGpu, ShadowViewCount) == 28);

        struct alignas(16) LocalLightGpu
        {
            Vector3 Position{};
            float Range = 10.0f;
            Vector3 Direction{ 0.0f, -1.0f, 0.0f }; // Spot only; normalized.
            float OuterAngleCos = 1.0f; // Spot only; cosine of the half-angle.
            Vector3 ColorIntensity{};
            float InnerAngleCos = 1.0f;
            uint32_t Type = static_cast<uint32_t>(LocalLightType::Point);
            uint32_t ShadowViewOffset = InvalidShadowView;
            uint32_t ShadowViewCount = 0; // No shadow: 0. Spot: 1. Point cubemap: 6.
            uint32_t Padding = 0;
        };
        static_assert(sizeof(LocalLightGpu) == 64);
        static_assert(offsetof(LocalLightGpu, Range) == 12);
        static_assert(offsetof(LocalLightGpu, Direction) == 16);
        static_assert(offsetof(LocalLightGpu, OuterAngleCos) == 28);
        static_assert(offsetof(LocalLightGpu, ColorIntensity) == 32);
        static_assert(offsetof(LocalLightGpu, InnerAngleCos) == 44);
        static_assert(offsetof(LocalLightGpu, Type) == 48);
        static_assert(offsetof(LocalLightGpu, ShadowViewOffset) == 52);
        static_assert(offsetof(LocalLightGpu, ShadowViewCount) == 56);

        struct alignas(16) LightConstants
        {
            uint32_t DirectionalCount = 0;
            uint32_t LocalCount = 0;
            uint32_t Padding[2]{};
            DirectionalLightGpu DirectionalLights[MaxDirectionalLights]{};
            LocalLightGpu LocalLights[MaxLocalLights]{};
        };
        static_assert(offsetof(LightConstants, DirectionalLights) == 16);
        static_assert(offsetof(LightConstants, LocalLights) == 16 + 32 * MaxDirectionalLights);
        static_assert(sizeof(LightConstants) == 16 + 32 * MaxDirectionalLights + 64 * MaxLocalLights);

        struct alignas(16) ShadowViewGpu
        {
            DirectX::XMFLOAT4X4 LightViewProjection{}; // LightView * LightProjection.
            float DepthBias = 0.0f;
            uint32_t MapIndex = UINT32_MAX; // 2D-array slice or cube index, according to light type.
            float NearPlane = 0.1f;
            float FarPlane = 10.0f;
        };
        static_assert(sizeof(ShadowViewGpu) == 80);
        static_assert(offsetof(ShadowViewGpu, DepthBias) == 64);
        static_assert(offsetof(ShadowViewGpu, MapIndex) == 68);
        static_assert(offsetof(ShadowViewGpu, NearPlane) == 72);
        static_assert(offsetof(ShadowViewGpu, FarPlane) == 76);

        struct alignas(16) ShadowConstants
        {
            uint32_t ViewCount = 0;
            uint32_t Padding[3]{};
            // Directional/Spot use one view. Point uses six in +X,-X,+Y,-Y,+Z,-Z order.
            ShadowViewGpu Views[MaxShadowViews]{};
        };
        static_assert(offsetof(ShadowConstants, Views) == 16);
        static_assert(sizeof(ShadowConstants) == 16 + 80 * MaxShadowViews);
        static_assert(sizeof(LightConstants) <= 65536 && sizeof(ShadowConstants) <= 65536);

        struct alignas(16) SceneConstants
        {
            Vector3 AmbientColor{ 1.0f, 1.0f, 1.0f };
            float AmbientIntensity = 0.1f;
            Vector3 FogColor{ 0.5f, 0.5f, 0.5f };
            float FogDensity = 0.01f;
            float FogStartDistance = 0.0f;
            float FogEndDistance = 100.0f;
            uint32_t FogMode = static_cast<uint32_t>(Canape::FogMode::Disabled);
            float Padding = 0.0f;
        };
        static_assert(sizeof(SceneConstants) == 48);
        static_assert(offsetof(SceneConstants, AmbientIntensity) == 12);
        static_assert(offsetof(SceneConstants, FogColor) == 16);
        static_assert(offsetof(SceneConstants, FogDensity) == 28);
        static_assert(offsetof(SceneConstants, FogStartDistance) == 32);
        static_assert(offsetof(SceneConstants, FogEndDistance) == 36);
        static_assert(offsetof(SceneConstants, FogMode) == 40);

        struct MaterialConstants
        {
            Vector4 BaseColor;
            uint32_t Mode;
            float AlphaCutoff;
            float Padding[2]{};
        };
        static_assert(sizeof(MaterialConstants) == 32);
        static_assert(offsetof(MaterialConstants, Mode) == 16 && offsetof(MaterialConstants, AlphaCutoff) == 20);

        HWND Window = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool FrameActive = false;
        std::string LastError;

        ViewConstants ViewData{};
        SceneConstants SceneData{};
        bool ViewDirty = true;
        bool SceneDirty = true;
        RenderScene QueuedScene;

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        // Reverse member destruction releases cached resources before Context and Device.
        // The cache retains neither pointer; all creation calls borrow this renderer's pair.
        DX11ResourceCache Resources;
        DX11PipelineState Pipelines;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> RenderTargetView;
        ComPtr<ID3D11Texture2D> DepthTexture;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;
        ComPtr<ID3D11Buffer> ObjectBuffer;
        ComPtr<ID3D11Buffer> MaterialBuffer;
        ComPtr<ID3D11Buffer> ViewBuffer;
        ComPtr<ID3D11Buffer> SceneBuffer;

        Impl()
        {
            DirectX::XMStoreFloat4x4(&ViewData.View, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&ViewData.Projection, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&ViewData.ViewProjection, DirectX::XMMatrixIdentity());
        }

        ~Impl()
        {
            // Drop context-held buffer/SRV references before releasing the owned GPU objects.
            if (Context) Context->ClearState();
        }

        void BindSharedConstants()
        {
            // Upload once per view/scene change, including the direct DrawMesh path.
            if (ViewDirty)
            {
                Context->UpdateSubresource(ViewBuffer.Get(), 0, nullptr, &ViewData, 0, 0);
                ViewDirty = false;
            }
            if (SceneDirty)
            {
                Context->UpdateSubresource(SceneBuffer.Get(), 0, nullptr, &SceneData, 0, 0);
                SceneDirty = false;
            }

            ID3D11Buffer* viewBuffer = ViewBuffer.Get();
            ID3D11Buffer* sceneBuffer = SceneBuffer.Get();
            Context->VSSetConstantBuffers(2, 1, &viewBuffer);
            Context->PSSetConstantBuffers(2, 1, &viewBuffer);
            Context->PSSetConstantBuffers(4, 1, &sceneBuffer);
        }

        void BindPipeline(const DX11PipelineSelection& pipeline)
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
                LastError = HResultMessage("IDXGISwapChain::GetBuffer", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            // Keep the UNORM swap-chain storage; encode linear RGB once through its sRGB view.
            // CreateTargets is also used after ResizeBuffers, so resize preserves this policy.
            D3D11_RENDER_TARGET_VIEW_DESC renderTargetDesc{};
            renderTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            renderTargetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            result = Device->CreateRenderTargetView(backBuffer.Get(), &renderTargetDesc, &RenderTargetView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateRenderTargetView(back buffer, RGBA8_UNORM_SRGB)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
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
                LastError = HResultMessage("ID3D11Device::CreateTexture2D(depth)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            result = Device->CreateDepthStencilView(DepthTexture.Get(), nullptr, &DepthStencilView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateDepthStencilView", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
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
                LastError = HResultMessage("IDXGISwapChain::ResizeBuffers", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
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
                LastError = HResultMessage("ID3D11Device::CreateBuffer(constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            static_assert(sizeof(Vector4) == 16);
            constantBufferDesc.ByteWidth = sizeof(MaterialConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &MaterialBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(material constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(ViewConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &ViewBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(view constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(SceneConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &SceneBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(scene constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

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
        CN_LOG_RENDERER("Init", "렌더러 초기화 완료");
        Shutdown();
        m_Impl = std::make_unique<Impl>();
        m_Impl->Window = static_cast<HWND>(nativeWindow);

        if (!m_Impl->Window || !IsWindow(m_Impl->Window))
        {
            m_Impl->LastError = "Initialize received an invalid native window";
            CN_LOG_RENDERER("Error", m_Impl->LastError);
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
            m_Impl->LastError = HResultMessage("D3D11CreateDeviceAndSwapChain", result);
            CN_LOG_RENDERER("Error", m_Impl->LastError);
            return false;
        }

        return m_Impl->CreateTargets(width, height) && m_Impl->CreatePipeline();
    }

    void DX11Renderer::Shutdown()
    {
        m_Impl.reset();
    }

    MeshResourceId DX11Renderer::CreateMesh(
        std::span<const Vertex> vertices,
        std::span<const uint32_t> indices)
    {
        return CreateMeshData(vertices.data(), vertices.size(), sizeof(Vertex), VertexFormat::Position, indices, {});
    }

    MeshResourceId DX11Renderer::CreateMesh(std::span<const StaticVertex> vertices,
        std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        return CreateMeshData(vertices.data(), vertices.size(), sizeof(StaticVertex), VertexFormat::Static, indices, submeshes);
    }

    MeshResourceId DX11Renderer::CreateMeshData(const void* vertices, size_t vertexCount,
        uint32_t stride, VertexFormat format, std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.CreateMesh(m_Impl->Device.Get(), vertices, vertexCount, stride, format,
            indices, submeshes, m_Impl->LastError);
    }

    void DX11Renderer::DestroyMesh(MeshResourceId mesh)
    {
        if (m_Impl) m_Impl->Resources.DestroyMesh(mesh);
    }

    bool DX11Renderer::IsMeshValid(MeshResourceId mesh) const
    {
        return m_Impl && m_Impl->Resources.IsMeshValid(mesh);
    }

    void DX11Renderer::SetCamera(const float* view, const float* projection)
    {
        if (!m_Impl)
        {
            return;
        }

        using namespace DirectX;

        const XMMATRIX viewMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(view));
        const XMMATRIX projectionMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(projection));
        XMStoreFloat4x4(&m_Impl->ViewData.View, viewMatrix);
        XMStoreFloat4x4(&m_Impl->ViewData.Projection, projectionMatrix);
        XMStoreFloat4x4(&m_Impl->ViewData.ViewProjection, viewMatrix * projectionMatrix);

        // Preserve the legacy SetCamera + DrawMesh path, which has no position argument.
        m_Impl->ViewData.CameraPosition = {};
        XMVECTOR determinant;
        const XMMATRIX inverseView = XMMatrixInverse(&determinant, viewMatrix);
        const float det = XMVectorGetX(determinant);
        if (std::isfinite(det) && det != 0.0f && !XMMatrixIsInfinite(inverseView) && !XMMatrixIsNaN(inverseView))
            m_Impl->ViewData.CameraPosition = Vector3::FromXM(inverseView.r[3]);
        m_Impl->ViewDirty = true;
    }

    void DX11Renderer::SetRenderView(const RenderView& view)
    {
        if (m_Impl) m_Impl->QueuedScene.camera = view;
    }

    void DX11Renderer::AddRenderItem(const RenderItem& item)
    {
        if (m_Impl) m_Impl->QueuedScene.items.push_back(item);
    }

    MaterialResourceId DX11Renderer::CreateMaterial(const MaterialDesc& desc, TextureResourceId baseColorTexture)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        if (!m_Impl->Device)
        {
            m_Impl->LastError = "CreateMaterial requires an initialized renderer";
            CN_LOG_RENDERER("Error", m_Impl->LastError);
            return {};
        }
        return m_Impl->Resources.CreateMaterial(desc, baseColorTexture, m_Impl->LastError);
    }

    MaterialDesc DX11Renderer::GetMaterial(MaterialResourceId material) const
    {
        if (m_Impl)
        {
            if (const auto* resource = m_Impl->Resources.GetMaterial(material)) return resource->Desc;
        }
        return {}; // Default white material, including nonexistent or stale IDs.
    }

    void DX11Renderer::DestroyMaterial(MaterialResourceId material)
    {
        if (m_Impl) m_Impl->Resources.DestroyMaterial(material);
    }

    bool DX11Renderer::IsMaterialValid(MaterialResourceId material) const
    {
        return m_Impl && m_Impl->Resources.IsMaterialValid(material);
    }

    TextureResourceId DX11Renderer::CreateTexture(uint32_t width, uint32_t height, TextureFormat format, std::span<const uint8_t> pixels)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.CreateTexture(m_Impl->Device.Get(), m_Impl->Context.Get(),
            width, height, format, pixels, m_Impl->LastError);
    }

    bool DX11Renderer::IsTextureValid(TextureResourceId texture) const
    {
        return m_Impl && m_Impl->Resources.IsTextureValid(texture);
    }

    void DX11Renderer::DestroyTexture(TextureResourceId texture)
    {
        if (!IsTextureValid(texture)) return;
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

        // Direct draws start with scene defaults instead of inheriting the previous frame.
        m_Impl->SceneData = {};
        m_Impl->SceneDirty = true;
        // Reset the complete default pipeline, including frames ending in transparency.
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque));

        m_Impl->FrameActive = true;
        return true;
    }

    void DX11Renderer::DrawScene()
    {
        if (!m_Impl)
        {
            return;
        }

        DrawScene(m_Impl->QueuedScene);
        m_Impl->QueuedScene = {};
    }

    void DX11Renderer::DrawScene(const RenderScene& scene)
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

        SetCamera(scene.camera.view.Data(), scene.camera.projection.Data());
        m_Impl->ViewData.CameraPosition = scene.camera.cameraPosition;
        m_Impl->SceneData.AmbientColor = scene.ambientColor;
        m_Impl->SceneData.AmbientIntensity = scene.ambientIntensity;
        m_Impl->SceneData.FogColor = scene.fog.color;
        m_Impl->SceneData.FogDensity = scene.fog.density;
        m_Impl->SceneData.FogStartDistance = scene.fog.startDistance;
        m_Impl->SceneData.FogEndDistance = scene.fog.endDistance;
        m_Impl->SceneData.FogMode = static_cast<uint32_t>(scene.fog.mode);
        m_Impl->SceneDirty = true;
        m_Impl->BindSharedConstants();
        std::vector<const RenderItem*> opaque, masked;
        struct TransparentDraw { const RenderItem* Item; float Depth; };
        std::vector<TransparentDraw> transparent;
        for (const RenderItem& item : scene.items)
        {
            if (!IsMeshValid(item.mesh)) continue;
            switch (GetMaterial(item.material).alphaMode)
            {
            case AlphaMode::Masked: masked.push_back(&item); break;
            case AlphaMode::Transparent:
            {
                // Approximate camera-space depth from the world origin; no bounds/triangle sorting.
                float depth = scene.camera.view.TransformPoint(item.world.GetTranslation()).Z;
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

    void DX11Renderer::DrawMesh(MeshResourceId mesh, const float* world, uint32_t submeshIndex, MaterialResourceId material)
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
        const DX11MeshResource& gpuMesh = *resource;
        if (submeshIndex >= gpuMesh.SubMeshes.size())
        {
            m_Impl->LastError = "DrawMesh received an invalid submeshIndex";
            CN_LOG_RENDERER("Draw", "잘못된 서브메시 인덱스: ", submeshIndex);
            return;
        }

        using namespace DirectX;

        const XMMATRIX worldMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(world));
        Impl::ObjectConstants constants{};
        XMStoreFloat4x4(&constants.World, worldMatrix);
        XMVECTOR determinant;
        const XMMATRIX inverseWorld = XMMatrixInverse(&determinant, worldMatrix);
        const float det = XMVectorGetX(determinant);
        // Mathematical inverse-transpose for normals, not an upload/packing transpose.
        // Singular transforms use zero normals and the geometric fallback in the pixel shader.
        if (std::isfinite(det) && det != 0.0f && !XMMatrixIsInfinite(inverseWorld) && !XMMatrixIsNaN(inverseWorld))
            XMStoreFloat4x4(&constants.WorldInverseTranspose, XMMatrixTranspose(inverseWorld));

        ID3D11DeviceContext* context = m_Impl->Context.Get();
        context->UpdateSubresource(m_Impl->ObjectBuffer.Get(), 0, nullptr, &constants, 0, 0);
        const DX11MaterialResource* materialResource = m_Impl->Resources.GetMaterial(material);
        const MaterialDesc materialDesc = materialResource ? materialResource->Desc : MaterialDesc{};
        const Impl::MaterialConstants materialConstants{ materialDesc.baseColor, static_cast<uint32_t>(materialDesc.alphaMode), materialDesc.alphaCutoff };
        context->UpdateSubresource(m_Impl->MaterialBuffer.Get(), 0, nullptr, &materialConstants, 0, 0);
        // Bind every draw, including direct DrawMesh calls, so prior pipeline state cannot leak.
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(gpuMesh.Format, materialDesc.alphaMode));
        m_Impl->BindSharedConstants();
        ID3D11Buffer* objectBuffer = m_Impl->ObjectBuffer.Get();
        context->VSSetConstantBuffers(0, 1, &objectBuffer);
        ID3D11Buffer* materialBuffer = m_Impl->MaterialBuffer.Get();
        context->PSSetConstantBuffers(1, 1, &materialBuffer);
        ID3D11ShaderResourceView* textureView = m_Impl->Resources.GetWhiteTextureView();
        if (materialResource)
        {
            if (const auto* texture = m_Impl->Resources.GetTexture(materialResource->BaseColorTexture))
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
            m_Impl->LastError = HResultMessage("IDXGISwapChain::Present", result);
            CN_LOG_RENDERER("Error", m_Impl->LastError);
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
