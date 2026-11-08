#pragma once

#include "JoltPhysics/PhysicsTypes.h"
#include "Physics/HingeComponent.h"
#include "Physics/HingeState.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/RigidBodyShape.h"
#include "Physics/RigidBodyState.h"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace Canape
{
	class Component;
	class JoltRuntime;
	class JoltWorld;

	using RigidBodies = std::vector<const RigidBodyComponent*>;

	struct PhysicsSceneStats
	{
		std::size_t bodyCount = 0;
		std::size_t activeBodies = 0;
		// Steps since the world was created, and how long the last one took in the backend.
		std::uint64_t stepCount = 0;
		float lastStepMilliseconds = 0.0f;
	};

	// Owns the physics world of one scene. It is declared before the scene's game objects so it
	// outlives every component; its destructor never touches component pointers.
	class PhysicsScene final
	{
	public:
        // scene with physics world
        PhysicsScene(JoltRuntime&, const PhysicsWorldDesc&);
        // scene without physics world
		PhysicsScene();
		~PhysicsScene();

		PhysicsScene(const PhysicsScene&) = delete;
		PhysicsScene& operator=(const PhysicsScene&) = delete;

		bool HasWorld() const { return m_World != nullptr; }

		// A body this scene has never synced has no body and no issue.
		bool HasBody(const RigidBodyComponent&) const;
		RigidBodyIssue GetIssue(const RigidBodyComponent&) const;
		// Applied at once while the body exists;
        // otherwise queued and applied right after the next sync creates it,
		// or dropped at that sync when there is still no body.
		void AddImpulse(const RigidBodyComponent&, const Vector3& impulse);
		// Where a kinematic body is to be after the next step; without a target it holds its creation pose.
		// Kept until the body is released, so a target set before the body exists reaches its first step.
		void SetKinematicTarget(const RigidBodyComponent&, const PhysicsPose& target);
		// The velocity of a world-space point moving with the body, as of the last step; zero without a body.
		Vector3 GetVelocityAt(const RigidBodyComponent&, const Vector3& worldPoint) const;

		// A hinge this scene has never synced has no hinge and no issue.
		bool HasHinge(const HingeComponent&) const;
		HingeIssue GetIssue(const HingeComponent&) const;
		bool IsHingeHeld(const HingeComponent&) const;
		// 0 without a hinge; a released hinge keeps reading until its body is rebuilt.
		float GetHingeAngle(const HingeComponent&) const;
		Vector3 GetHingeAnchor(const HingeComponent&) const;
		// Lets a built hinge go. The hinge stays released, even when its body is rebuilt, until the component is
		// disabled and enabled again; a hinge released before it exists is not made this run.
		void ReleaseHinge(const HingeComponent&);

		// Moving bodies (sleeping ones included) whose shapes overlap the world-space box, in creation order.
		RigidBodies QueryBox(const Vector3& center, const Vector3& halfExtent) const;

		// Bodies and hinges whose issue is an authoring problem, as of the last sync.
		std::uint32_t GetAuthoringIssueCount() const { return m_BodyIssueCount + m_HingeIssueCount; }
		// The flags of the last step; None before the first step.
		PhysicsStepError GetLastStepError() const { return m_LastStepError; }

		// Zeros while there is no world.
		PhysicsSceneStats GetStats() const;

	private:
		friend class Scene;

		void DestroyWorld();

		void ReleaseBodies(std::span<Component* const> removed);
		void SyncBodies(std::span<RigidBodyComponent* const>);
		// After SyncBodies, so that the body of a hinge exists.
		void SyncHinges(std::span<HingeComponent* const>);

		PhysicsStepError Step(float fixedDeltaTime, std::span<RigidBodyComponent* const>);

        JoltWorld* GetWorld() const { return m_World.get(); }

		const RigidBodyState* FindState(const RigidBodyComponent&) const;
		RigidBodyState* FindState(const RigidBodyComponent& body)
        {
            return const_cast<RigidBodyState*>(std::as_const(*this).FindState(body));
        }
		const RigidBodyComponent* FindBody(PhysicsBodyId) const;
		const HingeState* FindState(const HingeComponent&) const;
		// The id of the body's backend body, or an invalid id while it has none.
		PhysicsBodyId BuiltIdOf(const RigidBodyComponent*) const;

		void SyncBody(const RigidBodyComponent&, RigidBodyState&);
		// Records and logs why the shape in m_Shape left the body without a backend body.
		void FailBody(const RigidBodyComponent&, RigidBodyState&, RigidBodyIssue);
		void ReleaseBody(const RigidBodyComponent&, RigidBodyState&);
		void ApplyQueuedImpulses(RigidBodyState&);
		void MoveKinematicBodies(float fixedDeltaTime, std::span<RigidBodyComponent* const>);
		void WriteBack(const RigidBodyComponent&, PhysicsBodyId) const;

		void SyncHinge(const HingeComponent&, HingeState&);
		// Records why the hinge was not made and logs it when the reason is new.
		void FailHinge(const HingeComponent&, HingeState&, HingeIssue, PhysicsBodyId held, const HingeAuthoring&);
		void DestroyHinge(HingeState&);
		// The backend drops a hinge with its body; the record follows. A body removed for good ends its hinge like a
		// release, one merely rebuilt lets it be made again.
		void DropHingesOf(PhysicsBodyId body, bool ended);

    private:
		std::unique_ptr<JoltWorld> m_World;
		// Keys are never dereferenced;
        // a body's entry is erased when the component is removed.
		std::unordered_map<const RigidBodyComponent*, RigidBodyState> m_Bodies;
		std::unordered_map<const HingeComponent*, HingeState> m_Hinges;

		std::uint64_t m_StepCount = 0;
		float m_LastStepMilliseconds = 0.0f;
		std::uint32_t m_BodyIssueCount = 0;
		std::uint32_t m_HingeIssueCount = 0;
		PhysicsStepError m_LastStepError = PhysicsStepError::None;

		// Scratch for SyncBody; holds the shape of the body synced last.
		RigidBodyShape m_Shape;
    };

	// The physics of the scene that owns the component's object, or null while it has none.
	PhysicsScene* FindPhysics(const Component&);
}
