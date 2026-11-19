#pragma once

#include <Engine.h>

#include <array>
#include <string>

using namespace Canape;

class AttackState final : public Canape::FsmState
{
    RTTR_ENABLE(Canape::FsmState)
    RTTR_REGISTRATION_FRIEND

public:
    void Initialize() override;
    void Enter() override;
    void Update(float deltaTime) override;
    void Exit() override;

private:
    void PlayCombo(int comboIndex);
    float GetNormalizedTime() const;

    static constexpr int kComboCount = 3;
    const std::array<std::string, kComboCount> m_comboClips = {
        "Content/Models/Knight/attack01.aclip",
        "Content/Models/Knight/attack02.aclip",
        "Content/Models/Knight/attack03.aclip",
    };

    AnimatorComponent* m_animator = nullptr;
    int m_comboIndex = 0;
    bool m_inputBuffered = false;
    float m_graceTimer = 0.0f;

    float m_inputOpenRatio = 0.3f;
    float m_comboCancelRatio = 0.8f;
    float m_comboGraceTime = 0.35f;
};
