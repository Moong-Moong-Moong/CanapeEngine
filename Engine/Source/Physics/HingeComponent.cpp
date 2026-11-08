#include "Physics/HingeComponent.h"

#include "Physics/PhysicsScene.h"

namespace Canape
{
    bool HingeComponent::HasHinge() const
    {
        if(const auto* physics = FindPhysics(*this))
        {
            return physics->HasHinge(*this);
        }

        return false;
    }

    HingeIssue HingeComponent::GetIssue() const
    {
        if(const auto* physics = FindPhysics(*this))
        {
            return physics->GetIssue(*this);
        }

        return HingeIssue::None;
    }

    void HingeComponent::Release()
    {
        if(auto* physics = FindPhysics(*this))
        {
            physics->ReleaseHinge(*this);
        }
    }

    bool HingeComponent::IsHeld() const
    {
        if(const auto* physics = FindPhysics(*this))
        {
            return physics->IsHingeHeld(*this);
        }

        return false;
    }

    float HingeComponent::GetAngle() const
    {
        if(const auto* physics = FindPhysics(*this))
        {
            return physics->GetHingeAngle(*this);
        }

        return 0.0f;
    }

    Vector3 HingeComponent::GetWorldAnchor() const
    {
        if(const auto* physics = FindPhysics(*this))
        {
            return physics->GetHingeAnchor(*this);
        }

        return Vector3::Zero;
    }
}
