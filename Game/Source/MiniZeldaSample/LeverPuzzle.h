#pragma once

#include <Engine.h>

#include <array>
#include <string>

// 지렛대 퍼즐 루트. 돌 두 개를 들어 옮기는 기능, 타겟 판정, 리셋을 맡는다.
// 돌 조종 입력은 석판(LeverConsole)이 넣는다. 물리 이벤트 없이 매 FixedUpdate마다 검사한다.
//
// - 돌을 집으면(TryPick) pickLift만큼 살짝 떠오르고, SetCarryMove 방향으로 수평 이동, SetCarryVertical로
//   오르내린다(높이는 퍼즐 루트 기준 minCarryHeight ~ maxCarryHeight).
//   들고 있는 동안은 매 스텝 Teleport로 위치를 정하므로 중력을 받지 않는다. 놓으면(EndCarry) 그대로 떨어진다.
// - 들고 다닐 수 있는 범위는 carryCenter 주변 carryRadius로 제한된다(타겟까지 직접 들고 갈 수 없게).
// - target: 멀리 세워진 과녁. 작은 돌이 그 박스(오브젝트 위치/회전/스케일, 사방으로 targetMargin만큼 넓힘)에
//   닿으면 클리어. 과녁 메쉬는 idleMaterial(회색)에서 activeMaterial(노란색)로 바뀌고 그대로 유지된다.
// - viewCamera: 들고 있지 않은 돌이 이 카메라 화면 밖에 offscreenResetTime초 동안 있으면 제자리로 돌아온다.
class LeverPuzzle final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    //***** 석판에서 호출
    // 레이에 처음 맞은 바디가 돌이면 그 돌을 집는다.
    bool TryPick(const Canape::Vector3& origin, const Canape::Vector3& direction);
    // 수평 이동 방향(월드, 길이 0~1). 집고 있는 동안만 의미가 있다.
    void SetCarryMove(const Canape::Vector3& direction) { m_CarryMove = direction; }
    // 오르내림 입력(-1 내리기 ~ +1 올리기). 집고 있는 동안만 의미가 있다.
    void SetCarryVertical(float input) { m_CarryVertical = input; }
    void EndCarry();
    bool IsCarrying() const { return m_Carried >= 0; }
    // 돌을 처음 자리로 되돌린다. 클리어 상태는 유지된다.
    void ResetStones();

    bool IsSolved() const { return m_Solved; }
    // 개발자 모드: 클리어 상태까지 처음으로 되돌린다(나무는 LeverTree::ResetTree).
    void DevReset() { m_Solved = false; ResetStones(); }

protected:
    void Start() override;
    void FixedUpdate() override;

private:
    struct StoneStart
    {
        bool valid = false;
        Canape::PhysicsPose pose{};
    };

    Canape::GameObject* GetStone(int index) const;
    Canape::RigidBodyComponent* GetStoneBody(int index) const;
    bool IsInsideTarget(const Canape::Vector3& point) const;
    bool IsOnScreen(const Canape::Vector3& point) const;
    void ResetStone(int index);
    void ShowTarget(bool active);

    //***** Authoring
    Handle<Canape::GameObject> m_BigStone{};
    Handle<Canape::GameObject> m_SmallStone{};
    Handle<Canape::GameObject> m_Target{};
    Handle<Canape::GameObject> m_CarryCenter{};
    Handle<Canape::GameObject> m_ViewCamera{};
    std::string m_IdleMaterial;
    std::string m_ActiveMaterial;
    std::string m_SolvedSound = "Content/Audio/ClearBell.mp3"; // 클리어 순간 2D로 재생
    float m_PickLift = 0.4f;         // 집는 순간 떠오르는 높이(m)
    float m_VerticalSpeed = 3.0f;    // Q/E로 오르내리는 속도(m/s)
    float m_MinCarryHeight = 0.5f;   // 들고 있을 때 돌 중심의 최저/최고 높이(퍼즐 루트 기준, m)
    float m_MaxCarryHeight = 10.0f;  // 큰 돌은 7 m쯤에서 떨어뜨려야 작은 돌이 과녁까지 날아간다
    float m_CarryRadius = 8.0f;   // carryCenter에서 수평으로 이 거리까지만 들고 갈 수 있다
    float m_MoveSpeed = 4.0f;     // 수평 이동 속도(m/s)
    float m_LiftSpeed = 6.0f;     // 들어 올리는 속도(m/s)
    float m_TargetMargin = 0.6f;  // 과녁 박스를 사방으로 넓히는 거리(m). 과녁에 맞고 튕겨도 닿은 것으로 센다
    float m_OffscreenResetTime = 3.0f; // 화면 밖에 이 시간(초) 이상 있으면 제자리로
    float m_LostDepth = 20.0f;    // 퍼즐 루트보다 이만큼 아래로 떨어진 돌은 제자리로

    //***** Runtime (직렬화 안 함)
    std::array<StoneStart, 2> m_StoneStarts{};
    std::array<float, 2> m_OffscreenTime{};
    int m_Carried = -1;
    Canape::PhysicsPose m_CarryPose{};
    Canape::Vector3 m_CarryMove{};
    float m_CarryVertical = 0.0f;
    float m_CarryTargetY = 0.0f; // 들고 있는 돌이 향하는 높이(월드)
    bool m_Solved = false;
    bool m_TargetShown = true; // 첫 갱신에서 머티리얼을 반드시 적용
};
