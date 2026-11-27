#pragma once

#include <Engine.h>

//***** 기사단 게임 본 화면 (뼈대)
//*** 배경과 제목 텍스트만 띄우고 커서를 요청한다. 보드 UI는 이후 단계에서 채운다.
class KnightsBoardController final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    KnightsBoardController() = default;

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;
};
