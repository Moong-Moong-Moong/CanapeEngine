#pragma once

#include <Engine.h>

#include <array>
#include <string>

// 기울이는 미로 퍼즐. 퍼즐 루트 오브젝트에 붙인다.
//
// - board: 키네마틱 RigidBody. 바닥/벽 콜라이더와 목표 영역 메쉬가 그 아래에 있어 판과 함께 기운다.
// - balls: 판 위를 굴러다니는 공 두 개(다이나믹, 판의 자식이 아니어야 한다).
// - zones: 목표 영역 메쉬 두 개. 오브젝트의 위치/회전/스케일이 곧 판정 박스다(빌트인 큐브 기준 단위 크기).
//   공이 들어와 있는 동안 activeMaterial, 아니면 idleMaterial을 보여준다.
//   두 영역에 동시에 공이 하나씩 들어가면 클리어. 이후 두 영역은 activeMaterial로 고정되고 공은 계속 자유롭게 움직인다.
// - fallZone: 월드 축 정렬 박스(위치 ± 스케일/2). 공이 여기에 닿으면 공과 판을 처음 상태로 되돌린다.
//
// 물리 이벤트 없이 매 FixedUpdate마다 직접 검사한다.
class TiltMazePuzzle final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    //***** 조작 (2단계에서 석판 스크립트가 사용)
    // 켜져 있는 동안만 입력을 받는다. 꺼지면 판은 마지막 기울기를 유지한다.
    void SetControlled(bool controlled) { m_Controlled = controlled; }
    bool IsControlled() const { return m_Controlled; }
    // 기울기 입력. x: 공이 오른쪽(+X)으로, y: 공이 앞쪽(+Z)으로 구르게 하는 방향. 단위는 도.
    void AddTilt(float rightDegrees, float forwardDegrees);

    bool IsSolved() const { return m_Solved; }
    // 공과 판을 처음 상태로 되돌린다. 클리어 상태는 유지된다.
    void ResetPuzzle();
    // 개발자 모드: 클리어 상태까지 처음으로 되돌린다.
    void DevReset() { m_Solved = false; m_ResetPending = true; }

protected:
    void Start() override;
    void Update() override;
    void FixedUpdate() override;

private:
    struct BallStart
    {
        bool valid = false;
        Canape::PhysicsPose pose{};
    };

    Canape::RigidBodyComponent* GetBody(const Handle<Canape::GameObject>& handle) const;
    bool IsInsideZone(const Canape::GameObject& zone, const Canape::Vector3& point) const;
    bool IsInsideFallZone(const Canape::Vector3& point) const;
    void ShowZone(int index, bool active);
    void ApplyBoardPose();

    //***** Authoring
    Handle<Canape::GameObject> m_Board{};
    Handle<Canape::GameObject> m_BallA{};
    Handle<Canape::GameObject> m_BallB{};
    Handle<Canape::GameObject> m_ZoneA{};
    Handle<Canape::GameObject> m_ZoneB{};
    Handle<Canape::GameObject> m_FallZone{};
    std::string m_IdleMaterial;
    std::string m_ActiveMaterial;
    std::string m_SolvedSound = "Content/Audio/ClearBell.mp3"; // 클리어 순간 2D로 재생
    // 판의 최대 기울기(도).
    float m_MaxTilt = 12.0f;
    // 판 회전의 최대 속도(도/초)와 가속도(도/초^2). 판은 가속해서 움직이다가 목표 기울기 앞에서 감속해 멈춘다.
    // 판이 갑자기 멈추거나 빠르게 들어 올리면 공이 튀어 오르므로 너무 크게 잡지 않는다.
    float m_TiltSpeed = 20.0f;
    float m_TiltAcceleration = 30.0f;
    // 영역 표면 위로 이 높이(m)까지 공의 중심이 있으면 들어온 것으로 본다.
    float m_ZoneHeight = 0.4f;
    // 석판 없이 방향키로 바로 조작(테스트용). 끄면 석판(TiltMazeConsole)이 SetControlled/AddTilt로 조작한다.
    bool m_KeyboardControl = false;
    // 방향키를 누르고 있을 때 초당 목표 기울기 변화(도).
    float m_KeyboardTiltRate = 30.0f;

    //***** Runtime (직렬화 안 함)
    bool m_Controlled = false;
    bool m_Solved = false;
    bool m_HasBase = false;
    Canape::PhysicsPose m_BaseWorldPose{};
    std::array<BallStart, 2> m_BallStarts{};
    std::array<bool, 2> m_ZoneShown{ true, true }; // 첫 갱신에서 반드시 머티리얼을 적용하도록 true로 시작
    float m_TargetPitch = 0.0f; // 판 로컬 X축 회전(도). +면 +Z 쪽이 내려가 공이 +Z로 구른다.
    float m_TargetRoll = 0.0f;  // 판 로컬 Z축 회전(도). -면 +X 쪽이 내려가 공이 +X로 구른다.
    float m_Pitch = 0.0f;
    float m_Roll = 0.0f;
    float m_PitchVelocity = 0.0f; // 도/초
    float m_RollVelocity = 0.0f;
    bool m_ResetPending = false;
};
