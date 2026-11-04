#pragma once

#include "JoltPhysics/PhysicsTypes.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

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
		auto GetWorldEpoch() const { return m_WorldEpoch; }
		// Zeros while there is no world.
		PhysicsSceneStats GetStats() const;

	private:
		friend class Scene;
		friend class RigidBodyComponent;
		friend class HingeComponent;

		void DestroyWorld();

		void ReleaseBodies(std::span<Component* const> removed);
		void SyncBodies();

		PhysicsStepError Step(float fixedDeltaTime);

        JoltWorld* GetWorld() const { return m_World.get(); }

    private:
		std::unique_ptr<JoltWorld> m_World;

        // Changes whenever the world is destroyed
		std::uint32_t m_WorldEpoch = 0;
		std::uint64_t m_StepCount = 0;
		float m_LastStepMilliseconds = 0.0f;

        std::uint32_t m_AuthoringIssueCount = 0;
    };
}
