#pragma once

#include "Core/Containers/SlotMap.h"
#include "JoltPhysics/PhysicsTypes.h"
#include "Math/Vector3.h"
#include "Physics/HingeComponent.h"

namespace Canape
{
    // The authored values a hinge is built from; a change rebuilds it.
    struct HingeAuthoring {
        HingeSettings settings{};

        static HingeAuthoring Of(const HingeComponent& hinge)
        {
            return { hinge.GetSettings() };
        }

        bool IsSameAs(const HingeAuthoring& other) const
        {
            return SameBits(settings, other.settings);
        }
    };

    // The record of one Hinge's runtime side: empty, built (a valid id), failed (an issue) or released.
    // It never touches the world; the owner creates and destroys hinges and tells it the outcome.
    class HingeState {
    private:
        PhysicsHingeId id{};
        HingeIssue issue = HingeIssue::None;
        // Set by Release, or when the body is removed for good; only Clear resets it.
        bool released = false;
        // What the last Adopt or Fail was made from; an invalid id when the object had no body.
        PhysicsBodyId heldBody{};
        HingeAuthoring builtWith{};
        Vector3 anchor{};

    public:
        PhysicsHingeId GetId() const { return id; }
        HingeIssue GetIssue() const { return issue; }
        bool IsBuilt() const { return id.IsValid(); }
        bool IsReleased() const { return released; }
        const Vector3& GetAnchor() const { return anchor; }
        bool Uses(PhysicsBodyId body) const { return body.IsValid() && heldBody == body; }

        // A built hinge matches while its body and authoring are the same; a failure matches while nothing it was made
        // from changed, so it is neither retried nor logged again.
        bool IsSameAs(PhysicsBodyId held, const HingeAuthoring& authoring) const
        {
            const bool recorded = IsBuilt() || issue != HingeIssue::None;
            return recorded && heldBody == held && builtWith.IsSameAs(authoring);
        }

        void Adopt(PhysicsHingeId created, PhysicsBodyId held, const HingeAuthoring& authoring, const Vector3& worldAnchor)
        {
            id = created;
            issue = HingeIssue::None;
            heldBody = held;
            builtWith = authoring;
            anchor = worldAnchor;
        }

        void Fail(HingeIssue failure, PhysicsBodyId held, const HingeAuthoring& authoring)
        {
            id = {};
            issue = failure;
            heldBody = held;
            builtWith = authoring;
        }

        // The backend removed the hinge together with its body; the release and the anchor are kept.
        void Drop()
        {
            id = {};
            issue = HingeIssue::None;
            heldBody = {};
            builtWith = {};
        }

        void Release() { released = true; }

        void Clear()
        {
            Drop();
            released = false;
            anchor = {};
        }
    };
}
