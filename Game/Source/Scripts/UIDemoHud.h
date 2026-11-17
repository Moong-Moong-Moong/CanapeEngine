#pragma once

#include <Engine.h>

//***** UI 확인용 HUD (1·2단계)
//*** 왼쪽 위: 둥근 9-slice 체력바와 수치 텍스트. 시간이 지나면 줄어든다.
//*** 오른쪽 위: 글자가 붙은 회복·일시정지 버튼. 버튼 위 클릭은 게임 쪽 마우스 입력에서 빠진다.
//*** 왼쪽 아래: 세로 박스 + 자동 줄바꿈 도움말. 패널 밖으로 넘친 줄은 잘린다.
//*** 아래 가운데: 가로 박스로 늘어놓은 슬롯.
class UIDemoHud final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    UIDemoHud() = default;

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;

private:
    void RefreshPauseButton();
    Canape::UIImage* AddRounded(Canape::UIElement& parent, std::string name, Canape::UIColor color, float border);
    Canape::UIText* AddLabel(Canape::UIElement& parent, std::string name, std::string text, float size);

    float m_DrainPerSecond = 0.1f;
    float m_Health = 1.0f;
    bool m_Paused = false;
    int m_ShownPercent = -1;

    Canape::TextureResourceId m_RoundedTexture;
    Canape::UIImage* m_HealthFill = nullptr;
    Canape::UIText* m_HealthText = nullptr;
    Canape::UIButton* m_PauseButton = nullptr;
    Canape::UIText* m_PauseLabel = nullptr;
};
