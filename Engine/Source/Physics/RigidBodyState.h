#pragma once

#include <cstdint>
#include <vector>

#include "JoltPhysics/PhysicsTypes.h"
#include "Math/Vector3.h"
#include "Physics/RigidBodyComponent.h"
#include "Physics/RigidBodyShape.h"

namespace Canape
{
    class ColliderComponent;

    using QueuedImpulses = std::vector<Vector3>;

    // How the shape a body should have now relates to the one its state was made from.
    enum class ShapeMatch : std::uint8_t {
        Same,
        // Still the same body, but a change that is ignored after creation was seen for the first time.
        FirstMutation,
        // The body must be built again, or the failure retried.
        Changed,
    };

    // The record of one RigidBody's runtime side: empty, built (a valid id) or failed (an issue).
    // It never touches the world; the owner creates and destroys bodies and tells it the outcome.
    class RigidBodyState {
    private:
        PhysicsBodyId id{};
        RigidBodyIssue issue = RigidBodyIssue::None;
        bool mutationNoticed = false;
        // What the last Adopt or Fail was made from.
        RigidBodyShape builtFrom;
        PhysicsBodySettings builtWith{};
        // Impulses that arrived while there was no body; Clear leaves them for the owner to apply or drop.
        QueuedImpulses queuedImpulses;

    public:
        PhysicsBodyId GetId() const { return id; }
        RigidBodyIssue GetIssue() const { return issue; }
        bool IsBuilt() const { return id.IsValid(); }
        bool IsBuiltFrom(const ColliderComponent& collider) const;
        const QueuedImpulses& GetQueuedImpulses() const { return queuedImpulses; }

        // A built body matches while its colliders are the same; a failure matches while nothing it was made from changed.
        ShapeMatch Match(const PhysicsBodySettings& settings, const RigidBodyShape& shape);
        // Records the body the world created from the shape.
        void Adopt(PhysicsBodyId created, const PhysicsBodySettings& settings, const RigidBodyShape& shape);
        void Fail(RigidBodyIssue failure, const PhysicsBodySettings& settings, const RigidBodyShape& shape);
        void Clear();

        void QueueImpulse(const Vector3& impulse) { queuedImpulses.push_back(impulse); }
        void ClearQueuedImpulses() { queuedImpulses.clear(); }

    private:
        bool isUnchangedFailure(const PhysicsBodySettings& settings, const RigidBodyShape& shape) const;
    };
}
