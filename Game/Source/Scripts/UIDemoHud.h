#pragma once

#include <Engine.h>

//***** 1단계 UI 확인용 HUD
//*** 왼쪽 위 체력바가 시간에 따라 줄어들고, 오른쪽 위 버튼으로 회복·일시정지한다.
//*** 버튼 위에서 클릭하면 게임 쪽 마우스 입력은 막힌다.
class UIDemoHud final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    UIDemoHud() = default;

protected:
    void Start() override;
    void Update() override;

private:
    void RefreshPauseButton();

    float m_DrainPerSecond = 0.1f;
    float m_Health = 1.0f;
    bool m_Paused = false;

    Canape::UIImage* m_HealthFill = nullptr;
    Canape::UIButton* m_PauseButton = nullptr;
};
