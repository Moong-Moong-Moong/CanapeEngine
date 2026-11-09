#include "Physics/RigidBodyComponent.h"

#include "Physics/PhysicsScene.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

namespace Canape
{
	bool RigidBodyComponent::HasBody() const
	{
		if (const auto* physics = FindPhysics(*this))
        {
            return physics->HasBody(*this);
        }

        return false;
	}

	RigidBodyIssue RigidBodyComponent::GetIssue() const
	{
        if (const auto* physics = FindPhysics(*this))
        {
            return physics->GetIssue(*this);
        }

        return RigidBodyIssue::None;
	}

	bool RigidBodyComponent::IsAwake() const
	{
		const auto* physics = FindPhysics(*this);
		return physics && physics->IsAwake(*this);
	}

	void RigidBodyComponent::AddImpulse(const Vector3& impulse)
	{
		if (auto* physics = FindPhysics(*this))
		{
			physics->AddImpulse(*this, impulse);
		}
	}

	void RigidBodyComponent::SetKinematicTarget(const PhysicsPose& target)
	{
		if (auto* physics = FindPhysics(*this))
		{
			physics->SetKinematicTarget(*this, target);
		}
	}

	Vector3 RigidBodyComponent::GetVelocityAt(const Vector3& worldPoint) const
	{
		if (const auto* physics = FindPhysics(*this))
		{
			return physics->GetVelocityAt(*this, worldPoint);
		}

		return Vector3::Zero;
	}
}
