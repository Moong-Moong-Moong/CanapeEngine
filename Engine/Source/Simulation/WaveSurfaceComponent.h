#pragma once

#include <cstdint>

#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Scene/Component/Component.h"

namespace Canape
{
    // Centered on the Transform position; its rotation and scale are ignored.
    class WaveSurfaceComponent final: public Component {
        RTTR_ENABLE(Component)

    private:
        Vector2 areaSize { 160.0f, 160.0f };
        // Grid points per side of the drawn surface.
        std::uint32_t resolution = 256;
        float swellAmplitude = 0.15f;
        float swellWavelength = 12.0f;
        float swellSpeed = 2.5f;
        // Linear RGB.
        Vector3 deepColor { 0.04f, 0.2f, 0.3f };
        Vector3 shallowColor { 0.25f, 0.55f, 0.6f };

    public:
        WaveSurfaceComponent() = default;

        const Vector2& GetAreaSize() const { return areaSize; }
        void SetAreaSize(const Vector2& size) { areaSize = size; }
        std::uint32_t GetResolution() const { return resolution; }
        void SetResolution(std::uint32_t pointsPerSide) { resolution = pointsPerSide; }
        float GetSwellAmplitude() const { return swellAmplitude; }
        void SetSwellAmplitude(float meters) { swellAmplitude = meters; }
        float GetSwellWavelength() const { return swellWavelength; }
        void SetSwellWavelength(float meters) { swellWavelength = meters; }
        float GetSwellSpeed() const { return swellSpeed; }
        void SetSwellSpeed(float metersPerSecond) { swellSpeed = metersPerSecond; }
        const Vector3& GetDeepColor() const { return deepColor; }
        void SetDeepColor(const Vector3& color) { deepColor = color; }
        const Vector3& GetShallowColor() const { return shallowColor; }
        void SetShallowColor(const Vector3& color) { shallowColor = color; }
    };
}
