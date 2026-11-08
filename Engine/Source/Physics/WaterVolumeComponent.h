#pragma once

#include "Math/Vector3.h"
#include "Scene/Component/Component.h"

namespace Canape
{
    // A box of water: size times the Transform scale, centered on the Transform position, axis aligned.
    // It is not a body; the scene's physics applies it to the dynamic bodies inside it every fixed step.
    // Sleeping bodies inside it are woken once when its effective box, density or drags change or it becomes enabled.
    class WaterVolumeComponent final: public Component {
        RTTR_ENABLE(Component)

    private:
        // Meters before the Transform scale.
        Vector3 size = Vector3::One;
        // kg/m^3
        float density = 1000.0f;
        float linearDrag = 0.05f;
        float angularDrag = 0.01f;

    public:
        WaterVolumeComponent() = default;

        const Vector3& GetSize() const { return size; }
        void SetSize(const Vector3& boxSize) { size = boxSize; }
        float GetDensity() const { return density; }
        void SetDensity(float waterDensity) { density = waterDensity; }
        float GetLinearDrag() const { return linearDrag; }
        void SetLinearDrag(float drag) { linearDrag = drag; }
        float GetAngularDrag() const { return angularDrag; }
        void SetAngularDrag(float drag) { angularDrag = drag; }
    };
}
