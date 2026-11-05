#include "Physics/RigidBodyState.h"

#include <algorithm>

namespace Canape
{
    bool RigidBodyState::IsBuiltFrom(const ColliderComponent& collider) const
    {
        return std::ranges::any_of(
            builtFrom.GetRecords(),
            [&collider](const ColliderRecord& record)
            {
                return record.collider == &collider;
            }
        );
    }

    ShapeMatch RigidBodyState::Match(const PhysicsBodySettings& settings, const RigidBodyShape& shape)
    {
        using enum ShapeMatch;

        if(IsBuilt())
        {
            if(!builtFrom.HasSameColliders(shape))
            {
                return Changed;
            }
            if(mutationNoticed || builtFrom.HasSameRecords(shape))
            {
                return Same;
            }
            mutationNoticed = true;
            return FirstMutation;
        }

        return isUnchangedFailure(settings, shape) ? Same : Changed;
    }

    void RigidBodyState::Adopt(PhysicsBodyId created, const PhysicsBodySettings& settings, const RigidBodyShape& shape)
    {
        id = created;
        issue = RigidBodyIssue::None;
        mutationNoticed = false;
        builtFrom = shape;
        builtWith = settings;
    }

    void RigidBodyState::Fail(RigidBodyIssue failure, const PhysicsBodySettings& settings, const RigidBodyShape& shape)
    {
        id = {};
        issue = failure;
        builtFrom = shape;
        builtWith = settings;
    }

    void RigidBodyState::Clear()
    {
        id = {};
        issue = RigidBodyIssue::None;
        mutationNoticed = false;
        builtFrom.Clear();
        builtWith = {};
    }

    // A retry would fail the same way: nothing the failed attempt was made from has changed.
    // A body that was built and then lost is not a failure, so it is built again.
    bool RigidBodyState::isUnchangedFailure(const PhysicsBodySettings& settings, const RigidBodyShape& shape) const
    {
        return issue != RigidBodyIssue::None
            && builtFrom.IsSameAs(shape)
            && SameBits(builtWith, settings);
    }
}
