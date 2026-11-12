#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DX11Renderer/Frame/DX11ScenePreparation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "CNDebug/DebugLog.h"

namespace Canape::ScenePreparation
{
    namespace SC = ShaderConstants;
    SC::LightConstants BuildLightConstants(std::span<const RenderLight> lights,
        uint32_t* shadowDirectionalIndex)
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

    bool BuildFrustumBoundingSphere(const SC::ViewConstants& camera, float shadowDistance, Vector3& center,
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

    bool BuildDirectionalShadowView(const SC::ViewConstants& camera, uint32_t mapResolution, const Vector3& direction,
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
        if (!BuildFrustumBoundingSphere(camera, settings.shadowDistance, center, radius, fittedRadius)
            || mapResolution == 0) return false;
        // A snapped center can move by half a texel on each light-space axis.
        // Expand XY only when the sphere's existing padding is insufficient.
        const double resolution = static_cast<double>(mapResolution);
        const double requiredRadius = resolution > 1
            ? fittedRadius / (1.0 - 1.0 / resolution) : fittedRadius;
        const float projectionRadius = requiredRadius > radius
            ? std::nextafter(static_cast<float>(requiredRadius), std::numeric_limits<float>::infinity())
            : radius;
        const float projectionWidth = 2.0f * projectionRadius;
        const float projectionHeight = 2.0f * projectionRadius;
        const float texelSizeX = projectionWidth / mapResolution;
        const float texelSizeY = projectionHeight / mapResolution;
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
        const float snappedX = mapResolution > 1
            ? std::round(originalX / texelSizeX) * texelSizeX : originalX;
        const float snappedY = mapResolution > 1
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

    SC::ViewConstants BuildViewConstants(const float* view, const float* projection)
    {
        SC::ViewConstants data{};
        using namespace DirectX;

        const XMMATRIX viewMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(view));
        const XMMATRIX projectionMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(projection));
        XMStoreFloat4x4(&data.View, viewMatrix);
        XMStoreFloat4x4(&data.Projection, projectionMatrix);
        XMStoreFloat4x4(&data.ViewProjection, viewMatrix * projectionMatrix);

        // Preserve the legacy SetCamera + DrawMesh path, which has no position argument.
        data.CameraPosition = {};
        XMVECTOR determinant;
        const XMMATRIX inverseView = XMMatrixInverse(&determinant, viewMatrix);
        const float det = XMVectorGetX(determinant);
        if (std::isfinite(det) && det != 0.0f && !XMMatrixIsInfinite(inverseView) && !XMMatrixIsNaN(inverseView))
            data.CameraPosition = Vector3::FromXM(inverseView.r[3]);
        return data;
    }

    SC::SceneConstants BuildSceneConstants(const RenderScene& scene)
    {
        SC::SceneConstants data{};
        data.AmbientColor = scene.ambientColor;
        data.AmbientIntensity = scene.ambientIntensity;
        data.FogColor = scene.fog.color;
        data.FogDensity = scene.fog.density;
        data.FogStartDistance = scene.fog.startDistance;
        data.FogEndDistance = scene.fog.endDistance;
        data.FogMode = static_cast<uint32_t>(scene.fog.mode);
        return data;
    }

    SC::ObjectConstants BuildObjectConstants(const float* world)
    {
        using namespace DirectX;
        const XMMATRIX worldMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(world));
        SC::ObjectConstants data{};
        XMStoreFloat4x4(&data.World, worldMatrix);
        XMVECTOR determinant;
        const XMMATRIX inverseWorld = XMMatrixInverse(&determinant, worldMatrix);
        const float det = XMVectorGetX(determinant);
        // Normal inverse-transpose is mathematical, unrelated to row-major upload packing.
        if (std::isfinite(det) && det != 0.0f && !XMMatrixIsInfinite(inverseWorld) && !XMMatrixIsNaN(inverseWorld))
            XMStoreFloat4x4(&data.WorldInverseTranspose, XMMatrixTranspose(inverseWorld));
        return data;
    }

    PreparedMaterial PrepareMaterial(const DX11ResourceCache& resources, MaterialResourceId material)
    {
        const DX11MaterialResource* resource = resources.GetMaterial(material);
        const MaterialDesc desc = resource ? resource->Desc : MaterialDesc{};
        ID3D11ShaderResourceView* textureView = resources.GetWhiteTextureView();
        ID3D11ShaderResourceView* ormView = nullptr;
        ID3D11ShaderResourceView* normalView = nullptr;
        ID3D11ShaderResourceView* occlusionView = nullptr;
        uint32_t hasTexture = 0;
        uint32_t hasORMTexture = 0;
        uint32_t hasNormalTexture = 0;
        uint32_t hasOcclusionTexture = 0;
        if (resource)
        {
            if (const auto* texture = resources.GetTexture(resource->BaseColorTexture))
            {
                textureView = texture->ShaderResourceView.Get();
#if CANAPE_RENDER_DEBUG
                hasTexture = 1;
#endif
            }
            if (const auto* texture = resources.GetTexture(resource->ORMTexture))
            {
                ormView = texture->ShaderResourceView.Get();
                hasORMTexture = 1;
            }
            if (const auto* texture = resources.GetTexture(resource->NormalTexture))
            {
                normalView = texture->ShaderResourceView.Get();
                hasNormalTexture = 1;
            }
            if (const auto* texture = resources.GetTexture(resource->OcclusionTexture))
            {
                occlusionView = texture->ShaderResourceView.Get();
                hasOcclusionTexture = 1;
            }
        }
        const SC::MaterialConstants constants{ desc.baseColor, static_cast<uint32_t>(desc.alphaMode),
            desc.alphaCutoff, hasTexture, hasNormalTexture, desc.metallic, desc.roughness,
            hasORMTexture, hasOcclusionTexture };
        return { desc, constants, textureView, ormView, normalView, occlusionView };
    }

    PreparedLighting PrepareLights(std::span<const RenderLight> lights, const DirectionalShadowSettings& settings)
    {
        PreparedLighting result;
        result.Lights = BuildLightConstants(lights, &result.ShadowDirectionalIndex);
        result.Shadows.PcfEnabled = settings.pcf ? 1u : 0u;
        result.Pcss.TanLightAngularRadius = std::tan(settings.lightAngularRadius);
        result.Pcss.BlockerSearchRadiusWorld = settings.blockerSearchRadiusWorld;
        result.Pcss.MinFilterRadiusWorld = settings.minFilterRadiusWorld;
        result.Pcss.MaxFilterRadiusWorld = settings.maxFilterRadiusWorld;
        result.Pcss.PcfSampleCount = settings.pcfSampleCount;
        result.Pcss.PcfTapSpacingScale = settings.pcfTapSpacingScale;
        result.Pcss.BlockerSampleCount = settings.blockerSampleCount;
        result.Pcss.FilterMode = !settings.pcf ? 0u : !settings.pcss ? 1u
            : settings.pcfSampleCount == 25 && settings.legacyPcf25 ? 3u : 2u;
        return result;
    }

    void PrepareShadow(PreparedLighting& data, const SC::ViewConstants& view,
        const DirectionalShadowSettings& settings, uint32_t resolution)
    {
        data.ShadowActive = BuildDirectionalShadowView(view, resolution,
            data.Lights.DirectionalLights[data.ShadowDirectionalIndex].Direction,
            settings, data.Shadows.Views[0], data.Pcss);
        if (data.ShadowActive)
        {
            data.Shadows.ViewCount = 1;
            auto& light = data.Lights.DirectionalLights[data.ShadowDirectionalIndex];
            light.ShadowViewOffset = 0;
            light.ShadowViewCount = 1;
        }
    }

    bool PrepareMeshDraw(const DX11ResourceCache& resources, MeshResourceId mesh, const float* world,
        uint32_t submeshIndex, MaterialResourceId material, PreparedMeshDraw& result, std::string& error)
    {
        const auto* resource = resources.GetMesh(mesh);
        if (!resource) return false;
        if (submeshIndex >= resource->SubMeshes.size())
        {
            error = "DrawMesh received an invalid submeshIndex";
            CN_LOG_RENDERER("Draw", "잘못된 서브메시 인덱스: ", submeshIndex);
            return false;
        }
        result.Mesh = resource;
        result.SubmeshIndex = submeshIndex;
        result.Object = BuildObjectConstants(world);
        result.Material = PrepareMaterial(resources, material);
        return true;
    }

    PreparedScene PrepareScene(const RenderScene& scene, const DX11ResourceCache& resources, std::string& error)
    {
        PreparedScene result;
        result.View = BuildViewConstants(scene.camera.view.Data(), scene.camera.projection.Data());
        result.View.CameraPosition = scene.camera.cameraPosition;
        result.Scene = BuildSceneConstants(scene);
        result.Draws.reserve(scene.items.size());
        for (const auto& item : scene.items)
        {
            PreparedMeshDraw draw;
            if (!PrepareMeshDraw(resources, item.mesh, item.world.Data(), item.submeshIndex,
                item.material, draw, error)) continue;
            const size_t index = result.Draws.size();
            const AlphaMode alpha = draw.Material.Description.alphaMode;
            if (item.castShadow && alpha != AlphaMode::Transparent) result.ShadowCasters.push_back(index);
            switch (alpha)
            {
            case AlphaMode::Masked: result.Masked.push_back(index); break;
            case AlphaMode::Transparent:
                draw.Depth = scene.camera.view.TransformPoint(item.world.GetTranslation()).Z;
                if (!std::isfinite(draw.Depth)) draw.Depth = 0.0f;
                result.Transparent.push_back(index);
                break;
            default: result.Opaque.push_back(index); break;
            }
            result.Draws.push_back(draw);
        }
        std::stable_sort(result.Transparent.begin(), result.Transparent.end(), [&](size_t a, size_t b)
        {
            return result.Draws[a].Depth > result.Draws[b].Depth;
        });
        result.Skybox = PrepareSkybox(scene.skybox, result.View, resources);
        return result;
    }

    PreparedSkybox PrepareSkybox(const RenderSkybox& skybox, const SC::ViewConstants& camera,
        const DX11ResourceCache& resources)
    {
        const auto* cubemap = resources.GetCubemap(skybox.cubemap);
        if (!cubemap || !cubemap->ShaderResourceView) return {};
        PreparedSkybox result;
        using namespace DirectX;
        const XMMATRIX projection = XMLoadFloat4x4(&camera.Projection);
        const XMMATRIX view = XMLoadFloat4x4(&camera.View);
        XMVECTOR projectionDeterminant;
        XMVECTOR viewDeterminant;
        const XMMATRIX inverseProjection = XMMatrixInverse(&projectionDeterminant, projection);
        const XMMATRIX inverseView = XMMatrixInverse(&viewDeterminant, view);
        const float projectionDet = XMVectorGetX(projectionDeterminant);
        const float viewDet = XMVectorGetX(viewDeterminant);
        if (!std::isfinite(projectionDet) || projectionDet == 0.0f
            || !std::isfinite(viewDet) || viewDet == 0.0f
            || XMMatrixIsNaN(inverseProjection) || XMMatrixIsInfinite(inverseProjection)
            || XMMatrixIsNaN(inverseView) || XMMatrixIsInfinite(inverseView)) return {};

        auto& constants = result.Constants;
        // Existing shaders use row-major mul(vector, matrix); upload without a transpose.
        XMStoreFloat4x4(&constants.InverseProjection, inverseProjection);
        XMStoreFloat4x4(&constants.InverseView, inverseView);
        constants.Brightness = std::isfinite(skybox.brightness) ? std::max(0.0f, skybox.brightness) : 1.0f;
        const float rotation = std::isfinite(skybox.rotationRadians) ? skybox.rotationRadians : 0.0f;
        constants.RotationCos = std::cos(rotation);
        constants.RotationSin = std::sin(rotation);
        constants.MipCount = static_cast<float>(cubemap->MipLevels);
        result.Texture = cubemap->ShaderResourceView.Get();
        return result;
    }

    PreparedFinalOutput PrepareFinalOutput(const DX11PipelineState& pipelines,
        ID3D11ShaderResourceView* sceneColor, ID3D11ShaderResourceView* depth,
        ID3D11ShaderResourceView* shadow, bool shadowActive, const RendererDebugSettings* debug)
    {
        PreparedFinalOutput result;
#if CANAPE_RENDER_DEBUG
        const bool cameraDepth = debug && debug->enabled && debug->viewMode == DebugViewMode::CameraDepth;
        const bool shadowDepth = debug && debug->enabled && debug->viewMode == DebugViewMode::ShadowDepth && shadowActive;
        result.DisplayDepth = cameraDepth || shadowDepth;
        result.Texture = shadowDepth ? shadow : cameraDepth ? depth : sceneColor;
        if (result.DisplayDepth)
            result.DepthDisplay = { shadowDepth ? 0.0f : debug->depthMin, shadowDepth ? 1.0f : debug->depthMax };
#else
        (void)depth; (void)shadow; (void)shadowActive; (void)debug;
        result.Texture = sceneColor;
#endif
        result.Pipeline = pipelines.ResolveFinalOutput(result.DisplayDepth);
        return result;
    }
}
