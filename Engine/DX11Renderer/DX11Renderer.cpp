#include "DX11Renderer/DX11Renderer.h"
#include "DX11Renderer/Data/ShaderConstants.h"

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
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <vector>

#include "CNDebug/DebugLog.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace Canape
{
    namespace SC = ShaderConstants;

    namespace
    {
        struct SkyboxVertex { float Position[2]; };
        static_assert(sizeof(SkyboxVertex) == sizeof(float) * 2);

        // All temporary light values live here, outside GPU packing/upload code.
        [[maybe_unused]] std::array<RenderLight, 2> CreateTestLights()
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
            return scene.lights;
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
        // Renderer-local settings. The camera frustum determines the XY extent;
        // lightDistance and farPlane retain depth room for off-screen casters.
        struct DirectionalShadowSettings
        {
            bool enabled = true;
            uint32_t resolution = 4096*2;
            bool pcf = true; // 3x3 taps with hardware comparison filtering; false selects a single comparison.
            float shadowDistance = 25.0f; // Camera-space extent covered by this shadow map.
            Vector3 center{}; // Legacy fixed-frustum values; fitted projection uses the camera.
            float width = 40.0f;
            float height = 40.0f;
            float lightDistance = 80.0f;
            float nearPlane = 0.1f;
            float farPlane = 200.0f;
            float depthBias = DefaultShadowReceiverBias; // Receiver comparison bias.
            bool pcss = true;
            float lightAngularRadius = 0.02f; // Radians; directional light's virtual angular radius.
            float blockerSearchRadiusWorld = 0.125f;
            float minFilterRadiusWorld = 0.0f; // One shadow texel is the shader-side floor.
            float maxFilterRadiusWorld = 0.4f / 1.5f; // Base penumbra cap; final taps are also clamped to the search radius.
            float pcfTapSpacingScale = 1.0f; // Diagnostic multiplier after penumbra estimation.
            uint32_t pcfSampleCount = 64;
            uint32_t blockerSampleCount = 128;
            bool legacyPcf25 = true; // Original 25 offsets remain available for comparison.
        };

        struct ShadowMapResources
        {
            ComPtr<ID3D11Texture2D> Texture;
            ComPtr<ID3D11DepthStencilView> DepthView;
            ComPtr<ID3D11ShaderResourceView> ShaderView;
            ComPtr<ID3D11SamplerState> PointSampler;
            ComPtr<ID3D11SamplerState> PcfSampler;
            ComPtr<ID3D11Buffer> Constants;
            ComPtr<ID3D11Buffer> PcssConstants;
            uint32_t Resolution = 0;
        };

        HWND Window = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        uint32_t BackBufferWidth = 0;
        uint32_t BackBufferHeight = 0;
        uint32_t RequestedSceneWidth = 0;
        uint32_t RequestedSceneHeight = 0;
        std::function<void()> Overlay;
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

        SC::ViewConstants ViewData{};
        SC::SceneConstants SceneData{};
        bool ViewDirty = true;
        bool SceneDirty = true;
        RenderScene QueuedScene;
        DirectionalShadowSettings ShadowSettings{};
        bool ShadowActive = false;
        uint32_t AttemptedShadowResolution = UINT32_MAX;

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        // Reverse member destruction releases cached resources before Context and Device.
        // The cache retains neither pointer; all creation calls borrow this renderer's pair.
        DX11ResourceCache Resources;
        ComPtr<ID3D11Buffer> SkyboxVertexBuffer;
        ComPtr<ID3D11Buffer> SkyboxIndexBuffer;
        static constexpr UINT SkyboxIndexCount = 6;
        DX11PipelineState Pipelines;
        ShadowMapResources ShadowMap;
        ComPtr<ID3D11SamplerState> FallbackShadowSampler;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> RenderTargetView;
        ComPtr<ID3D11RenderTargetView> OverlayRenderTargetView;
        ComPtr<ID3D11Texture2D> SceneColorTexture;
        ComPtr<ID3D11RenderTargetView> SceneColorRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> SceneColorShaderResourceView;
        ComPtr<ID3D11Texture2D> DisplayTexture;
        ComPtr<ID3D11RenderTargetView> DisplayRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> DisplayShaderResourceView;
        ComPtr<ID3D11ShaderResourceView> RetiredDisplayShaderResourceView;
        ComPtr<ID3D11Texture2D> DepthTexture;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;
#if CANAPE_RENDER_DEBUG
        ComPtr<ID3D11ShaderResourceView> DepthShaderResourceView;
#endif
        ComPtr<ID3D11Buffer> ObjectBuffer;
        ComPtr<ID3D11Buffer> MaterialBuffer;
        ComPtr<ID3D11Buffer> ViewBuffer;
        ComPtr<ID3D11Buffer> SkyboxViewBuffer;
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

        static SC::LightConstants BuildLightConstants(std::span<const RenderLight> lights,
            uint32_t* shadowDirectionalIndex = nullptr)
        {
            if (shadowDirectionalIndex) *shadowDirectionalIndex = SC::InvalidShadowView;
            // Rebuild the entire buffer, including unused entries and shadow metadata.
            SC::LightConstants data{};
            for (const RenderLight& light : lights)
            {
                if (!IsFinite(light.color) || !std::isfinite(light.intensity) || light.intensity <= 0.0f)
                    continue;
                const Vector3 colorIntensity = light.color * light.intensity;
                if (!IsFinite(colorIntensity)) continue;

                if (light.type == LightType::Directional)
                {
                    if (data.DirectionalCount == SC::MaxDirectionalLights || !IsFinite(light.direction)) continue;
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
                    if (data.LocalCount == SC::MaxLocalLights || !IsFinite(light.position)
                        || !std::isfinite(light.range) || light.range <= 0.0f) continue;
                    auto& gpu = data.LocalLights[data.LocalCount++];
                    gpu.Position = light.position;
                    gpu.Range = light.range;
                    gpu.ColorIntensity = colorIntensity;
                    gpu.Type = static_cast<uint32_t>(SC::LocalLightType::Point);
                }
                // Spot shading is future work. Shadow metadata is published only after a valid pass.
            }
            return data;
        }

        bool EnsureShadowResources()
        {
            if (ShadowMap.Texture && ShadowMap.Resolution == ShadowSettings.resolution) return true;
            // Retry after a resolution change or renderer reinitialization, without logging every frame.
            if (AttemptedShadowResolution == ShadowSettings.resolution) return false;
            AttemptedShadowResolution = ShadowSettings.resolution;
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
            sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            sampler.BorderColor[0] = sampler.BorderColor[1] = sampler.BorderColor[2] = sampler.BorderColor[3] = 1.0f;
            sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
            sampler.MaxAnisotropy = 1;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            result = Device->CreateSamplerState(&sampler, &pending.PointSampler);
            if (FAILED(result)) return failed("CreateSamplerState(shadow point comparison)", result);
            sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            result = Device->CreateSamplerState(&sampler, &pending.PcfSampler);
            if (FAILED(result)) return failed("CreateSamplerState(shadow PCF comparison)", result);
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = sizeof(SC::ShadowConstants);
            buffer.Usage = D3D11_USAGE_DEFAULT;
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            result = Device->CreateBuffer(&buffer, nullptr, &pending.Constants);
            if (FAILED(result)) return failed("CreateBuffer(shadow constants)", result);
            buffer.ByteWidth = sizeof(SC::PcssConstants);
            result = Device->CreateBuffer(&buffer, nullptr, &pending.PcssConstants);
            if (FAILED(result)) return failed("CreateBuffer(PCSS constants)", result);
            pending.Resolution = ShadowSettings.resolution;
            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(1, 1, &unbound);
            ShadowMap = std::move(pending);
            return true;
        }

        static bool BuildFrustumBoundingSphere(const SC::ViewConstants& camera, float shadowDistance, Vector3& center,
            float& radius, double& fittedRadius)
        {
            using namespace DirectX;
            const XMMATRIX projection = XMLoadFloat4x4(&camera.Projection);
            const XMMATRIX view = XMLoadFloat4x4(&camera.View);
            XMVECTOR projectionDeterminant, viewDeterminant;
            const XMMATRIX inverseProjection = XMMatrixInverse(&projectionDeterminant, projection);
            const XMMATRIX inverseView = XMMatrixInverse(&viewDeterminant, view);
            const float projectionDet = XMVectorGetX(projectionDeterminant);
            const float viewDet = XMVectorGetX(viewDeterminant);
            if (!std::isfinite(projectionDet) || projectionDet == 0.0f
                || !std::isfinite(viewDet) || viewDet == 0.0f
                || XMMatrixIsNaN(inverseProjection) || XMMatrixIsInfinite(inverseProjection)
                || XMMatrixIsNaN(inverseView) || XMMatrixIsInfinite(inverseView)) return false;

            const XMVECTOR nearCenter = XMVector3TransformCoord(XMVectorSet(0, 0, 0, 1), inverseProjection);
            const XMVECTOR farCenter = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 1), inverseProjection);
            const float cameraNear = XMVectorGetZ(nearCenter);
            const float cameraFar = XMVectorGetZ(farCenter);
            if (!std::isfinite(shadowDistance) || !std::isfinite(cameraNear) || !std::isfinite(cameraFar)
                || cameraNear < 0.0f || cameraFar <= cameraNear) return false;
            const float shadowFar = std::min(cameraFar, shadowDistance);
            if (shadowFar <= cameraNear) return false;
            const float farFraction = (shadowFar - cameraNear) / (cameraFar - cameraNear);

            std::array<XMFLOAT3, 8> viewCorners{};
            double worldX = 0.0, worldY = 0.0, worldZ = 0.0;
            double viewX = 0.0, viewY = 0.0, viewZ = 0.0;
            size_t index = 0;
            // DirectX LH clip space has x/y in [-1,1] and z in [0,1].
            // Interpolating each near-to-far edge caps the camera frustum without
            // changing its projection or requiring a separate camera matrix.
            for (float z : { 0.0f, 1.0f })
                for (float y : { -1.0f, 1.0f })
                    for (float x : { -1.0f, 1.0f })
                    {
                        const XMVECTOR nearCorner = XMVector3TransformCoord(XMVectorSet(x, y, 0, 1), inverseProjection);
                        const XMVECTOR farCorner = XMVector3TransformCoord(XMVectorSet(x, y, 1, 1), inverseProjection);
                        const XMVECTOR viewCorner = z == 0.0f ? nearCorner
                            : XMVectorLerp(nearCorner, farCorner, farFraction);
                        const XMVECTOR worldCorner = XMVector3TransformCoord(viewCorner, inverseView);
                        XMFLOAT3 worldPoint;
                        XMStoreFloat3(&viewCorners[index++], viewCorner);
                        XMStoreFloat3(&worldPoint, worldCorner);
                        if (!std::isfinite(worldPoint.x) || !std::isfinite(worldPoint.y)
                            || !std::isfinite(worldPoint.z)) return false;
                        worldX += worldPoint.x; worldY += worldPoint.y; worldZ += worldPoint.z;
                        viewX += XMVectorGetX(viewCorner);
                        viewY += XMVectorGetY(viewCorner);
                        viewZ += XMVectorGetZ(viewCorner);
                    }
            center = { static_cast<float>(worldX / 8.0), static_cast<float>(worldY / 8.0),
                static_cast<float>(worldZ / 8.0) };
            if (!IsFinite(center)) return false;

            // Distances in camera space are rotation invariant. Computing them from
            // world corners would introduce view-inverse rounding into the radius.
            double radiusSquared = 0.0;
            for (const XMFLOAT3& corner : viewCorners)
            {
                const double dx = corner.x - viewX / 8.0;
                const double dy = corner.y - viewY / 8.0;
                const double dz = corner.z - viewZ / 8.0;
                radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
            }
            fittedRadius = std::sqrt(radiusSquared);
            // Fixed, projection-dependent margin covers float transforms of the
            // center and corners without allowing camera rotation to resize XY.
            radius = static_cast<float>(fittedRadius * 1.001 + 0.01);
            return std::isfinite(radius) && radius > 0.0f;
        }

        bool BuildDirectionalShadowView(const Vector3& direction,
            const DirectionalShadowSettings& settings, SC::ShadowViewGpu& output, SC::PcssConstants& pcssOutput)
        {
            if (!IsFinite(direction) || direction.LengthSquared() < 1e-12f
                || !std::isfinite(settings.lightDistance) || settings.lightDistance <= 0.0f
                || !std::isfinite(settings.nearPlane) || settings.nearPlane < 0.0f
                || !std::isfinite(settings.farPlane) || settings.farPlane <= settings.nearPlane
                || !std::isfinite(settings.depthBias) || settings.depthBias < 0.0f) return false;
            using namespace DirectX;
            Vector3 center;
            float radius;
            double fittedRadius;
            if (!BuildFrustumBoundingSphere(ViewData, settings.shadowDistance, center, radius, fittedRadius)
                || ShadowMap.Resolution == 0) return false;
            // A snapped center can move by half a texel on each light-space axis.
            // Expand XY only when the sphere's existing padding is insufficient.
            const double resolution = static_cast<double>(ShadowMap.Resolution);
            const double requiredRadius = ShadowMap.Resolution > 1
                ? fittedRadius / (1.0 - 1.0 / resolution) : fittedRadius;
            const float projectionRadius = requiredRadius > radius
                ? std::nextafter(static_cast<float>(requiredRadius), std::numeric_limits<float>::infinity())
                : radius;
            const float projectionWidth = 2.0f * projectionRadius;
            const float projectionHeight = 2.0f * projectionRadius;
            const float texelSizeX = projectionWidth / ShadowMap.Resolution;
            const float texelSizeY = projectionHeight / ShadowMap.Resolution;
            if (!std::isfinite(projectionWidth) || !std::isfinite(projectionHeight)
                || !std::isfinite(texelSizeX) || !std::isfinite(texelSizeY)
                || texelSizeX <= 0.0f || texelSizeY <= 0.0f) return false;
            // Snapping changes only XY; retain the existing depth placement and range.
            const float lightDistance = radius + settings.lightDistance;
            const float farPlane = 2.0f * radius + settings.farPlane;
            if (!std::isfinite(lightDistance) || !std::isfinite(farPlane)
                || farPlane <= settings.nearPlane) return false;
            const XMVECTOR forward = XMVector3Normalize(direction.ToXM());
            const XMVECTOR up = std::abs(XMVectorGetY(forward)) > 0.99f
                ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
            // A light view at the world origin supplies fixed axes. A view centered
            // on the sphere would always report (0,0) and cannot define a grid.
            const XMMATRIX lightAxes = XMMatrixLookToLH(XMVectorZero(), forward, up);
            const XMVECTOR centerLS = XMVector3TransformNormal(center.ToXM(), lightAxes);
            const float originalX = XMVectorGetX(centerLS);
            const float originalY = XMVectorGetY(centerLS);
            const float snappedX = ShadowMap.Resolution > 1
                ? std::round(originalX / texelSizeX) * texelSizeX : originalX;
            const float snappedY = ShadowMap.Resolution > 1
                ? std::round(originalY / texelSizeY) * texelSizeY : originalY;
            const XMVECTOR offsetLS = XMVectorSet(snappedX - originalX, snappedY - originalY, 0.0f, 0.0f);
            const XMVECTOR snappedCenter = XMVectorAdd(center.ToXM(),
                XMVector3TransformNormal(offsetLS, XMMatrixTranspose(lightAxes)));
            if (!std::isfinite(snappedX) || !std::isfinite(snappedY)
                || !IsFinite(Vector3::FromXM(snappedCenter))) return false;
            const XMVECTOR eye = XMVectorSubtract(snappedCenter, XMVectorScale(forward, lightDistance));
            const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
            const XMMATRIX projection = XMMatrixOrthographicOffCenterLH(-projectionRadius, projectionRadius,
                -projectionRadius, projectionRadius,
                settings.nearPlane, farPlane);
            const XMMATRIX viewProjection = view * projection;
            if (XMMatrixIsNaN(viewProjection) || XMMatrixIsInfinite(viewProjection)) return false;
            // Row-major, left-handed, z [0,1], no upload transpose, same as the camera.
            XMStoreFloat4x4(&output.LightViewProjection, viewProjection);
            output.DepthBias = settings.depthBias;
            output.MapIndex = 0;
            output.NearPlane = settings.nearPlane;
            output.FarPlane = farPlane;
            pcssOutput.OrthoSize = { projectionWidth, projectionHeight };
            return true;
        }

        void UploadLights(std::span<const RenderLight> lights, const RenderScene* scene = nullptr)
        {
            uint32_t shadowIndex = SC::InvalidShadowView;
            SC::LightConstants data = BuildLightConstants(lights, &shadowIndex);
            SC::ShadowConstants shadowData{};
            shadowData.PcfEnabled = ShadowSettings.pcf ? 1u : 0u;
            SC::PcssConstants pcssData{};
            pcssData.TanLightAngularRadius = std::tan(ShadowSettings.lightAngularRadius);
            pcssData.BlockerSearchRadiusWorld = ShadowSettings.blockerSearchRadiusWorld;
            pcssData.MinFilterRadiusWorld = ShadowSettings.minFilterRadiusWorld;
            pcssData.MaxFilterRadiusWorld = ShadowSettings.maxFilterRadiusWorld;
            pcssData.PcfSampleCount = ShadowSettings.pcfSampleCount;
            pcssData.PcfTapSpacingScale = ShadowSettings.pcfTapSpacingScale;
            pcssData.BlockerSampleCount = ShadowSettings.blockerSampleCount;
            pcssData.FilterMode = !ShadowSettings.pcf ? 0u : !ShadowSettings.pcss ? 1u
                : ShadowSettings.pcfSampleCount == 25 && ShadowSettings.legacyPcf25 ? 3u : 2u;
            ShadowActive = scene && ShadowSettings.enabled && shadowIndex != SC::InvalidShadowView
                && Pipelines.ShadowsAvailable() && EnsureShadowResources()
                && BuildDirectionalShadowView(data.DirectionalLights[shadowIndex].Direction,
                    ShadowSettings, shadowData.Views[0], pcssData);
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
            if (ShadowMap.PcssConstants)
                Context->UpdateSubresource(ShadowMap.PcssConstants.Get(), 0, nullptr, &pcssData, 0, 0);
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
            SC::ObjectConstants constants{};
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
            const SC::MaterialConstants constants{ desc.baseColor, static_cast<uint32_t>(desc.alphaMode),
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
            ID3D11Buffer* pcssBuffer = ShadowMap.PcssConstants.Get();
            Context->PSSetConstantBuffers(7, 1, &pcssBuffer);
            ID3D11ShaderResourceView* shadowView = ShadowActive ? ShadowMap.ShaderView.Get() : nullptr;
            ID3D11SamplerState* shadowSampler = ShadowSettings.pcf
                ? ShadowMap.PcfSampler.Get() : ShadowMap.PointSampler.Get();
            if (!shadowSampler) shadowSampler = FallbackShadowSampler.Get();
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

        void DrawSkybox(const RenderSkybox& skybox)
        {
            const DX11CubemapResource* cubemap = Resources.GetCubemap(skybox.cubemap);
            if (!cubemap || !cubemap->ShaderResourceView || !SkyboxVertexBuffer || !SkyboxIndexBuffer
                || !SkyboxViewBuffer) return;

            using namespace DirectX;
            const XMMATRIX projection = XMLoadFloat4x4(&ViewData.Projection);
            const XMMATRIX view = XMLoadFloat4x4(&ViewData.View);
            XMVECTOR projectionDeterminant;
            XMVECTOR viewDeterminant;
            const XMMATRIX inverseProjection = XMMatrixInverse(&projectionDeterminant, projection);
            const XMMATRIX inverseView = XMMatrixInverse(&viewDeterminant, view);
            const float projectionDet = XMVectorGetX(projectionDeterminant);
            const float viewDet = XMVectorGetX(viewDeterminant);
            if (!std::isfinite(projectionDet) || projectionDet == 0.0f
                || !std::isfinite(viewDet) || viewDet == 0.0f
                || XMMatrixIsNaN(inverseProjection) || XMMatrixIsInfinite(inverseProjection)
                || XMMatrixIsNaN(inverseView) || XMMatrixIsInfinite(inverseView)) return;

            SC::SkyboxViewConstants constants{};
            // Existing shaders use row-major mul(vector, matrix); upload without a transpose.
            XMStoreFloat4x4(&constants.InverseProjection, inverseProjection);
            XMStoreFloat4x4(&constants.InverseView, inverseView);
            constants.Brightness = std::isfinite(skybox.brightness) ? std::max(0.0f, skybox.brightness) : 1.0f;
            const float rotation = std::isfinite(skybox.rotationRadians) ? skybox.rotationRadians : 0.0f;
            constants.RotationCos = std::cos(rotation);
            constants.RotationSin = std::sin(rotation);
            Context->UpdateSubresource(SkyboxViewBuffer.Get(), 0, nullptr, &constants, 0, 0);

            BindMainTargets();
            const DX11PipelineSelection pipeline = Pipelines.ResolveSkybox();
            BindPipeline(pipeline);
            const UINT stride = sizeof(SkyboxVertex);
            constexpr UINT offset = 0;
            ID3D11Buffer* vertexBuffer = SkyboxVertexBuffer.Get();
            Context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            Context->IASetIndexBuffer(SkyboxIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11Buffer* viewBuffer = SkyboxViewBuffer.Get();
            Context->VSSetConstantBuffers(6, 1, &viewBuffer);
            Context->PSSetConstantBuffers(6, 1, &viewBuffer);
            ID3D11ShaderResourceView* cubeView = cubemap->ShaderResourceView.Get();
            Context->PSSetShaderResources(2, 1, &cubeView);
            Context->PSSetSamplers(2, 1, &pipeline.Sampler);
            Context->DrawIndexed(SkyboxIndexCount, 0, 0);
#if CANAPE_RENDER_DEBUG
            ++CurrentStats.drawCalls;
            CurrentStats.triangleCount += SkyboxIndexCount / 3;
#endif
            ID3D11ShaderResourceView* unboundView = nullptr;
            ID3D11SamplerState* unboundSampler = nullptr;
            ID3D11Buffer* unboundBuffer = nullptr;
            Context->PSSetShaderResources(2, 1, &unboundView);
            Context->PSSetSamplers(2, 1, &unboundSampler);
            Context->VSSetConstantBuffers(6, 1, &unboundBuffer);
            Context->PSSetConstantBuffers(6, 1, &unboundBuffer);
        }

        void RenderFinalOutput(ID3D11RenderTargetView* target, uint32_t width, uint32_t height)
        {
            // Rebinding the back buffer unbinds both SceneColor RTV and depth DSV before SRV reads.
            Context->OMSetRenderTargets(1, &target, nullptr);

            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(width);
            viewport.Height = static_cast<float>(height);
            viewport.MaxDepth = 1.0f;
            Context->RSSetViewports(1, &viewport);

#if CANAPE_RENDER_DEBUG
            const bool cameraDepth = DebugSettings.enabled
                && DebugSettings.viewMode == DebugViewMode::CameraDepth;
            const bool shadowDepth = DebugSettings.enabled
                && DebugSettings.viewMode == DebugViewMode::ShadowDepth
                && ShadowActive;
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
                const SC::DepthDisplayConstants display{ shadowDepth ? 0.0f : DebugSettings.depthMin,
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

        void RenderOverlay()
        {
            ID3D11RenderTargetView* target = OverlayRenderTargetView.Get();
            Context->OMSetRenderTargets(1, &target, nullptr);

            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(BackBufferWidth);
            viewport.Height = static_cast<float>(BackBufferHeight);
            viewport.MaxDepth = 1.0f;
            Context->RSSetViewports(1, &viewport);

            const float clearColor[] = { 0.1f, 0.1f, 0.1f, 1.0f };
            Context->ClearRenderTargetView(target, clearColor);
            Overlay();
        }

        bool CreateTargets(uint32_t width, uint32_t height)
        {
            return CreateBackBufferTargets(width, height) && CreateSceneTargets(width, height);
        }

        bool CreateBackBufferTargets(uint32_t width, uint32_t height)
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

            renderTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            result = Device->CreateRenderTargetView(backBuffer.Get(), &renderTargetDesc, &OverlayRenderTargetView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateRenderTargetView(back buffer, RGBA8_UNORM)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            BackBufferWidth = width;
            BackBufferHeight = height;
            return true;
        }

        bool CreateSceneTargets(uint32_t width, uint32_t height)
        {
            // Match the old back-buffer sRGB render target so scene blending stays linear.
            D3D11_TEXTURE2D_DESC sceneColorDesc{};
            sceneColorDesc.Width = width;
            sceneColorDesc.Height = height;
            sceneColorDesc.MipLevels = 1;
            sceneColorDesc.ArraySize = 1;
            sceneColorDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
            sceneColorDesc.SampleDesc.Count = 1;
            sceneColorDesc.Usage = D3D11_USAGE_DEFAULT;
            sceneColorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            HRESULT result = Device->CreateTexture2D(&sceneColorDesc, nullptr, &SceneColorTexture);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateTexture2D(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            D3D11_RENDER_TARGET_VIEW_DESC sceneTargetDesc{};
            sceneTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            sceneTargetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            result = Device->CreateRenderTargetView(SceneColorTexture.Get(), &sceneTargetDesc, &SceneColorRenderTargetView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateRenderTargetView(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC sceneResourceDesc{};
            sceneResourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            sceneResourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sceneResourceDesc.Texture2D.MipLevels = 1;
            result = Device->CreateShaderResourceView(SceneColorTexture.Get(), &sceneResourceDesc, &SceneColorShaderResourceView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateShaderResourceView(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            result = Device->CreateTexture2D(&sceneColorDesc, nullptr, &DisplayTexture);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateTexture2D(display)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
            result = Device->CreateRenderTargetView(DisplayTexture.Get(), &sceneTargetDesc, &DisplayRenderTargetView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateRenderTargetView(display)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
            sceneResourceDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            result = Device->CreateShaderResourceView(DisplayTexture.Get(), &sceneResourceDesc, &DisplayShaderResourceView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateShaderResourceView(display)", result);
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
            const uint32_t sceneWidth = RequestedSceneWidth ? RequestedSceneWidth : width;
            const uint32_t sceneHeight = RequestedSceneHeight ? RequestedSceneHeight : height;
            const bool backBufferChanged = width != BackBufferWidth || height != BackBufferHeight;
            const bool sceneChanged = sceneWidth != Width || sceneHeight != Height;
            if (!backBufferChanged && !sceneChanged)
            {
                return true;
            }

            Context->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(0, 1, &unbound);

            if (sceneChanged)
            {
                RetiredDisplayShaderResourceView = std::move(DisplayShaderResourceView);
                DisplayRenderTargetView.Reset();
                DisplayTexture.Reset();
                SceneColorShaderResourceView.Reset();
                SceneColorRenderTargetView.Reset();
                SceneColorTexture.Reset();
#if CANAPE_RENDER_DEBUG
                DepthShaderResourceView.Reset();
#endif
                DepthStencilView.Reset();
                DepthTexture.Reset();
            }

            if (backBufferChanged)
            {
                RenderTargetView.Reset();
                OverlayRenderTargetView.Reset();
                const HRESULT result = SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
                if (FAILED(result))
                {
                    LastError = HResultMessage("IDXGISwapChain::ResizeBuffers", result);
                    CN_LOG_RENDERER("Error", LastError);
                    return false;
                }
                if (!CreateBackBufferTargets(width, height))
                {
                    return false;
                }
            }

            return !sceneChanged || CreateSceneTargets(sceneWidth, sceneHeight);
        }

        bool CreateSkyboxQuad()
        {
            constexpr std::array<SkyboxVertex, 4> vertices{{
                {{ -1.0f,  1.0f }},
                {{  1.0f,  1.0f }},
                {{  1.0f, -1.0f }},
                {{ -1.0f, -1.0f }}
            }};
            constexpr std::array<uint32_t, SkyboxIndexCount> indices{ 0, 1, 2, 0, 2, 3 };

            D3D11_BUFFER_DESC vertexDesc{};
            vertexDesc.ByteWidth = sizeof(vertices);
            vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
            vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vertexData{ vertices.data() };
            ComPtr<ID3D11Buffer> vertexBuffer;
            HRESULT result = Device->CreateBuffer(&vertexDesc, &vertexData, &vertexBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(skybox vertices)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            D3D11_BUFFER_DESC indexDesc{};
            indexDesc.ByteWidth = sizeof(indices);
            indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
            indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA indexData{ indices.data() };
            ComPtr<ID3D11Buffer> indexBuffer;
            result = Device->CreateBuffer(&indexDesc, &indexData, &indexBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(skybox indices)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            SkyboxVertexBuffer = std::move(vertexBuffer);
            SkyboxIndexBuffer = std::move(indexBuffer);
            return true;
        }

        bool CreatePipeline()
        {
            if (!Pipelines.Initialize(Device.Get(), LastError)) return false;

            D3D11_BUFFER_DESC constantBufferDesc{};
            constantBufferDesc.ByteWidth = sizeof(SC::ObjectConstants);
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
            constantBufferDesc.ByteWidth = sizeof(SC::MaterialConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &MaterialBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(material constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(SC::ViewConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &ViewBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(view constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(SC::SkyboxViewConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &SkyboxViewBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(skybox view constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(SC::LightConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &LightBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(light constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(SC::SceneConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &SceneBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(scene constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

#if CANAPE_RENDER_DEBUG
            constantBufferDesc.ByteWidth = sizeof(SC::DepthDisplayConstants);
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

        if (!m_Impl->CreateTargets(width, height) || !m_Impl->CreatePipeline()
            || !m_Impl->CreateSkyboxQuad()) return false;
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
            && valid.viewMode != DebugViewMode::MipLOD && valid.viewMode != DebugViewMode::ShadowDepth
            && valid.viewMode != DebugViewMode::ShadowVisibility
            && valid.viewMode != DebugViewMode::PcssFilterRadius)
            valid.viewMode = DebugViewMode::Final;
        const RendererDebugSettings defaults;
        if (!std::isfinite(valid.depthMin) || !std::isfinite(valid.depthMax)
            || valid.depthMin < 0.0f || valid.depthMax > 1.0f || valid.depthMin >= valid.depthMax)
        {
            valid.depthMin = defaults.depthMin;
            valid.depthMax = defaults.depthMax;
        }
        if (valid.shadowCullMode != ShadowCullMode::None && valid.shadowCullMode != ShadowCullMode::Front
            && valid.shadowCullMode != ShadowCullMode::Back)
            valid.shadowCullMode = defaults.shadowCullMode;
        if (!std::isfinite(valid.shadowSlopeBias) || valid.shadowSlopeBias < 0.0f)
            valid.shadowSlopeBias = defaults.shadowSlopeBias;
        if (!std::isfinite(valid.shadowReceiverBias) || valid.shadowReceiverBias < 0.0f)
            valid.shadowReceiverBias = defaults.shadowReceiverBias;
        if (m_Impl->Device && m_Impl->Pipelines.ShadowsAvailable()
            && (valid.shadowCullMode != m_Impl->DebugSettings.shadowCullMode
                || valid.shadowSlopeBias != m_Impl->DebugSettings.shadowSlopeBias))
        {
            std::string error;
            if (!m_Impl->Pipelines.SetShadowRasterizer(m_Impl->Device.Get(),
                valid.shadowCullMode, valid.shadowSlopeBias, error))
            {
                CN_LOG_RENDERER("Shadow", error);
                valid.shadowCullMode = m_Impl->DebugSettings.shadowCullMode;
                valid.shadowSlopeBias = m_Impl->DebugSettings.shadowSlopeBias;
            }
        }
        m_Impl->ShadowSettings.depthBias = valid.shadowReceiverBias;
        m_Impl->ShadowSettings.pcf = valid.shadowPcf;
        if (valid.captureGpuTimings && !m_Impl->DebugSettings.captureGpuTimings)
            m_Impl->MinGpuFrameId = m_Impl->NextFrameId + 1;
        m_Impl->DebugSettings = valid;
#else
        (void)settings;
#endif
    }

    RendererDebugSettings DX11Renderer::GetDebugSettings() const
    {
#if CANAPE_RENDER_DEBUG
        if (m_Impl) return m_Impl->DebugSettings;
#endif
        return {};
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

    bool DX11Renderer::SetShadowFilterSettings(const ShadowFilterSettings& settings)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();

        const auto isSampleCount = [](uint32_t count)
        {
            return std::find(std::begin(ShadowSampleCountOptions), std::end(ShadowSampleCountOptions), count)
                != std::end(ShadowSampleCountOptions);
        };
        if (!isSampleCount(settings.pcfSampleCount) || !isSampleCount(settings.blockerSampleCount)
            || !std::isfinite(settings.shadowDistance) || settings.shadowDistance < MinShadowDistance
            || !std::isfinite(settings.pcfTapSpacingScale) || settings.pcfTapSpacingScale <= 0.0f
            || !std::isfinite(settings.blockerSearchRadius) || settings.blockerSearchRadius <= 0.0f)
            return false;

        auto& shadow = m_Impl->ShadowSettings;
        shadow.pcss = settings.pcss;
        shadow.shadowDistance = settings.shadowDistance;
        shadow.pcfSampleCount = settings.pcfSampleCount;
        shadow.blockerSampleCount = settings.blockerSampleCount;
        shadow.legacyPcf25 = settings.legacyPcf25;
        shadow.pcfTapSpacingScale = settings.pcfTapSpacingScale;
        shadow.blockerSearchRadiusWorld = settings.blockerSearchRadius;
        return true;
    }

    ShadowFilterSettings DX11Renderer::GetShadowFilterSettings() const
    {
        if (!m_Impl) return {};
        const auto& shadow = m_Impl->ShadowSettings;
        ShadowFilterSettings settings;
        settings.pcss = shadow.pcss;
        settings.shadowDistance = shadow.shadowDistance;
        settings.pcfSampleCount = shadow.pcfSampleCount;
        settings.blockerSampleCount = shadow.blockerSampleCount;
        settings.legacyPcf25 = shadow.legacyPcf25;
        settings.pcfTapSpacingScale = shadow.pcfTapSpacingScale;
        settings.blockerSearchRadius = shadow.blockerSearchRadiusWorld;
        return settings;
    }

    void DX11Renderer::SetRenderView(const RenderView& view)
    {
        if (m_Impl) m_Impl->QueuedScene.camera = view;
    }

    void DX11Renderer::AddRenderItem(const RenderItem& item)
    {
        if (m_Impl) m_Impl->QueuedScene.items.push_back(item);
    }

    void DX11Renderer::AddLight(const RenderLight& light)
    {
        if (m_Impl) m_Impl->QueuedScene.lights.push_back(light);
    }

    void DX11Renderer::SetFog(const RenderFog& fog)
    {
        if (m_Impl) m_Impl->QueuedScene.fog = fog;
    }

    void DX11Renderer::SetSkybox(const RenderSkybox& skybox)
    {
        if (m_Impl) m_Impl->QueuedScene.skybox = skybox;
    }

    void DX11Renderer::SetAmbient(const Vector3& color, float intensity)
    {
        if (!m_Impl) return;
        m_Impl->QueuedScene.ambientColor = color;
        m_Impl->QueuedScene.ambientIntensity = intensity;
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

    CubemapResourceId DX11Renderer::CreateCubemap(uint32_t faceSize, uint32_t mipLevels, TextureFormat format, std::span<const uint8_t> pixels)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.CreateCubemap(m_Impl->Device.Get(), m_Impl->Context.Get(),
            faceSize, mipLevels, format, pixels, m_Impl->LastError);
    }

    bool DX11Renderer::IsCubemapValid(CubemapResourceId cubemap) const
    {
        return m_Impl && m_Impl->Resources.IsCubemapValid(cubemap);
    }

    void DX11Renderer::DestroyCubemap(CubemapResourceId cubemap)
    {
        if (m_Impl) m_Impl->Resources.DestroyCubemap(cubemap);
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
        m_Impl->DrawSkybox(scene.skybox);
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
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(gpuMesh.Format, materialDesc.alphaMode, meshView,
            wireframe));
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
        if (m_Impl->Overlay)
        {
            m_Impl->RenderFinalOutput(m_Impl->DisplayRenderTargetView.Get(), m_Impl->Width, m_Impl->Height);
            m_Impl->RenderOverlay();
        }
        else
        {
            m_Impl->RenderFinalOutput(m_Impl->RenderTargetView.Get(), m_Impl->BackBufferWidth, m_Impl->BackBufferHeight);
        }
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

    void DX11Renderer::SetSceneSize(uint32_t width, uint32_t height)
    {
        if (!m_Impl)
        {
            return;
        }
        m_Impl->RequestedSceneWidth = width && height ? width : 0;
        m_Impl->RequestedSceneHeight = width && height ? height : 0;
    }

    void DX11Renderer::SetOverlay(std::function<void()> overlay)
    {
        if (m_Impl)
        {
            m_Impl->Overlay = std::move(overlay);
        }
    }

    void* DX11Renderer::GetNativeDevice() const
    {
        return m_Impl ? m_Impl->Device.Get() : nullptr;
    }

    void* DX11Renderer::GetNativeContext() const
    {
        return m_Impl ? m_Impl->Context.Get() : nullptr;
    }

    void* DX11Renderer::GetSceneTexture() const
    {
        return m_Impl ? m_Impl->DisplayShaderResourceView.Get() : nullptr;
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
