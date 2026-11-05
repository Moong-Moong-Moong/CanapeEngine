#pragma once

#include "JoltPhysics/PhysicsTypes.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/RigidBodyShape.h"
#include "Physics/RigidBodyState.h"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace Canape
{
	class Component;
	class JoltRuntime;
	class JoltWorld;

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

		// Zeros while there is no world.
		PhysicsSceneStats GetStats() const;

	private:
		friend class Scene;

		void DestroyWorld();

		void ReleaseBodies(std::span<Component* const> removed);
		void SyncBodies(std::span<RigidBodyComponent* const>);

		PhysicsStepError Step(float fixedDeltaTime, std::span<RigidBodyComponent* const>);

        JoltWorld* GetWorld() const { return m_World.get(); }

		const RigidBodyState* FindState(const RigidBodyComponent&) const;
		RigidBodyState* FindState(const RigidBodyComponent& body)
        {
            return const_cast<RigidBodyState*>(std::as_const(*this).FindState(body));
        }

		void SyncBody(const RigidBodyComponent&, RigidBodyState&);
		// Records and logs why the shape in m_Shape left the body without a backend body.
		void FailBody(const RigidBodyComponent&, RigidBodyState&, RigidBodyIssue);
		void ReleaseBody(RigidBodyState&);
		void ApplyQueuedImpulses(RigidBodyState&);
		void WriteBack(const RigidBodyComponent&, PhysicsBodyId) const;

    private:
		std::unique_ptr<JoltWorld> m_World;
		// Keys are never dereferenced;
        // a body's entry is erased when the component is removed.
		std::unordered_map<const RigidBodyComponent*, RigidBodyState> m_Bodies;

		std::uint64_t m_StepCount = 0;
		float m_LastStepMilliseconds = 0.0f;

		// Scratch for SyncBody; holds the shape of the body synced last.
		RigidBodyShape m_Shape;
		PhysicsStepError m_LastLoggedStepError = PhysicsStepError::None;
    };
}
