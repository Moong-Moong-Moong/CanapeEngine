#pragma once

#include <cstdint>
#include <numbers>

#include "Core/Containers/SlotMap.h"
#include "JoltPhysics/PhysicsTypes.h"
#include "Math/Vector3.h"
#include "Scene/Component/Component.h"

namespace Canape
{
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
            return "this object has no RigidBody";
        case HingeIssue::BodyFailed:
            return "this object's RigidBody has no physics body";
        case HingeIssue::NotDynamic:
            return "this object's RigidBody must be Dynamic";
        case HingeIssue::InvalidSettings:
            return "the axis must be unit, the limits within [-pi, 0] and [0, pi] with min below max, and the torque 0 or more";
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

    // Holds this object's dynamic RigidBody to the world so that it only turns about an axis through this object's
    // position. The angle reads 0 where the hinge was made. A change after the hinge exists rebuilds it at the next
    // sync; moving this object does not move the anchor.
    class HingeComponent final : public Component {
        RTTR_ENABLE(Component)

    private:
        HingeSettings settings;

    public:
        HingeComponent() = default;

        //***** Authoring
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
        // Lets the body go and wakes it. A released hinge is not made again, even when the body is rebuilt,
        // until this component is disabled and enabled; releasing twice does nothing.
        void Release();
        bool IsHeld() const;
        // Radians since creation, by the right-hand rule about the axis; it keeps reading after a release.
        float GetAngle() const;
        // Where the anchor was when the hinge was made.
        Vector3 GetWorldAnchor() const;
    };
}
