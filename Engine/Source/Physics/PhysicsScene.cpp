#include "Physics/PhysicsScene.h"

#include "CNDebug/DebugLog.h"
#include "JoltPhysics/JoltRuntime.h"
#include "JoltPhysics/JoltWorld.h"
#include "Physics/ColliderComponent.h"
#include "Physics/WaterVolumeComponent.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
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

		// The nearest runnable body at or above the collider: the one that shapes itself from it.
		const RigidBodyComponent* FindOwningBody(const ColliderComponent& collider)
		{
			for (const auto* node = collider.GetTransform(); node; node = node->GetParent())
			{
				const auto* body = node->GetOwnerObject()->GetComponent<RigidBodyComponent>();
				if (body && IsPhysicsRunnable(*body))
				{
					return body;
				}
			}
			return nullptr;
		}

		bool SameWater(const PhysicsWaterDesc& a, const PhysicsWaterDesc& b)
		{
			const auto& [center, halfExtent, density, linearDrag, angularDrag] = a;
			return SameBits(center, b.center)
				&& SameBits(halfExtent, b.halfExtent)
				&& SameBits(density, b.density)
				&& SameBits(linearDrag, b.linearDrag)
				&& SameBits(angularDrag, b.angularDrag);
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

        std::uint32_t CountAuthoringIssues(std::span<HingeComponent* const> hinges)
        {
            const auto count = static_cast<std::uint32_t>(std::ranges::count_if(
                hinges,
                [](const HingeComponent* hinge)
                {
                    return IsAuthoringIssue(hinge->GetIssue());
                }
            ));

            return count;
        }
	}

	PhysicsScene* FindPhysics(const Component& component)
	{
		const GameObject* owner = component.GetOwnerObject();
		if (owner == nullptr) return nullptr;

		if (Scene* scene = owner->GetScene())
		{
			return &scene->GetPhysics();
		}

		return nullptr;
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

	bool PhysicsScene::IsAwake(const RigidBodyComponent& body) const
	{
		const auto* state = FindState(body);
		return state && state->IsBuilt() && m_World->IsAwake(state->GetId());
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

	Vector3 PhysicsScene::GetVelocityAt(const RigidBodyComponent& body, const Vector3& worldPoint) const
	{
		const auto* state = FindState(body);
		return state && state->IsBuilt() ? m_World->GetPointVelocity(state->GetId(), worldPoint) : Vector3::Zero;
	}

	bool PhysicsScene::HasHinge(const HingeComponent& hinge) const
	{
		if (const auto* state = FindState(hinge))
		{
			return state->IsBuilt();
		}

		return false;
	}

	HingeIssue PhysicsScene::GetIssue(const HingeComponent& hinge) const
	{
		if (const auto* state = FindState(hinge))
		{
			return state->GetIssue();
		}

		return HingeIssue::None;
	}

	bool PhysicsScene::IsHingeHeld(const HingeComponent& hinge) const
	{
		const auto* state = FindState(hinge);
		return state && state->IsBuilt() && !state->IsReleased() && m_World->IsHingeHeld(state->GetId());
	}

	float PhysicsScene::GetHingeAngle(const HingeComponent& hinge) const
	{
		const auto* state = FindState(hinge);
		return state && state->IsBuilt() ? m_World->GetHingeAngle(state->GetId()) : 0.0f;
	}

	Vector3 PhysicsScene::GetHingeAnchor(const HingeComponent& hinge) const
	{
		const auto* state = FindState(hinge);
		return state ? state->GetAnchor() : Vector3::Zero;
	}

	void PhysicsScene::ReleaseHinge(const HingeComponent& hinge)
	{
		if (!HasWorld()) return;

		HingeState& state = m_Hinges[&hinge];
		if (state.IsReleased()) return;

		if (state.IsBuilt())
		{
			m_World->ReleaseHinge(state.GetId());
		}
		state.Release();
	}

	void PhysicsScene::DestroyWorld()
	{
		m_World.reset();
		m_Bodies.clear();
		m_Hinges.clear();
		m_Waters.clear();

		m_StepCount = 0;
		m_LastStepMilliseconds = 0.0f;
		m_BodyIssueCount = 0;
		m_HingeIssueCount = 0;
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

	const HingeState* PhysicsScene::FindState(const HingeComponent& hinge) const
	{
		const auto it = m_Hinges.find(&hinge);
		if (it != m_Hinges.end())
		{
			return &it->second;
		}

		return nullptr;
	}

	PhysicsBodyId PhysicsScene::BuiltIdOf(const RigidBodyComponent* body) const
	{
		const RigidBodyState* state = body ? FindState(*body) : nullptr;
		return state && state->IsBuilt() ? state->GetId() : PhysicsBodyId{};
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
					ReleaseBody(*body, *state);
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
					ReleaseBody(*owner, *state);
				}
			}
			else if (const auto* hinge = dynamic_cast<const HingeComponent*>(component))
			{
				if (const auto it = m_Hinges.find(hinge); it != m_Hinges.end())
				{
					DestroyHinge(it->second);
					m_Hinges.erase(it);
				}
			}
			else if (const auto* water = dynamic_cast<const WaterVolumeComponent*>(component))
			{
				if (const auto it = m_Waters.find(water); it != m_Waters.end())
				{
					WithdrawWater(it->second);
					m_Waters.erase(it);
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
				ReleaseBody(*body, state);
			}
			ApplyQueuedImpulses(state);
		}
		m_BodyIssueCount = CountAuthoringIssues(bodies);
	}

	void PhysicsScene::SyncHinges(std::span<HingeComponent* const> hinges)
	{
		if (!HasWorld()) return;

		for (const auto* hinge : hinges)
		{
			HingeState& state = m_Hinges[hinge];
			if (IsPhysicsRunnable(*hinge))
			{
				SyncHinge(*hinge, state);
			}
			else
			{
				DestroyHinge(state);
			}
		}
		m_HingeIssueCount = CountAuthoringIssues(hinges);
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
		ReleaseBody(body, state);

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

	void PhysicsScene::ReleaseBody(const RigidBodyComponent& body, RigidBodyState& state)
	{
		if (state.IsBuilt())
		{
			const bool ended = body.IsPendingDestroy() || body.GetOwnerObject()->IsPendingDestroy();
			DropHingesOf(state.GetId(), ended);
			m_World->DestroyBody(state.GetId());
			state.ClearKinematicTarget();
		}
		state.Clear();
	}

	void PhysicsScene::SyncHinge(const HingeComponent& hinge, HingeState& state)
	{
		if (state.IsReleased()) return;

		const RigidBodyComponent* held = hinge.GetOwnerObject()->GetComponent<RigidBodyComponent>();
		const PhysicsBodyId heldId = BuiltIdOf(held);
		const HingeAuthoring authoring = HingeAuthoring::Of(hinge);
		if (state.IsSameAs(heldId, authoring)) return;

		if (state.IsBuilt())
		{
			m_World->DestroyHinge(state.GetId());
		}

		HingeIssue issue = HingeIssue::None;
		if (held == nullptr)
		{
			issue = HingeIssue::NoBody;
		}
		else if (!IsValidSettings(hinge.GetSettings()))
		{
			issue = HingeIssue::InvalidSettings;
		}
		else if (!heldId.IsValid())
		{
			issue = HingeIssue::BodyFailed;
		}
		else if (held->GetMotion() != PhysicsMotion::Dynamic)
		{
			issue = HingeIssue::NotDynamic;
		}
		if (issue != HingeIssue::None)
		{
			FailHinge(hinge, state, issue, heldId, authoring);
			return;
		}

		const TransformComponent& transform = *hinge.GetTransform();
		const Vector3 pivot = transform.GetPosition();
		const Vector3 axis = transform.GetRotation().Normalized().Rotate(hinge.GetAxis()).Normalized();
		const PhysicsHingeId created = m_World->CreateHinge(PhysicsHingeDesc{
			.body = heldId,
			.pivot = pivot,
			.axis = axis,
			.normal = PerpendicularTo(axis),
			.minAngle = hinge.GetMinAngle(),
			.maxAngle = hinge.GetMaxAngle(),
			.maxFrictionTorque = hinge.GetMaxFrictionTorque()
		});
		if (!created.IsValid())
		{
			FailHinge(hinge, state, HingeIssue::BackendRefused, heldId, authoring);
			return;
		}
		state.Adopt(created, heldId, authoring, pivot);
	}

	void PhysicsScene::FailHinge(const HingeComponent& hinge, HingeState& state, HingeIssue issue,
		PhysicsBodyId held, const HingeAuthoring& authoring)
	{
		if (state.GetIssue() != issue)
		{
			CN_LOG_RUNTIME("Physics", "'", hinge.GetOwnerObject()->GetName(), "' has no hinge: ", IssueText(issue));
		}
		state.Fail(issue, held, authoring);
	}

	void PhysicsScene::DestroyHinge(HingeState& state)
	{
		if (state.IsBuilt())
		{
			m_World->DestroyHinge(state.GetId());
		}
		state.Clear();
	}

	void PhysicsScene::DropHingesOf(PhysicsBodyId body, bool ended)
	{
		for (auto& [hinge, state] : m_Hinges)
		{
			if (!state.Uses(body)) continue;

			state.Drop();
			if (ended)
			{
				state.Release();
			}
		}
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

	// Every runnable, valid water box every step. A box that changed, or is applied for the first time, wakes the
	// sleepers in the old box and in the new one; water that stays as it is never wakes anything.
	void PhysicsScene::ApplyWaters(float fixedDeltaTime, std::span<WaterVolumeComponent* const> waters)
	{
		for (const WaterVolumeComponent* water : waters)
		{
			WaterState& state = m_Waters[water];
			if (!IsPhysicsRunnable(*water))
			{
				WithdrawWater(state);
				continue;
			}

			const TransformComponent& transform = *water->GetTransform();
			const Vector3 size = water->GetSize() * transform.GetLossyScale();
			const bool usable = IsUsableExtent(size) && IsPositive(water->GetDensity())
				&& IsNonNegative(water->GetLinearDrag()) && IsNonNegative(water->GetAngularDrag());
			if (!usable)
			{
				WithdrawWater(state);
				if (!state.invalidLogged)
				{
					state.invalidLogged = true;
					CN_LOG_RUNTIME("Physics", "'", water->GetOwnerObject()->GetName(),
						"': a water volume needs a finite size above zero, a positive density and drags of zero or more; it is ignored");
				}
				continue;
			}
			state.invalidLogged = false;

			const bool rotated = std::fabs(transform.GetRotation().Normalized().W) < 0.99999f;
			if (rotated && !state.rotationLogged)
			{
				CN_LOG_RUNTIME("Physics", "'", water->GetOwnerObject()->GetName(), "': a water volume is an axis-aligned box; its rotation is ignored");
			}
			state.rotationLogged = rotated;

			const PhysicsWaterDesc desc{
				.center = transform.GetPosition(),
				.halfExtent = size * 0.5f,
				.density = water->GetDensity(),
				.linearDrag = water->GetLinearDrag(),
				.angularDrag = water->GetAngularDrag()
			};
			if (!state.applied || !SameWater(*state.applied, desc))
			{
				WithdrawWater(state);
				WakeSleepers(desc);
				state.applied = desc;
			}
			m_World->ApplyWater(desc, fixedDeltaTime);
		}
	}

	void PhysicsScene::WithdrawWater(WaterState& state)
	{
		if (!state.applied) return;

		WakeSleepers(*state.applied);
		state.applied.reset();
	}

	// A zero impulse is enough to activate a sleeping dynamic body; awake bodies are left untouched.
	void PhysicsScene::WakeSleepers(const PhysicsWaterDesc& water)
	{
		for (const PhysicsBodyId id : m_World->QueryBox(water.center, water.halfExtent))
		{
			if (!m_World->IsAwake(id))
			{
				m_World->AddImpulse(id, Vector3::Zero);
			}
		}
	}

	PhysicsStepError PhysicsScene::Step(float fixedDeltaTime, std::span<RigidBodyComponent* const> bodies, std::span<WaterVolumeComponent* const> waters)
	{
		if (!HasWorld()) return PhysicsStepError::None;

		ApplyWaters(fixedDeltaTime, waters);
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
