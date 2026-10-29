#pragma once

#include "DX11Renderer/Scene/RenderItem.h"
#include "Math/Matrix4.h"
#include "Math/Vector3.h"

#include <cstdint>
#include <vector>

namespace Canape
{
    struct RenderView
    {
        Matrix4 view{};
        Matrix4 projection{};
        Vector3 cameraPosition{};
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

    enum class FogMode : uint8_t { Disabled, Linear, Exp, Exp2 };

    struct RenderFog
    {
        FogMode mode = FogMode::Disabled;
        Vector3 color{ 0.5f, 0.5f, 0.5f }; // Linear RGB.
        float startDistance = 0.0f; // Linear.
        float endDistance = 100.0f; // Linear.
        float density = 0.01f; // Exp, Exp2.
    };

    struct RenderSkybox
    {
        // TextureResourceId cubemap{}; // TODO: enable when cube texture resources are supported.
        float brightness = 1.0f;
        float rotationRadians = 0.0f; // Rotation about the world Up axis.
    };

    struct RenderScene
    {
        RenderView camera;
        std::vector<RenderItem> items;

        Vector3 ambientColor{ 1.0f, 1.0f, 1.0f }; // Linear RGB.
        float ambientIntensity = 0.1f;
        RenderFog fog{};
        RenderSkybox skybox{};

        std::vector<RenderLight> lights;
    };
}
