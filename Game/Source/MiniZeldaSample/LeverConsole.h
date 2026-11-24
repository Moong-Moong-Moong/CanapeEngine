#pragma once

#include <Engine.h>

#include <string>

#include "MiniZeldaSample/PuzzleControlsHud.h"

class LeverPuzzle;

// 지렛대 퍼즐 앞 석판. 가까이에서 E를 누르면 지렛대와 타겟이 일직선으로 보이는 카메라로 바뀌고 돌을 조종한다.
//
// 조종 모드:
// - 마우스 포인터 아래로 레이캐스트해서 돌 위에서 왼쪽 버튼을 누르면 그 돌을 집는다(위로 떠오른다).
// - 버튼을 누르고 있는 동안 WASD로 돌을 수평 이동한다(카메라 기준). 버튼을 떼면 돌이 떨어진다.
// - R: 돌을 제자리로. E: 원래 카메라로 돌아간다.
// 나무가 쓰러지기 전에도 조종은 되지만 지렛대가 없어 클리어할 수 없다.
class LeverConsole final : public Canape::ScriptComponent
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
    LeverPuzzle* GetPuzzle() const;
    Canape::GameObject* GetPlayer();
    bool IsPlayerInRange();
    void Enter();
    void Exit();
    void SetPlayerControl(bool enabled);
    // 마우스 포인터를 지나는 월드 레이. 포인터가 게임 화면 밖이면 false.
    bool GetMouseRay(Canape::Vector3& origin, Canape::Vector3& direction) const;
    void UpdateStoneControl();

    //***** Authoring
    Handle<Canape::GameObject> m_Puzzle{};
    Handle<Canape::GameObject> m_PuzzleCamera{};
    std::string m_PlayerName = "Player";
    float m_InteractRadius = 2.0f;

    //***** Runtime (직렬화 안 함)
    bool m_Active = false;
    PuzzleControlsHud m_Hud; // 조작법 안내(조작 중에만 보인다)
    Handle<Canape::GameObject> m_Player{};
    Handle<Canape::GameObject> m_PreviousCamera{};
};
