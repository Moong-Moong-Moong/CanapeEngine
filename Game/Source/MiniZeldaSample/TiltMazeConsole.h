#pragma once

#include <Engine.h>

#include <string>

#include "MiniZeldaSample/PuzzleControlsHud.h"

class TiltMazePuzzle;

// 퍼즐판 앞 석판. 플레이어가 가까이에서 E를 누르면 퍼즐 조작 모드로 들어가고, 다시 E를 누르면 나온다.
//
// 조작 모드:
// - 메인 카메라를 퍼즐판을 수직으로 내려다보는 puzzleCamera로 바꾼다.
// - 플레이어 이동 입력(MoveAnimationController)과 기존 카메라 회전(BackViewCamera)을 멈춘다.
// - 마우스 이동량으로 판을 기울인다. 화면 오른쪽으로 밀면 공이 오른쪽으로, 위로 밀면 위쪽으로 구른다.
class TiltMazeConsole final : public Canape::ScriptComponent
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
    TiltMazePuzzle* GetPuzzle() const;
    Canape::GameObject* GetPlayer();
    bool IsPlayerInRange();
    void Enter();
    void Exit();
    void SetPlayerControl(bool enabled);

    //***** Authoring
    Handle<Canape::GameObject> m_Puzzle{};       // TiltMazePuzzle이 붙은 퍼즐 루트
    Handle<Canape::GameObject> m_PuzzleCamera{}; // 퍼즐판을 내려다보는 CameraComponent
    std::string m_PlayerName = "Player";         // prefab은 씬 오브젝트를 직접 참조할 수 없어 이름으로 찾는다
    float m_InteractRadius = 2.0f;               // 석판에서 이 거리(수평, m) 안이면 E로 조작 가능
    float m_MouseSensitivity = 0.08f;            // 마우스 1픽셀당 기울기(도)

    //***** Runtime (직렬화 안 함)
    bool m_Active = false;
    PuzzleControlsHud m_Hud; // 조작법 안내(조작 중에만 보인다)
    Handle<Canape::GameObject> m_Player{};
    Handle<Canape::GameObject> m_PreviousCamera{};
    bool m_LoggedNoPlayer = false;
};
