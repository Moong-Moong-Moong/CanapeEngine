#include "Simulation/WindComponent.h"

#include "Math/Vector3.h"
#include "Scene/Component/TransformComponent.h"

namespace Canape
{
    Vector2 WindComponent::GetVelocity() const
    {
        const auto forward = GetTransform()->GetForward();

        return forward.XZ() * speed;
    }
}
