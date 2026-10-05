#pragma once

#include <Engine.h>

class CameraController final : public Canape::MonoBehaviour
{
public:
    explicit CameraController(Canape::GameObjectHandle target = {})
        : m_Target(target)
    {
    }

    void SetTarget(Canape::GameObjectHandle target) { m_Target = target; }

protected:
    void Awake() override;
    void Start() override;
    void Update() override;
    void LateUpdate() override;

private:
    Canape::GameObjectHandle m_Target;

    float m_Distance = 4.0f;
    float m_PivotHeight = 1.2f;
    float m_Sensitivity = 0.15f;
    float m_MinPitch = -30.0f;
    float m_MaxPitch = 70.0f;

    float m_Yaw = 0.0f;
    float m_Pitch = 15.0f;
};
