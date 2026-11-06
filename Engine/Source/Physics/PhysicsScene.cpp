#include "Physics/PhysicsScene.h"

#include "CNDebug/DebugLog.h"
#include "JoltPhysics/JoltRuntime.h"
#include "JoltPhysics/JoltWorld.h"
#include "Physics/ColliderComponent.h"
#include "Scene/GameObject/GameObject.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <span>

namespace Canape
{
	namespace
	{
		PhysicsPose PoseOf(const TransformComponent& transform)
		{
			return {
				.position = transform.GetPosition(),
				.rotation = transform.GetRotation().Normalized()
			};
		}

		// The nearest body at or above the collider.
		const RigidBodyComponent* FindOwningBody(const ColliderComponent& collider)
		{
			for (const auto* node = collider.GetTransform(); node; node = node->GetParent())
			{
				if (const auto* body = node->GetOwnerObject()->GetComponent<RigidBodyComponent>())
				{
					return body;
				}
			}
			return nullptr;
		}

        std::uint32_t CountAuthoringIssues(std::span<RigidBodyComponent* const> bodies)
        {
            const auto count = static_cast<std::uint32_t>(std::ranges::count_if(
                bodies,
                [](const RigidBodyComponent* body)
                {
                    return IsAuthoringIssue(body->GetIssue());
                }
            ));

            return count;
        }
	}

	PhysicsScene::PhysicsScene(JoltRuntime& runtime, const PhysicsWorldDesc& desc)
		: m_World(std::make_unique<JoltWorld>(runtime, desc))
	{
		if (!HasWorld())
		{
			CN_LOG_RUNTIME("Physics", "the physics world was not created");
		}
	}

	PhysicsScene::PhysicsScene() = default;

	PhysicsScene::~PhysicsScene() = default;

	bool PhysicsScene::HasBody(const RigidBodyComponent& body) const
	{
        if (const auto* state = FindState(body))
        {
            return state->IsBuilt();
        }

        return false;
	}

	RigidBodyIssue PhysicsScene::GetIssue(const RigidBodyComponent& body) const
	{
        if (const auto* state = FindState(body))
        {
            return state->GetIssue();
        }

		return RigidBodyIssue::None;
	}

	void PhysicsScene::AddImpulse(const RigidBodyComponent& body, const Vector3& impulse)
	{
		if (!HasWorld()) return;

		RigidBodyState& state = m_Bodies[&body];
		if (state.IsBuilt())
		{
			m_World->AddImpulse(state.GetId(), impulse);
		}
		else
		{
			state.QueueImpulse(impulse);
		}
	}

	void PhysicsScene::SetKinematicTarget(const RigidBodyComponent& body, const PhysicsPose& target)
	{
		if (!HasWorld()) return;

        RigidBodyState& state = m_Bodies[&body];
		state.SetKinematicTarget(PhysicsPose{
            .position = target.position,
            .rotation = target.rotation.Normalized()
        });
	}

	void PhysicsScene::DestroyWorld()
	{
		m_World.reset();
		m_Bodies.clear();

		m_StepCount = 0;
		m_LastStepMilliseconds = 0.0f;
	}

	const RigidBodyState* PhysicsScene::FindState(const RigidBodyComponent& body) const
	{
		const auto it = m_Bodies.find(&body);
        if (it != m_Bodies.end())
        {
            return &it->second;
        }

        return nullptr;
	}

	const RigidBodyComponent* PhysicsScene::FindBody(PhysicsBodyId id) const
	{
		const auto it = std::ranges::find_if(
            m_Bodies,
            [id](const auto& entry)
            {
                return entry.second.GetId() == id;
            }
        );

        return it != m_Bodies.end() ? it->first : nullptr;
	}

	void PhysicsScene::ReleaseBodies(std::span<Component* const> removed)
	{
		for (const auto* component : removed)
		{
			if (const auto* body = dynamic_cast<const RigidBodyComponent*>(component))
			{
				if (auto* state = FindState(*body))
				{
					ReleaseBody(*state);
					m_Bodies.erase(body);
				}
			}
			else if (const auto* collider = dynamic_cast<const ColliderComponent*>(component))
			{
				// Released before the collider's address can be reused by another; the next sync builds the body again.
				const RigidBodyComponent* owner = FindOwningBody(*collider);
				RigidBodyState* state = owner ? FindState(*owner) : nullptr;
				if (state && state->IsBuiltFrom(*collider))
				{
					ReleaseBody(*state);
				}
			}
		}
	}

	void PhysicsScene::SyncBodies(std::span<RigidBodyComponent* const> bodies)
	{
        if (!HasWorld()) return;

		for (const auto* body : bodies)
		{
			RigidBodyState& state = m_Bodies[body];
			if (IsPhysicsRunnable(*body))
			{
				SyncBody(*body, state);
			}
			else
			{
				ReleaseBody(state);
			}
			ApplyQueuedImpulses(state);
		}
		m_AuthoringIssueCount = CountAuthoringIssues(bodies);
	}

	void PhysicsScene::SyncBody(const RigidBodyComponent& body, RigidBodyState& state)
	{
		m_Shape.Gather(body);

		const PhysicsBodySettings& settings = body.GetSettings();
		switch (state.Match(settings, m_Shape))
		{
		case ShapeMatch::Same:
			return;
		case ShapeMatch::FirstMutation:
			CN_LOG_RUNTIME("Physics", "'", body.GetOwnerObject()->GetName(),
				"': re-parenting, scale or collider size changes after creation are ignored; toggle the RigidBody to rebuild");
			return;
		case ShapeMatch::Changed:
			break;
		}
		ReleaseBody(state);

		if (const RigidBodyIssue issue = m_Shape.GetIssue(); issue != RigidBodyIssue::None)
		{
			FailBody(body, state, issue);
			return;
		}

		const PhysicsBodyId created = m_World->CreateBody(PhysicsBodyDesc{
			.parts = m_Shape.GetParts(),
			.pose = PoseOf(*body.GetTransform()),
			.settings = settings
		});
		if (!created.IsValid())
		{
			FailBody(body, state, RigidBodyIssue::BackendRefused);
			return;
		}
		state.Adopt(created, settings, m_Shape);

		if (m_Shape.HasNonUniformAncestors())
		{
			CN_LOG_RUNTIME("Physics", "'", body.GetOwnerObject()->GetName(), "': non-uniform ancestor scale, the shape is approximate");
		}
		if (m_Shape.HasNonUniformSphere())
		{
			CN_LOG_RUNTIME("Physics", "'", body.GetOwnerObject()->GetName(), "': non-uniform sphere scale, the largest axis is used");
		}
	}

	void PhysicsScene::FailBody(const RigidBodyComponent& body, RigidBodyState& state, RigidBodyIssue issue)
	{
		state.Fail(issue, body.GetSettings(), m_Shape);
		CN_LOG_RUNTIME("Physics", "'", body.GetOwnerObject()->GetName(), "' has no body: ", IssueText(issue));
	}

	void PhysicsScene::ReleaseBody(RigidBodyState& state)
	{
		if (state.IsBuilt())
		{
			m_World->DestroyBody(state.GetId());
			state.ClearKinematicTarget();
		}
		state.Clear();
	}

	// The queue reaches a built body and is dropped otherwise.
	void PhysicsScene::ApplyQueuedImpulses(RigidBodyState& state)
	{
		if (state.IsBuilt())
		{
			for (const Vector3& impulse : state.GetQueuedImpulses())
			{
				m_World->AddImpulse(state.GetId(), impulse);
			}
		}
		state.ClearQueuedImpulses();
	}

	// Every kinematic body with a target is sent it every step, so one that has arrived reads a zero velocity.
	void PhysicsScene::MoveKinematicBodies(float fixedDeltaTime, std::span<RigidBodyComponent* const> bodies)
	{
        using enum PhysicsMotion;

		for (const RigidBodyComponent* body : bodies)
		{
			const RigidBodyState& state = *FindState(*body);
            if (!state.IsBuilt() || m_World->GetMotion(state.GetId()) != Kinematic) continue;

            if (auto* target = state.GetKinematicTarget())
            {
                m_World->MoveKinematic(state.GetId(), *target, fixedDeltaTime);
            }
		}
	}

	PhysicsStepError PhysicsScene::Step(float fixedDeltaTime, std::span<RigidBodyComponent* const> bodies)
	{
		if (!HasWorld()) return PhysicsStepError::None;

		MoveKinematicBodies(fixedDeltaTime, bodies);

		const auto stepStart = std::chrono::steady_clock::now();
		const PhysicsStepError error = m_World->Step(fixedDeltaTime);

		m_LastStepMilliseconds = std::chrono::duration<float, std::milli>(
			std::chrono::steady_clock::now() - stepStart
		).count();
		++m_StepCount;

		if (StepFailed(error) && error != m_LastStepError)
		{
			CN_LOG_RUNTIME("Physics", "Physics step failed, error flags: ", static_cast<int>(error));
			assert(false && "Physics step failed");
		}
		m_LastStepError = error;

		for (const RigidBodyComponent* body : bodies)
		{
			const RigidBodyState& state = *FindState(*body);
			if (state.IsBuilt())
			{
				WriteBack(*body, state.GetId());
			}
		}
		return error;
	}

	void PhysicsScene::WriteBack(const RigidBodyComponent& body, PhysicsBodyId id) const
	{
		if (m_World->GetMotion(id) == PhysicsMotion::Static) return;

		const PhysicsPose pose = m_World->GetPose(id);
		TransformComponent* transform = body.GetTransform();
		if (const TransformComponent* parent = transform->GetParent())
		{
            transform->SetLocalPosition(parent->GetWorldToLocalMatrix().TransformPoint(pose.position));
            transform->SetLocalRotation((parent->GetRotation().Inverse() * pose.rotation).Normalized());
        }
        else
        {
            transform->SetLocalPosition(pose.position);
            transform->SetLocalRotation(pose.rotation);
        }
	}

	RigidBodies PhysicsScene::QueryBox(const Vector3& center, const Vector3& halfExtent) const
	{
		RigidBodies found;
		if (!HasWorld()) return found;

		for (const PhysicsBodyId id : m_World->QueryBox(center, halfExtent))
		{
			if (const auto* body = FindBody(id))
			{
				found.push_back(body);
			}
		}
		return found;
	}

	PhysicsSceneStats PhysicsScene::GetStats() const
	{
		if (!HasWorld()) return {};

		const PhysicsWorldStats world = m_World->GetStats();
		return {
			world.bodyCount,
			world.activeBodies,
			m_StepCount,
			m_LastStepMilliseconds
		};
	}
}
