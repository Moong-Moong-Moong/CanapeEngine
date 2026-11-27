#pragma once

#include <Engine.h>

//***** 기사단 게임 타이틀 화면
//*** 제목, [게임 시작], [종료] 버튼. 마우스 커서를 계속 보이게 요청한다.
class KnightsTitleMenu final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    KnightsTitleMenu() = default;

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;

private:
    Canape::UIButton* AddMenuButton(Canape::UIElement& parent, std::string name, std::string text, float y);

    Canape::TextureResourceId m_RoundedTexture;
};
