#pragma once

#include "Math/MathUtility.h"
#include "Math/Vector2.h"
#include "Scene/Component/Component.h"

namespace Canape
{
    // Blows along the Transform's forward; its position and scale are ignored.
    class WindComponent final: public Component
    {
        RTTR_ENABLE(Component)

    private:
        float speed = 4.3f;

    public:
        // World XZ, meters per second: the horizontal part of the wind.
        Vector2 GetVelocity() const;

        float GetSpeed() const
        {
            return speed;
        }
        void SetSpeed(float metersPerSecond)
        {
            speed = Math::Max(metersPerSecond, 0.0f);
        }
    };
}
