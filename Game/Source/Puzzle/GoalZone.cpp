#include "Puzzle/GoalZone.h"

#include <cmath>

#include "CNDebug/DebugLog.h"

RTTR_REGISTRATION
{
    rttr::registration::class_<GoalZone>("GoalZone")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("size", &GoalZone::size);
}

std::optional<GoalRegion> GoalZone::GetRegion() const
{
    const Canape::TransformComponent& transform = *GetTransform();
    const Canape::Vector3 extent = Canape::Abs(size * transform.GetLossyScale());
    if(!Canape::IsUsableExtent(extent))
    {
        if(!sizeLogged)
        {
            CN_LOG_GAME("puzzle", "'", GetOwnerObject()->GetName(), "' holds nothing: its size times the scale must be finite and above zero");
            sizeLogged = true;
        }
        return std::nullopt;
    }
    sizeLogged = false;

    constexpr float IdentityTolerance = 1.0e-4f;
    const bool rotated = std::fabs(transform.GetRotation().Normalized().W) < 1.0f - IdentityTolerance;
    if(rotated && !rotationLogged)
    {
        CN_LOG_GAME("puzzle", "'", GetOwnerObject()->GetName(), "': a zone is an axis-aligned box; its rotation is ignored");
    }
    rotationLogged = rotated;

    return GoalRegion{
        transform.GetPosition(),
        extent * 0.5f
    };
}
