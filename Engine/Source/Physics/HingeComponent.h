#pragma once

#include <cstdint>
#include <numbers>

#include "Core/Containers/SlotMap.h"
#include "JoltPhysics/PhysicsTypes.h"
#include "Math/Vector3.h"
#include "Scene/Component/Component.h"

namespace Canape
{
    class GameObject;

    // Why a Hinge that should hold its body does not.
    enum class HingeIssue : std::uint8_t {
        None,
        NoBody,
        BodyFailed,
        NotDynamic,
        InvalidSettings,
        BackendRefused,
    };

    inline constexpr bool IsAuthoringIssue(HingeIssue issue)
    {
        return issue == HingeIssue::NoBody ||
            issue == HingeIssue::NotDynamic ||
            issue == HingeIssue::InvalidSettings;
    }

    inline const char* IssueText(HingeIssue issue)
    {
        switch(issue)
        {
        case HingeIssue::NoBody:
            return "the held object is missing or has no RigidBody";
        case HingeIssue::BodyFailed:
            return "the held or mount body has no physics body";
        case HingeIssue::NotDynamic:
            return "the held body must be Dynamic";
        case HingeIssue::InvalidSettings:
            return "the axis must be unit, the limits within [-pi, 0] and [0, pi] with min below max, the torque 0 or more, "
                "and the held object another object";
        case HingeIssue::BackendRefused:
            return "the backend refused the hinge";
        default:
            return "none";
        }
    }

    // The axis is in the hinge object's local frame.
    struct HingeSettings {
        Vector3 axis = Vector3::Up;
        float minAngle = -std::numbers::pi_v<float>;
        float maxAngle = std::numbers::pi_v<float>;
        float maxFrictionTorque = 0.0f;
    };

    inline bool IsValidSettings(const HingeSettings& settings)
    {
        return IsUnit(settings.axis)
            && IsValidHingeLimits(settings.minAngle, settings.maxAngle)
            && IsNonNegative(settings.maxFrictionTorque);
    }

    inline constexpr bool SameBits(const HingeSettings& a, const HingeSettings& b)
    {
        const auto& [axis, minAngle, maxAngle, maxFrictionTorque] = a;
        return SameBits(axis, b.axis)
            && SameBits(minAngle, b.minAngle)
            && SameBits(maxAngle, b.maxAngle)
            && SameBits(maxFrictionTorque, b.maxFrictionTorque);
    }

    // Turns about an axis through this object's position. It holds the dynamic RigidBody of `body` to this object's
    // RigidBody, or to the world when it has none. The angle reads 0 where the hinge was made.
    // A change after the hinge exists rebuilds it at the next sync; moving this object does not move the anchor.
    class HingeComponent final : public Component {
        RTTR_ENABLE(Component)

    private:
        Handle<GameObject> body{};
        HingeSettings settings;

    public:
        HingeComponent() = default;

        //***** Authoring
        const Handle<GameObject>& GetBody() const { return body; }
        void SetBody(const Handle<GameObject>& heldObject) { body = heldObject; }
        const HingeSettings& GetSettings() const { return settings; }

        const Vector3& GetAxis() const { return settings.axis; }
        void SetAxis(const Vector3& axis) { settings.axis = axis; }
        float GetMinAngle() const { return settings.minAngle; }
        void SetMinAngle(float angle) { settings.minAngle = angle; }
        float GetMaxAngle() const { return settings.maxAngle; }
        void SetMaxAngle(float angle) { settings.maxAngle = angle; }
        float GetMaxFrictionTorque() const { return settings.maxFrictionTorque; }
        void SetMaxFrictionTorque(float torque) { settings.maxFrictionTorque = torque; }

        //***** Runtime
        // The owning scene's PhysicsScene keeps the runtime state; these forward to it.
        bool HasHinge() const;
        bool HasFailed() const { return GetIssue() != HingeIssue::None; }
        HingeIssue GetIssue() const;
        // Lets the body go and wakes it. A released hinge is not made again, even when a body is rebuilt,
        // until this component is disabled and enabled; releasing twice does nothing.
        void Release();
        bool IsHeld() const;
        // Radians since creation, by the right-hand rule about the axis; it keeps reading after a release.
        float GetAngle() const;
        // Where the anchor was when the hinge was made.
        Vector3 GetWorldAnchor() const;
    };
}
