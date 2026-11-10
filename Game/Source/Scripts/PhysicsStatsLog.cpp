#include "Scripts/PhysicsStatsLog.h"

#include <cmath>
#include <cstdint>

#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<PhysicsStatsLog>("PhysicsStatsLog")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("toggleKey", &PhysicsStatsLog::toggleKey);
}

void PhysicsStatsLog::FixedUpdate()
{
    if(!logging) return;

    const auto ticksPerSecond = static_cast<std::uint64_t>(std::lround(1.0f / Canape::Time::FixedDeltaTime()));
    if(++loggedTicks % ticksPerSecond != 0) return;

#if defined(_DEBUG)
    const Canape::PhysicsSceneStats stats = GetOwnerObject()->GetScene()->GetPhysics().GetStats();
    CN_LOG_GAME("physics", "bodies ", stats.bodyCount, ", active ", stats.activeBodies, ", steps ", stats.stepCount,
        ", last step ", stats.lastStepMilliseconds, " ms");
#endif
}

void PhysicsStatsLog::Update()
{
    if(Canape::Application::Get().GetInputManager().IsKeyPressed(toggleKey))
    {
        logging = !logging;
    }
}
