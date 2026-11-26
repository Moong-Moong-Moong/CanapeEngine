#pragma once

#include "DX11Renderer/Scene/MeshItem.h"
#include "DX11Renderer/Data/PostProcessSettings.h"
#include "Math/Matrix4.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace Canape
{
    struct RenderView
    {
        Matrix4 view{};
        Matrix4 projection{};
        Vector3 cameraPosition{};
        PostProcessSettings postProcess{}; // Future per-view input; backend defaults are used for now.
    };

    enum class LightType : uint8_t { Directional, Point, Spot };

    struct RenderLight
    {
        uint64_t id = 0; // Stable within the scene. Zero is invalid.
        LightType type = LightType::Point;
        Vector3 color{ 1.0f, 1.0f, 1.0f }; // Linear RGB.
        float intensity = 1.0f;

        Vector3 position{}; // Point, Spot: world position.
        Vector3 direction{ 0.0f, -1.0f, 0.0f }; // Directional, Spot: world travel direction.
        float range = 10.0f; // Point, Spot: world units.
        float innerAngleRadians = 0.35f; // Spot: half-angle from the center axis.
        float outerAngleRadians = 0.52f; // Spot: half-angle from the center axis.

        bool castShadow = false;
    };

    enum class FogMode : uint8_t { Disabled, Linear, Exp, Exp2, Height };

    struct RenderFog
    {
        FogMode mode = FogMode::Disabled;
        Vector3 color{ 0.5f, 0.5f, 0.5f }; // Linear RGB.
        float startDistance = 0.0f; // Linear.
        float endDistance = 100.0f; // Linear.
        float density = 0.01f; // Exp, Exp2, Height (at baseHeight).
        float baseHeight = 0.0f; // Height: reference world-space Y.
        float heightFalloff = 0.1f; // Height: exponential density falloff per world unit.
    };

    struct RenderSkybox
    {
        CubemapResourceId cubemap{}; // Invalid/missing resources disable skybox rendering.
        float brightness = 1.0f;
        float rotationRadians = 0.0f; // Rotation about the world Up axis.
    };

    // Editor visualization requests; never enter mesh, mirror, shadow or picking lists.
    struct EditorWireSphere
    {
        Vector3 center{};
        float radius = 1.0f; // World units, independent of the owner's scale.
        Vector4 color{ 1.0f, 0.7f, 0.1f, 1.0f };
    };

    struct RenderScene
    {
        RenderView camera;
        std::vector<MeshItem> items;
        std::vector<SkinnedMeshItem> skinnedItems;
        std::vector<Matrix4> bonePalettes;

        Vector3 ambientColor{ 1.0f, 1.0f, 1.0f }; // Linear RGB.
        float ambientIntensity = 0.0f;
        RenderFog fog{};
        RenderSkybox skybox{};
        std::optional<float> mirrorPlaneHeight; // World-space Y.

        std::vector<RenderLight> lights;
        std::vector<EditorWireSphere> editorWireSpheres;
    };
}
