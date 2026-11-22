#pragma once

#include <Engine.h>

#include <array>
#include <deque>
#include <string>

// 고대의 문과 그 주위의 횃불 3개. 퍼즐을 하나 풀 때마다 횃불이 하나씩 켜지고, 셋 다 켜지면 문이 열린다.
//
// - puzzle1~3Name: 씬에 배치된 퍼즐 루트의 이름(TiltMazePuzzle / LeverPuzzle / MarbleRun 스크립트가 붙은 오브젝트).
//   푼 순서대로 torch 1, 2, 3이 켜진다.
// - 퍼즐을 풀고 solveDelay초 뒤 연출: 열려 있는 석판을 닫고, 그 횃불을 비추는 카메라로 바꿔 불을 켠 뒤
//   주인공 카메라로 돌아온다. 불은 fire 오브젝트의 ParticleEffectComponent를 켜서 낸다(처음에는 꺼 둔다).
// - 세 번째 횃불 연출이 끝나면 문을 정면으로 비추는 카메라로 바꾸고, 카메라를 흔들면서 문(두 회전축)을 연다.
//   다 열리면 주인공 카메라로 돌아온다.
// - 연출 중에는 플레이어 조작과 석판 진입을 막는다(IsCinematicPlaying).
class AncientGate final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    // 연출 중이면 석판들이 E 입력을 무시한다.
    static bool IsCinematicPlaying() { return s_CinematicPlaying; }
    // 문이 다 열렸는지(엔딩 판정용)
    bool IsGateOpened() const { return m_GateOpened; }

    //***** 개발자 모드
    // 횃불 하나를 바로 켠다(연출 없이). 셋 다 켜지면 문 열기 연출을 시작한다.
    void DevLightTorch(int index);
    // 퍼즐 하나를 "아직 안 푼 것"으로 되돌린다(다시 풀면 다음 꺼진 횃불이 켜진다).
    void DevForgetPuzzle(int index);

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;

private:
    enum class Step { None, TorchLook, TorchHold, GateLook, GateOpen, GateHold };

    struct PendingTorch
    {
        float delay = 0.0f;
    };

    bool IsPuzzleSolved(const std::string& name) const;
    void CheckPuzzles();
    void BeginCinematic();
    void EndCinematic();
    void SwitchCamera(const Handle<Canape::GameObject>& cameraObject);
    void SetPlayerControl(bool enabled);
    void CloseConsoles();
    void SetFire(int index, bool lit);
    void UpdateGateOpen(float dt);

    //***** Authoring
    std::string m_Puzzle1Name = "TiltMazePuzzle";
    std::string m_Puzzle2Name = "LeverPuzzle";
    std::string m_Puzzle3Name = "MarbleRun";
    Handle<Canape::GameObject> m_Fire1{};
    Handle<Canape::GameObject> m_Fire2{};
    Handle<Canape::GameObject> m_Fire3{};
    Handle<Canape::GameObject> m_TorchCamera1{};
    Handle<Canape::GameObject> m_TorchCamera2{};
    Handle<Canape::GameObject> m_TorchCamera3{};
    Handle<Canape::GameObject> m_GateCamera{};
    Handle<Canape::GameObject> m_LeftPivot{};   // 왼쪽 문짝의 회전축(+Y로 열린다)
    Handle<Canape::GameObject> m_RightPivot{};  // 오른쪽 문짝의 회전축(-Y로 열린다)
    std::string m_PlayerName = "Player";
    std::string m_TorchSound = "Content/Audio/TorchFlare.mp3"; // 횃불이 켜질 때 그 위치에서 3D로
    float m_SolveDelay = 2.0f;      // 퍼즐을 풀고 연출을 시작하기까지(초)
    float m_TorchLookTime = 0.8f;   // 횃불 카메라로 바꾸고 불을 켜기까지(초)
    float m_TorchHoldTime = 2.0f;   // 불이 켜진 뒤 보여주는 시간(초)
    float m_GateLookTime = 1.0f;    // 문 카메라로 바꾸고 열기 시작하기까지(초)
    float m_DoorOpenTime = 3.0f;    // 문이 다 열리는 데 걸리는 시간(초)
    float m_DoorOpenAngle = 90.0f;  // 문짝이 열리는 각도(도)
    float m_GateHoldTime = 1.5f;    // 다 열린 뒤 보여주는 시간(초)
    float m_ShakeAmplitude = 0.08f; // 카메라 흔들림 크기(m)
    float m_ShakeFrequency = 18.0f; // 카메라 흔들림 빠르기(Hz)

    //***** Runtime (직렬화 안 함)
    static bool s_CinematicPlaying;
    std::array<bool, 3> m_Counted{};
    std::deque<PendingTorch> m_Pending;
    std::array<bool, 3> m_Lit{};
    int m_CurrentTorch = -1; // 연출 중인 횃불
    int FirstUnlitTorch() const;
    bool AllLit() const { return m_Lit[0] && m_Lit[1] && m_Lit[2]; }
    void BeginGateSequence();
    Step m_Step = Step::None;
    float m_Timer = 0.0f;
    Handle<Canape::GameObject> m_ReturnCamera{};
    Canape::Vector3 m_GateCameraBase{};
    Canape::Quaternion m_LeftStart = Canape::Quaternion::Identity;
    Canape::Quaternion m_RightStart = Canape::Quaternion::Identity;
    float m_OpenTime = 0.0f;
    bool m_GateOpened = false;
};
