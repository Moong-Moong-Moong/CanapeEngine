#pragma once

#include <Engine.h>

#include <string>

#include "MiniZeldaSample/PuzzleControlsHud.h"

class MarbleRun;

// 구슬 퍼즐 앞 석판. 가까이에서 E를 누르면 공을 앞에서 따라가는 카메라로 바뀌고 공이 굴러가기 시작한다.
// 조작 중에는 Space(또는 마우스 왼쪽 버튼)로 홈 묶음을 바꾼다. 다시 E를 누르면 원래 카메라로 돌아간다.
class MarbleConsole final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    bool IsActive() const { return m_Active; }
    // 조작 중이면 빠져나온다(연출 시작 시 AncientGate가 호출).
    void Close() { if (m_Active) Exit(); }

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;

private:
    MarbleRun* GetPuzzle() const;
    Canape::GameObject* GetPlayer();
    bool IsPlayerInRange();
    void Enter();
    void Exit();
    void SetPlayerControl(bool enabled);

    //***** Authoring
    Handle<Canape::GameObject> m_Puzzle{};
    Handle<Canape::GameObject> m_FollowCamera{};
    std::string m_PlayerName = "Player";
    float m_InteractRadius = 2.0f;

    //***** Runtime (직렬화 안 함)
    bool m_Active = false;
    PuzzleControlsHud m_Hud; // 조작법 안내(조작 중에만 보인다)
    Handle<Canape::GameObject> m_Player{};
    Handle<Canape::GameObject> m_PreviousCamera{};
};
