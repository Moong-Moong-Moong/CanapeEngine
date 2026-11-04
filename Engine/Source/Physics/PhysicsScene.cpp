#include "Physics/PhysicsScene.h"

#include "Core/Application.h"

#include "CNDebug/DebugLog.h"
#include "JoltPhysics/JoltRuntime.h"
#include "JoltPhysics/JoltWorld.h"

#include <cassert>
#include <chrono>

namespace Canape
{
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

	void PhysicsScene::DestroyWorld()
	{
		m_World.reset();

		++m_WorldEpoch;
		m_StepCount = 0;
		m_LastStepMilliseconds = 0.0f;
	}

    void PhysicsScene::ReleaseBodies(std::span<Component* const>)
	{
	}

	void PhysicsScene::SyncBodies()
	{
		m_AuthoringIssueCount = 0;
	}

	PhysicsStepError PhysicsScene::Step(float fixedDeltaTime)
	{
		const auto stepStart = std::chrono::steady_clock::now();
        const PhysicsStepError error = m_World->Step(fixedDeltaTime);

        m_LastStepMilliseconds = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - stepStart
        ).count();
        ++m_StepCount;

        if (StepFailed(error))
		{
			CN_LOG_RUNTIME("Physics", "Physics step failed, error flags: ", static_cast<int>(error));
			assert(false && "Physics step failed");
		}

		return error;
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
