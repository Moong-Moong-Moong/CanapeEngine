#pragma once

#include <Engine.h>

class PlayerController final : public Canape::MonoBehaviour
{
public:
    explicit PlayerController(float moveSpeed = 3.0f, float turnSpeedDegrees = 540.0f)
        : m_MoveSpeed(moveSpeed)
        , m_TurnSpeedDegrees(turnSpeedDegrees)
    {
    }

protected:
    void Awake() override;
    void Start() override;
    void Update() override;

private:
    Canape::Vector3 GetMoveInput() const;
    void GetCameraAxes(Canape::Vector3& outForward, Canape::Vector3& outRight) const;

    float m_MoveSpeed;
    float m_TurnSpeedDegrees;
};
