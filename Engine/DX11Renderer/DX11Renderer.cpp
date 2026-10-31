#include "DX11Renderer/DX11Renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d11.h>
#include "DX11Renderer/Pipeline/DX11PipelineState.h"
#if CANAPE_RENDER_DEBUG
#include "DX11Renderer/Pipeline/GPUProfiler.h"
#endif
#include "DX11Renderer/Resource/DX11ResourceCache.h"
#include <DirectXMath.h>
#include <wrl/client.h>

#include <sstream>
#include <cmath>
#include <algorithm>
#include <array>

#include "CNDebug/DebugLog.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace Canape
{
    namespace
    {
        // All temporary light values live here, outside GPU packing/upload code.
        std::array<RenderLight, 2> CreateTestLights()
        {
            RenderLight directional;
            directional.id = 1;
            directional.type = LightType::Directional;
            directional.direction = { -0.4f, -1.0f, 0.6f };
            directional.color = { 0.85f, 0.85f, 0.9f };
            directional.intensity = 0.9f;
            directional.castShadow = true;

            RenderLight point;
            point.id = 2;
            point.type = LightType::Point;
            point.position = { 2.0f, 3.0f, -2.0f };
            point.color = { 1.0f, 0.45f, 0.18f };
            point.intensity = 2.0f;
            point.range = 12.0f;
            return { directional, point };
        }

        std::span<const RenderLight> SelectLights(const RenderScene& scene)
        {
            // Engine integration: replace this body with: return scene.lights;
            // Both BeginFrame (direct DrawMesh) and DrawScene use this selector.
            (void)scene;
            static const auto testLights = CreateTestLights();
            return testLights;
        }

        bool IsFinite(const Vector3& value)
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

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
        enum class ShadowDebugMode { None, MapDepth, Visibility };

        // Renderer-local settings. Scene/camera-bound fitting can later replace only
        // BuildDirectionalShadowView; no light direction is duplicated here.
        struct DirectionalShadowSettings
        {
            bool enabled = true;
            uint32_t resolution = 2048;
            bool pcf = true; // Hardware 2x2 PCF; false selects a single comparison.
            Vector3 center{};
            float width = 80.0f;
            float height = 80.0f;
            float lightDistance = 80.0f;
            float nearPlane = 0.1f;
            float farPlane = 200.0f;
            float depthBias = 0.0005f; // Receiver depth only; rasterizer bias remains zero.
            ShadowDebugMode debugView = ShadowDebugMode::None; // Debug/Release only.
        };

        struct ShadowMapResources
        {
            ComPtr<ID3D11Texture2D> Texture;
            ComPtr<ID3D11DepthStencilView> DepthView;
            ComPtr<ID3D11ShaderResourceView> ShaderView;
            ComPtr<ID3D11SamplerState> Sampler;
            ComPtr<ID3D11Buffer> Constants;
            uint32_t Resolution = 0;
            bool Pcf = false;
        };

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
            uint32_t HasBaseColorTexture = 0;
            float Padding = 0.0f;
        };
        static_assert(sizeof(MaterialConstants) == 32);
        static_assert(offsetof(MaterialConstants, Mode) == 16 && offsetof(MaterialConstants, AlphaCutoff) == 20);
        static_assert(offsetof(MaterialConstants, HasBaseColorTexture) == 24);

#if CANAPE_RENDER_DEBUG
        struct alignas(16) DepthDisplayConstants
        {
            float Min;
            float Max;
            float Padding[2]{};
        };
        static_assert(sizeof(DepthDisplayConstants) == 16);
#endif

        HWND Window = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool FrameActive = false;
        std::string LastError;
#if CANAPE_RENDER_DEBUG
        RendererDebugSettings DebugSettings{};
        RendererDebugStats CurrentStats{};
        RendererDebugStats CompletedStats{};
        uint64_t NextFrameId = 0;
        GPUProfiler Profiler;
        bool ProfilerAvailable = false;
        uint64_t MinGpuFrameId = 0;
#endif

        ViewConstants ViewData{};
        SceneConstants SceneData{};
        bool ViewDirty = true;
        bool SceneDirty = true;
        RenderScene QueuedScene;
        DirectionalShadowSettings ShadowSettings{};
        bool ShadowActive = false;
        uint32_t AttemptedShadowResolution = UINT32_MAX;
        bool AttemptedShadowPcf = false;

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        // Reverse member destruction releases cached resources before Context and Device.
        // The cache retains neither pointer; all creation calls borrow this renderer's pair.
        DX11ResourceCache Resources;
        DX11PipelineState Pipelines;
        ShadowMapResources ShadowMap;
        ComPtr<ID3D11SamplerState> FallbackShadowSampler;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> RenderTargetView;
        ComPtr<ID3D11Texture2D> SceneColorTexture;
        ComPtr<ID3D11RenderTargetView> SceneColorRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> SceneColorShaderResourceView;
        ComPtr<ID3D11Texture2D> DepthTexture;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;
#if CANAPE_RENDER_DEBUG
        ComPtr<ID3D11ShaderResourceView> DepthShaderResourceView;
#endif
        ComPtr<ID3D11Buffer> ObjectBuffer;
        ComPtr<ID3D11Buffer> MaterialBuffer;
        ComPtr<ID3D11Buffer> ViewBuffer;
        ComPtr<ID3D11Buffer> SceneBuffer;
        ComPtr<ID3D11Buffer> LightBuffer;
#if CANAPE_RENDER_DEBUG
        ComPtr<ID3D11Buffer> DepthDisplayBuffer;
#endif

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

        static LightConstants BuildLightConstants(std::span<const RenderLight> lights,
            uint32_t* shadowDirectionalIndex = nullptr)
        {
            if (shadowDirectionalIndex) *shadowDirectionalIndex = InvalidShadowView;
            // Rebuild the entire buffer, including unused entries and shadow metadata.
            LightConstants data{};
            for (const RenderLight& light : lights)
            {
                if (!IsFinite(light.color) || !std::isfinite(light.intensity) || light.intensity <= 0.0f)
                    continue;
                const Vector3 colorIntensity = light.color * light.intensity;
                if (!IsFinite(colorIntensity)) continue;

                if (light.type == LightType::Directional)
                {
                    if (data.DirectionalCount == MaxDirectionalLights || !IsFinite(light.direction)) continue;
                    const float lengthSquared = light.direction.LengthSquared();
                    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-12f) continue;
                    // The first accepted Directional light owns the one shadow map.
                    // Respect its existing castShadow flag; do not silently choose another light.
                    if (shadowDirectionalIndex && data.DirectionalCount == 0 && light.castShadow)
                        *shadowDirectionalIndex = 0;
                    auto& gpu = data.DirectionalLights[data.DirectionalCount++];
                    gpu.Direction = light.direction / std::sqrt(lengthSquared);
                    gpu.ColorIntensity = colorIntensity;
                }
                else if (light.type == LightType::Point)
                {
                    if (data.LocalCount == MaxLocalLights || !IsFinite(light.position)
                        || !std::isfinite(light.range) || light.range <= 0.0f) continue;
                    auto& gpu = data.LocalLights[data.LocalCount++];
                    gpu.Position = light.position;
                    gpu.Range = light.range;
                    gpu.ColorIntensity = colorIntensity;
                    gpu.Type = static_cast<uint32_t>(LocalLightType::Point);
                }
                // Spot shading is future work. Shadow metadata is published only after a valid pass.
            }
            return data;
        }

        bool EnsureShadowResources()
        {
            if (ShadowMap.Texture && ShadowMap.Resolution == ShadowSettings.resolution
                && ShadowMap.Pcf == ShadowSettings.pcf) return true;
            // Retry after settings change or renderer reinitialization, without logging every frame.
            if (AttemptedShadowResolution == ShadowSettings.resolution && AttemptedShadowPcf == ShadowSettings.pcf)
                return false;
            AttemptedShadowResolution = ShadowSettings.resolution;
            AttemptedShadowPcf = ShadowSettings.pcf;
            auto failed = [](const char* operation, HRESULT result)
            {
                CN_LOG_RENDERER("Shadow", HResultMessage(operation, result), "; rendering without shadows");
                (void)operation;
                (void)result;
                return false;
            };
            if (ShadowSettings.resolution == 0 || ShadowSettings.resolution > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
                return failed("Shadow map resolution", E_INVALIDARG);

            // Publish a complete set only. A failed recreation never exposes a partial texture/view pair.
            ShadowMapResources pending;
            D3D11_TEXTURE2D_DESC texture{};
            texture.Width = texture.Height = ShadowSettings.resolution;
            texture.MipLevels = texture.ArraySize = 1;
            texture.Format = DXGI_FORMAT_R32_TYPELESS;
            texture.SampleDesc.Count = 1;
            texture.Usage = D3D11_USAGE_DEFAULT;
            texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            HRESULT result = Device->CreateTexture2D(&texture, nullptr, &pending.Texture);
            if (FAILED(result)) return failed("CreateTexture2D(shadow)", result);
            D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format = DXGI_FORMAT_D32_FLOAT;
            dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            result = Device->CreateDepthStencilView(pending.Texture.Get(), &dsv, &pending.DepthView);
            if (FAILED(result)) return failed("CreateDepthStencilView(shadow)", result);
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R32_FLOAT;
            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
            result = Device->CreateShaderResourceView(pending.Texture.Get(), &srv, &pending.ShaderView);
            if (FAILED(result)) return failed("CreateShaderResourceView(shadow)", result);
            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter = ShadowSettings.pcf ? D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT
                : D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            sampler.BorderColor[0] = sampler.BorderColor[1] = sampler.BorderColor[2] = sampler.BorderColor[3] = 1.0f;
            sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
            sampler.MaxAnisotropy = 1;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            result = Device->CreateSamplerState(&sampler, &pending.Sampler);
            if (FAILED(result)) return failed("CreateSamplerState(shadow comparison)", result);
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = sizeof(ShadowConstants);
            buffer.Usage = D3D11_USAGE_DEFAULT;
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            result = Device->CreateBuffer(&buffer, nullptr, &pending.Constants);
            if (FAILED(result)) return failed("CreateBuffer(shadow constants)", result);
            pending.Resolution = ShadowSettings.resolution;
            pending.Pcf = ShadowSettings.pcf;
            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(1, 1, &unbound);
            ShadowMap = std::move(pending);
            return true;
        }

        static bool BuildDirectionalShadowView(const Vector3& direction,
            const DirectionalShadowSettings& settings, ShadowViewGpu& output)
        {
            if (!IsFinite(direction) || direction.LengthSquared() < 1e-12f || !IsFinite(settings.center)
                || !std::isfinite(settings.width) || settings.width <= 0.0f
                || !std::isfinite(settings.height) || settings.height <= 0.0f
                || !std::isfinite(settings.lightDistance) || settings.lightDistance <= 0.0f
                || !std::isfinite(settings.nearPlane) || settings.nearPlane < 0.0f
                || !std::isfinite(settings.farPlane) || settings.farPlane <= settings.nearPlane
                || !std::isfinite(settings.depthBias) || settings.depthBias < 0.0f) return false;
            using namespace DirectX;
            const XMVECTOR forward = XMVector3Normalize(direction.ToXM());
            const XMVECTOR eye = XMVectorSubtract(settings.center.ToXM(), XMVectorScale(forward, settings.lightDistance));
            const XMVECTOR up = std::abs(XMVectorGetY(forward)) > 0.99f
                ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
            const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
            const XMMATRIX projection = XMMatrixOrthographicLH(settings.width, settings.height,
                settings.nearPlane, settings.farPlane);
            const XMMATRIX viewProjection = view * projection;
            if (XMMatrixIsNaN(viewProjection) || XMMatrixIsInfinite(viewProjection)) return false;
            // Row-major, left-handed, z [0,1], no upload transpose, same as the camera.
            XMStoreFloat4x4(&output.LightViewProjection, viewProjection);
            output.DepthBias = settings.depthBias;
            output.MapIndex = 0;
            output.NearPlane = settings.nearPlane;
            output.FarPlane = settings.farPlane;
            return true;
        }

        void UploadLights(std::span<const RenderLight> lights, const RenderScene* scene = nullptr)
        {
            uint32_t shadowIndex = InvalidShadowView;
            LightConstants data = BuildLightConstants(lights, &shadowIndex);
            ShadowConstants shadowData{};
            ShadowActive = scene && ShadowSettings.enabled && shadowIndex != InvalidShadowView
                && Pipelines.ShadowsAvailable() && EnsureShadowResources()
                && BuildDirectionalShadowView(data.DirectionalLights[shadowIndex].Direction,
                    ShadowSettings, shadowData.Views[0]);
            if (ShadowActive)
            {
                shadowData.ViewCount = 1;
                auto& light = data.DirectionalLights[shadowIndex];
                light.ShadowViewOffset = 0;
                light.ShadowViewCount = 1;
            }
            // Full uploads clear old views/metadata on every frame and every scene, including fallback.
            Context->UpdateSubresource(LightBuffer.Get(), 0, nullptr, &data, 0, 0);
            if (ShadowMap.Constants)
                Context->UpdateSubresource(ShadowMap.Constants.Get(), 0, nullptr, &shadowData, 0, 0);
            if (ShadowActive) RenderShadowPass(*scene);
        }

        void BindMainTargets()
        {
            ID3D11RenderTargetView* target = SceneColorRenderTargetView.Get();
            Context->OMSetRenderTargets(1, &target, DepthStencilView.Get());
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(Width);
            viewport.Height = static_cast<float>(Height);
            viewport.MaxDepth = 1.0f;
            Context->RSSetViewports(1, &viewport);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        void UpdateObjectConstants(const float* world)
        {
            using namespace DirectX;
            const XMMATRIX worldMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(world));
            ObjectConstants constants{};
            XMStoreFloat4x4(&constants.World, worldMatrix);
            XMVECTOR determinant;
            const XMMATRIX inverseWorld = XMMatrixInverse(&determinant, worldMatrix);
            const float det = XMVectorGetX(determinant);
            // Normal inverse-transpose is mathematical, unrelated to row-major upload packing.
            if (std::isfinite(det) && det != 0.0f && !XMMatrixIsInfinite(inverseWorld) && !XMMatrixIsNaN(inverseWorld))
                XMStoreFloat4x4(&constants.WorldInverseTranspose, XMMatrixTranspose(inverseWorld));
            Context->UpdateSubresource(ObjectBuffer.Get(), 0, nullptr, &constants, 0, 0);
            ID3D11Buffer* buffer = ObjectBuffer.Get();
            Context->VSSetConstantBuffers(0, 1, &buffer);
        }

        MaterialDesc BindMaterial(MaterialResourceId material)
        {
            const DX11MaterialResource* resource = Resources.GetMaterial(material);
            const MaterialDesc desc = resource ? resource->Desc : MaterialDesc{};
            ID3D11ShaderResourceView* textureView = Resources.GetWhiteTextureView();
            uint32_t hasTexture = 0;
            if (resource)
            {
                if (const auto* texture = Resources.GetTexture(resource->BaseColorTexture))
                {
                    textureView = texture->ShaderResourceView.Get();
#if CANAPE_RENDER_DEBUG
                    hasTexture = 1;
#endif
                }
            }
            const MaterialConstants constants{ desc.baseColor, static_cast<uint32_t>(desc.alphaMode),
                desc.alphaCutoff, hasTexture };
            Context->UpdateSubresource(MaterialBuffer.Get(), 0, nullptr, &constants, 0, 0);
            ID3D11Buffer* buffer = MaterialBuffer.Get();
            Context->PSSetConstantBuffers(1, 1, &buffer);
            Context->PSSetShaderResources(0, 1, &textureView);
            return desc;
        }

        void DrawMeshGeometry(const DX11MeshResource& mesh, uint32_t submeshIndex)
        {
            const UINT stride = mesh.Stride;
            constexpr UINT offset = 0;
            ID3D11Buffer* vertexBuffer = mesh.VertexBuffer.Get();
            Context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            Context->IASetIndexBuffer(mesh.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            const SubMesh& part = mesh.SubMeshes[submeshIndex];
            Context->DrawIndexed(part.indexCount, part.startIndex, part.baseVertex);
#if CANAPE_RENDER_DEBUG
            ++CurrentStats.drawCalls;
            CurrentStats.triangleCount += part.indexCount / 3;
#endif
        }

        void RenderShadowPass(const RenderScene& scene)
        {
            // Unbind the read view before binding the same texture as writable depth.
            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(1, 1, &unbound);
            Context->OMSetRenderTargets(0, nullptr, ShadowMap.DepthView.Get());
            Context->ClearDepthStencilView(ShadowMap.DepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
            D3D11_VIEWPORT viewport{};
            viewport.Width = viewport.Height = static_cast<float>(ShadowMap.Resolution);
            viewport.MaxDepth = 1.0f;
            Context->RSSetViewports(1, &viewport);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11Buffer* shadowBuffer = ShadowMap.Constants.Get();
            Context->VSSetConstantBuffers(5, 1, &shadowBuffer);
            for (const RenderItem& item : scene.items)
            {
                if (!item.castShadow) continue;
                const auto* mesh = Resources.GetMesh(item.mesh);
                if (!mesh || item.submeshIndex >= mesh->SubMeshes.size()) continue;
                const auto* material = Resources.GetMaterial(item.material);
                const AlphaMode alpha = material ? material->Desc.alphaMode : AlphaMode::Opaque;
                if (alpha == AlphaMode::Transparent) continue;
                BindPipeline(Pipelines.ResolveShadow(mesh->Format, alpha));
                UpdateObjectConstants(item.world.Data());
                if (alpha == AlphaMode::Masked) BindMaterial(item.material);
                DrawMeshGeometry(*mesh, item.submeshIndex);
            }
            // The camera view constants were never overwritten. Restore camera targets,
            // viewport and the default pipeline; each main draw then binds its own variant.
            BindMainTargets();
            BindPipeline(Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque));
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
            ID3D11Buffer* lightBuffer = LightBuffer.Get();
            Context->VSSetConstantBuffers(2, 1, &viewBuffer);
            Context->PSSetConstantBuffers(2, 1, &viewBuffer);
            Context->PSSetConstantBuffers(3, 1, &lightBuffer);
            Context->PSSetConstantBuffers(4, 1, &sceneBuffer);
            ID3D11Buffer* shadowBuffer = ShadowMap.Constants.Get();
            Context->VSSetConstantBuffers(5, 1, &shadowBuffer);
            Context->PSSetConstantBuffers(5, 1, &shadowBuffer);
            ID3D11ShaderResourceView* shadowView = ShadowActive ? ShadowMap.ShaderView.Get() : nullptr;
            ID3D11SamplerState* shadowSampler = ShadowMap.Sampler ? ShadowMap.Sampler.Get() : FallbackShadowSampler.Get();
            Context->PSSetShaderResources(1, 1, &shadowView);
            Context->PSSetSamplers(1, 1, &shadowSampler);
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

        void RenderFinalOutput()
        {
            // Rebinding the back buffer unbinds both SceneColor RTV and depth DSV before SRV reads.
            ID3D11RenderTargetView* backBuffer = RenderTargetView.Get();
            Context->OMSetRenderTargets(1, &backBuffer, nullptr);

            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(Width);
            viewport.Height = static_cast<float>(Height);
            viewport.MaxDepth = 1.0f;
            Context->RSSetViewports(1, &viewport);

#if CANAPE_RENDER_DEBUG
            const bool cameraDepth = DebugSettings.enabled && DebugSettings.viewMode == DebugViewMode::CameraDepth;
            const bool shadowDepth = !cameraDepth && ShadowActive && ShadowSettings.debugView == ShadowDebugMode::MapDepth;
            BindPipeline(Pipelines.ResolveFinalOutput(cameraDepth || shadowDepth));
            ID3D11ShaderResourceView* output = shadowDepth ? ShadowMap.ShaderView.Get()
                : cameraDepth ? DepthShaderResourceView.Get() : SceneColorShaderResourceView.Get();
#else
            BindPipeline(Pipelines.ResolveFinalOutput(false));
            ID3D11ShaderResourceView* output = SceneColorShaderResourceView.Get();
#endif
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->PSSetShaderResources(0, 1, &output);
#if CANAPE_RENDER_DEBUG
            if (cameraDepth || shadowDepth)
            {
                const DepthDisplayConstants display{ shadowDepth ? 0.0f : DebugSettings.depthMin,
                    shadowDepth ? 1.0f : DebugSettings.depthMax };
                Context->UpdateSubresource(DepthDisplayBuffer.Get(), 0, nullptr, &display, 0, 0);
                ID3D11Buffer* buffer = DepthDisplayBuffer.Get();
                Context->PSSetConstantBuffers(0, 1, &buffer);
            }
#endif
            Context->Draw(3, 0);
#if CANAPE_RENDER_DEBUG
            ++CurrentStats.drawCalls;
            ++CurrentStats.triangleCount;
#endif

            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(0, 1, &unbound);
            ID3D11Buffer* unboundBuffer = nullptr;
            Context->PSSetConstantBuffers(0, 1, &unboundBuffer);
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

            // Match the old back-buffer sRGB render target so scene blending stays linear.
            D3D11_TEXTURE2D_DESC sceneColorDesc{};
            sceneColorDesc.Width = width;
            sceneColorDesc.Height = height;
            sceneColorDesc.MipLevels = 1;
            sceneColorDesc.ArraySize = 1;
            sceneColorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            sceneColorDesc.SampleDesc.Count = 1;
            sceneColorDesc.Usage = D3D11_USAGE_DEFAULT;
            sceneColorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            result = Device->CreateTexture2D(&sceneColorDesc, nullptr, &SceneColorTexture);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateTexture2D(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
            result = Device->CreateRenderTargetView(SceneColorTexture.Get(), nullptr, &SceneColorRenderTargetView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateRenderTargetView(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
            result = Device->CreateShaderResourceView(SceneColorTexture.Get(), nullptr, &SceneColorShaderResourceView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateShaderResourceView(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
#if CANAPE_RENDER_DEBUG
            depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
#else
            depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
#endif
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
#if CANAPE_RENDER_DEBUG
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
#else
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
#endif

            result = Device->CreateTexture2D(&depthDesc, nullptr, &DepthTexture);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateTexture2D(depth)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
            depthViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            result = Device->CreateDepthStencilView(DepthTexture.Get(), &depthViewDesc, &DepthStencilView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateDepthStencilView", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

#if CANAPE_RENDER_DEBUG
            D3D11_SHADER_RESOURCE_VIEW_DESC depthResourceDesc{};
            depthResourceDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            depthResourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            depthResourceDesc.Texture2D.MipLevels = 1;
            result = Device->CreateShaderResourceView(DepthTexture.Get(), &depthResourceDesc, &DepthShaderResourceView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateShaderResourceView(depth)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
#endif

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
            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(0, 1, &unbound);
            RenderTargetView.Reset();
            SceneColorShaderResourceView.Reset();
            SceneColorRenderTargetView.Reset();
            SceneColorTexture.Reset();
#if CANAPE_RENDER_DEBUG
            DepthShaderResourceView.Reset();
#endif
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

            constantBufferDesc.ByteWidth = sizeof(LightConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &LightBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(light constants)", result);
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

#if CANAPE_RENDER_DEBUG
            constantBufferDesc.ByteWidth = sizeof(DepthDisplayConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &DepthDisplayBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(depth display constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
#endif

            // The PS declares a comparison sampler even when shadow branches are disabled.
            // Keep that slot valid before the first map allocation and on resource fallback.
            D3D11_SAMPLER_DESC shadowSampler{};
            shadowSampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
            shadowSampler.AddressU = shadowSampler.AddressV = shadowSampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            shadowSampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
            shadowSampler.MaxAnisotropy = 1;
            shadowSampler.MaxLOD = D3D11_FLOAT32_MAX;
            result = Device->CreateSamplerState(&shadowSampler, &FallbackShadowSampler);
            if (FAILED(result)) CN_LOG_RENDERER("Shadow", HResultMessage("CreateSamplerState(shadow fallback)", result));

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

        if (!m_Impl->CreateTargets(width, height) || !m_Impl->CreatePipeline()) return false;
#if CANAPE_RENDER_DEBUG
        std::string profilerError;
        m_Impl->ProfilerAvailable = m_Impl->Profiler.Initialize(m_Impl->Device.Get(), profilerError);
        if (!m_Impl->ProfilerAvailable) CN_LOG_RENDERER("Profiler", profilerError);
#endif
        return true;
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

    void DX11Renderer::SetDebugSettings(const RendererDebugSettings& settings)
    {
#if CANAPE_RENDER_DEBUG
        if (!m_Impl) return;

        RendererDebugSettings valid = settings;
        if (valid.viewMode != DebugViewMode::Final && valid.viewMode != DebugViewMode::CameraDepth
            && valid.viewMode != DebugViewMode::WorldNormal && valid.viewMode != DebugViewMode::UV
            && valid.viewMode != DebugViewMode::MipLOD)
            valid.viewMode = DebugViewMode::Final;
        if (!std::isfinite(valid.depthMin) || !std::isfinite(valid.depthMax)
            || valid.depthMin < 0.0f || valid.depthMax > 1.0f || valid.depthMin >= valid.depthMax)
        {
            const RendererDebugSettings defaults;
            valid.depthMin = defaults.depthMin;
            valid.depthMax = defaults.depthMax;
        }
        if (valid.captureGpuTimings && !m_Impl->DebugSettings.captureGpuTimings)
            m_Impl->MinGpuFrameId = m_Impl->NextFrameId + 1;
        m_Impl->DebugSettings = valid;
#else
        (void)settings;
#endif
    }

    RendererDebugStats DX11Renderer::GetDebugStats() const
    {
        if (!m_Impl) return {};
        RendererDebugStats stats{};
#if CANAPE_RENDER_DEBUG
        stats = m_Impl->CompletedStats;
        if (m_Impl->DebugSettings.captureGpuTimings && m_Impl->ProfilerAvailable)
        {
            const GpuTimingResult& timing = m_Impl->Profiler.LatestResult();
            if (timing.frameId >= m_Impl->MinGpuFrameId)
            {
                stats.gpuFrameId = timing.frameId;
                stats.gpuTimingValid = timing.valid;
                stats.gpuFrameMs = timing.frameMs;
                stats.drawSceneMs = timing.drawSceneMs;
                stats.finalOutputMs = timing.finalOutputMs;
            }
        }
#endif
        return stats;
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
        if (!m_Impl || !m_Impl->Context || !m_Impl->SwapChain || m_Impl->FrameActive)
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

#if CANAPE_RENDER_DEBUG
        m_Impl->CurrentStats = {};
        m_Impl->CurrentStats.frameId = ++m_Impl->NextFrameId;
        if (m_Impl->ProfilerAvailable)
        {
            m_Impl->Profiler.Poll(context);
            if (m_Impl->DebugSettings.captureGpuTimings)
                m_Impl->Profiler.BeginFrame(context, m_Impl->CurrentStats.frameId);
        }
#endif

        // Linear RGB and linear alpha. The SceneColor sRGB RTV encodes RGB, including clear values.
        const float clearColor[] = { 0.025f, 0.035f, 0.055f, 1.0f };
        context->ClearRenderTargetView(m_Impl->SceneColorRenderTargetView.Get(), clearColor);
        context->ClearDepthStencilView(m_Impl->DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        m_Impl->BindMainTargets();

        // Direct draws start with scene defaults instead of inheriting the previous frame.
        // Always upload a freshly initialized LightConstants, even with no scene draws.
        m_Impl->UploadLights(SelectLights(m_Impl->QueuedScene));
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

#if CANAPE_RENDER_DEBUG
        if (m_Impl->ProfilerAvailable) m_Impl->Profiler.BeginDrawScene(m_Impl->Context.Get());
#endif

        SetCamera(scene.camera.view.Data(), scene.camera.projection.Data());
        m_Impl->ViewData.CameraPosition = scene.camera.cameraPosition;
        m_Impl->SceneData.AmbientColor = scene.ambientColor;
        m_Impl->SceneData.AmbientIntensity = scene.ambientIntensity;
        m_Impl->SceneData.FogColor = scene.fog.color;
        m_Impl->SceneData.FogDensity = scene.fog.density;
        m_Impl->SceneData.FogStartDistance = scene.fog.startDistance;
        m_Impl->SceneData.FogEndDistance = scene.fog.endDistance;
        m_Impl->SceneData.FogMode = static_cast<uint32_t>(scene.fog.mode);
        // Upload even for an empty scene or a second scene within the same frame.
        m_Impl->UploadLights(SelectLights(scene), &scene);
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
#if CANAPE_RENDER_DEBUG
        if (m_Impl->ProfilerAvailable) m_Impl->Profiler.EndDrawScene(m_Impl->Context.Get());
#endif
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

        m_Impl->UpdateObjectConstants(world);
        const MaterialDesc materialDesc = m_Impl->BindMaterial(material);
        // Bind every draw, including direct DrawMesh calls, so prior pipeline state cannot leak.
#if CANAPE_RENDER_DEBUG
        const DebugViewMode meshView = m_Impl->DebugSettings.enabled
            ? m_Impl->DebugSettings.viewMode : DebugViewMode::Final;
        const bool wireframe = m_Impl->DebugSettings.enabled && m_Impl->DebugSettings.wireframe;
        const bool shadowVisibility = m_Impl->ShadowSettings.debugView == Impl::ShadowDebugMode::Visibility;
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(gpuMesh.Format, materialDesc.alphaMode, meshView,
            wireframe, shadowVisibility));
#else
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(gpuMesh.Format, materialDesc.alphaMode));
#endif
        m_Impl->BindSharedConstants();
        m_Impl->DrawMeshGeometry(gpuMesh, submeshIndex);
    }

    void DX11Renderer::EndFrame()
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

#if CANAPE_RENDER_DEBUG
        if (m_Impl->ProfilerAvailable) m_Impl->Profiler.BeginFinalOutput(m_Impl->Context.Get());
#endif
        m_Impl->RenderFinalOutput();
#if CANAPE_RENDER_DEBUG
        if (m_Impl->ProfilerAvailable)
        {
            m_Impl->Profiler.EndFinalOutput(m_Impl->Context.Get());
            m_Impl->Profiler.EndFrame(m_Impl->Context.Get());
        }
#endif
        m_Impl->FrameActive = false;
#if CANAPE_RENDER_DEBUG
        m_Impl->CompletedStats = m_Impl->CurrentStats;
#endif

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
