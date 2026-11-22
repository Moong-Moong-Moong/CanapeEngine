#pragma once

#include <Engine.h>

// Third-person back view. Keeps the target centred, orbits with the mouse without turning the target,
// and swings back behind the target after a while without mouse input or when the target attacks.
class BackViewCamera final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    void SetTarget(Handle<Canape::GameObject> target) { m_Target = target; }

protected:
    void Start() override;
    void Update() override;
    void LateUpdate() override;

private:
    // Yaw, in degrees, of the target's forward on the XZ plane.
    float TargetYaw(const Canape::GameObject& target) const;
    bool IsTargetAttacking(const Canape::GameObject& target) const;
    // Lowest pitch that keeps the camera MinHeightAboveFeet over the target's feet.
    float GroundPitchLimit(const Canape::Vector3& feet, const Canape::Vector3& pivot) const;
    void Place(const Canape::Vector3& pivot);

    Handle<Canape::GameObject> m_Target;

    // Meters from the pivot to the camera.
    float m_Distance = 4.5f;
    // Meters above the target's origin (its feet) that the camera looks at.
    float m_PivotHeight = 1.3f;
    // Degrees per pixel of mouse movement.
    float m_Sensitivity = 0.15f;
    // Degrees; positive looks down.
    float m_MinPitch = -10.0f;
    float m_MaxPitch = 60.0f;
    // Pitch the camera settles at when it swings back behind the target.
    float m_DefaultPitch = 15.0f;
    // Seconds without mouse input before the camera swings back behind the target.
    float m_RecenterDelay = 2.0f;
    // How quickly it swings back (per second; higher is faster).
    float m_RecenterSpeed = 4.0f;
    // Meters the camera stays above the target's feet, so it never sinks into the ground.
    float m_MinHeightAboveFeet = 0.3f;
    // How tightly the pivot follows the target (per second; higher is tighter).
    float m_FollowSharpness = 20.0f;
    // Meters per second above which the target counts as moving (idle swing-back waits for it to stop).
    float m_MovingSpeed = 0.1f;

    // Current orbit, degrees.
    float m_Yaw = 0.0f;
    float m_Pitch = 15.0f;

    // Runtime state, not registered.
    float m_IdleTime = 0.0f;
    bool m_Recentering = false;
    bool m_WasAttacking = false;
    // The current swing-back was started by an attack (it keeps running while the target moves).
    bool m_AttackRecenter = false;
    bool m_HasLastFeet = false;
    Canape::Vector3 m_LastFeet;
    bool m_HasPivot = false;
    Canape::Vector3 m_Pivot;
};
