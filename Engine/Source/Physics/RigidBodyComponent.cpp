#include "Physics/RigidBodyComponent.h"

#include "Physics/PhysicsScene.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

namespace Canape
{
	bool RigidBodyComponent::HasBody() const
	{
		if (const auto* physics = FindPhysics())
        {
            return physics->HasBody(*this);
        }

        return false;
	}

	RigidBodyIssue RigidBodyComponent::GetIssue() const
	{
        if (const auto* physics = FindPhysics())
        {
            return physics->GetIssue(*this);
        }

        return RigidBodyIssue::None;
	}

	void RigidBodyComponent::AddImpulse(const Vector3& impulse)
	{
		if (auto* physics = FindPhysics())
		{
			physics->AddImpulse(*this, impulse);
		}
	}

	PhysicsScene* RigidBodyComponent::FindPhysics() const
	{
		auto* owner = GetOwnerObject();
        if(owner == nullptr) return nullptr;

        if(auto* scene = owner->GetScene())
        {
            return &scene->GetPhysics();
        }

        return nullptr;
	}
}
