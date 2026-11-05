#pragma once

#include <vector>

#include "JoltPhysics/PhysicsTypes.h"
#include "Math/Vector3.h"
#include "Physics/RigidBodyComponent.h"

namespace Canape
{
    class ColliderComponent;
    class Component;
    class TransformComponent;

    using PhysicsShapeParts = std::vector<PhysicsShapePart>;

    // A component takes part in physics only while it and its owner are enabled and not being removed.
    bool IsPhysicsRunnable(const Component& component);

    struct ColliderRecord {
        const ColliderComponent* collider = nullptr;
        const TransformComponent* parent = nullptr;
        Vector3 extent{};
    };

    using ColliderRecords = std::vector<ColliderRecord>;

    // The shape a RigidBody should have right now, read from its colliders, or the issue that leaves it without one.
    // A copy taken when the body is built is compared with later gathers to tell what changed.
    class RigidBodyShape {
    private:
        ColliderRecords records;
        PhysicsShapeParts parts;
        const TransformComponent* bodyParent = nullptr;
        RigidBodyIssue issue = RigidBodyIssue::None;
        bool nonUniformAncestors = false;
        bool nonUniformSphere = false;

    public:
        // Reuses the buffers; the result is valid until the next call.
        void Gather(const RigidBodyComponent& body);
        void Clear();

        const ColliderRecords& GetRecords() const { return records; }
        const PhysicsShapeParts& GetParts() const { return parts; }
        const TransformComponent* GetBodyParent() const { return bodyParent; }
        RigidBodyIssue GetIssue() const { return issue; }
        bool HasNonUniformAncestors() const { return nonUniformAncestors; }
        bool HasNonUniformSphere() const { return nonUniformSphere; }

        // The same colliders in the same order: a built body is kept while this holds.
        bool HasSameColliders(const RigidBodyShape& other) const;
        // Also the same body parent and, per collider, the same parent and extent: what a built body ignores.
        // Part poses are left out because a moving body recomputes them with rounding noise.
        bool HasSameRecords(const RigidBodyShape& other) const;
        // Everything bit for bit, parts and issue included: a failed attempt is retried when this breaks.
        bool IsSameAs(const RigidBodyShape& other) const;
    };
}
