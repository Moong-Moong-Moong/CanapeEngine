#pragma once

#include <Engine.h>

#include <string>
#include <vector>

// 자판기처럼 세워진 구슬 굴리기 퍼즐. 퍼즐 루트에 붙인다.
//
// - 공이 맨 위에서 떨어져 지그재그 내리막을 내려간다. 각 내리막에는 구멍이 3개 있다.
// - 구멍을 메우는 홈(bridge)은 두 묶음이다: 이름이 "_A"로 끝나는 홈(1, 3번째 구멍)과 "_B"로 끝나는 홈(2번째 구멍).
//   Swap()을 할 때마다 A가 나오고 B가 들어가거나, 그 반대가 된다. 들어간 홈은 뒤판 뒤로 slideDistance만큼 물러난다.
//   홈은 퍼즐 루트 기준 z = 0(길 위치)에 배치해 두면 된다.
// - 죽는 영역: deathZones 아래의 "DeathZone_L{층}_G{구멍}" 박스(반투명 빨강). 같은 번호의 홈
//   ("Bridge_L{층}_G{구멍}_A/B")이 나와 있지 않을 때 공의 중심이 이 박스 안에 들어오면 떨어진 것이다.
//   박스는 구멍 바로 아래 얕게 두어, 아래층 길을 굴러가는 공은 닿지 않는다.
//   떨어지면 잠시 뒤 처음부터 다시 굴러간다.
// - 공이 goal 영역에 들어오면 클리어. goal 메쉬가 idleMaterial(회색)에서 activeMaterial(노란색)로 바뀌고 유지된다.
// - 플레이 중(SetPlaying)에는 followCamera가 공을 앞에서 따라간다.
// 물리 이벤트 없이 매 FixedUpdate마다 검사한다.
class MarbleRun final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    //***** 석판에서 호출
    // 켜면 공을 처음 자리에서 releaseDelay초 뒤에 굴린다. 끄면 공을 처음 자리에 붙잡아 둔다.
    void SetPlaying(bool playing);
    // 홈 묶음을 바꾼다(A <-> B).
    void Swap() { m_StateB = !m_StateB; }
    bool IsSolved() const { return m_Phase == Phase::Solved; }
    // 개발자 모드: 클리어 상태까지 처음으로 되돌린다.
    void DevReset();

protected:
    void Start() override;
    void FixedUpdate() override;
    void LateUpdate() override;

private:
    enum class Phase { Holding, Waiting, Rolling, Failed, Solved };

    struct Bridge
    {
        Handle<Canape::GameObject> object{};
        bool groupB = false;
        Canape::Vector3 extended{};   // 부모 기준
        Canape::Vector3 retracted{};
        Canape::Vector3 current{};
        Canape::Quaternion worldRotation{};
        Handle<Canape::GameObject> deathZone{}; // 이 홈이 메우는 구멍의 죽는 영역
    };

    void CollectBridges();
    void MoveBridges(float dt);
    void HoldBall();
    void Restart();
    bool FellThroughGap(const Canape::Vector3& ballWorld) const;
    bool IsInsideGoal(const Canape::Vector3& ball) const;
    void ShowGoal(bool active);
    Canape::Vector3 ToRootLocal(const Canape::Vector3& world) const;

    //***** Authoring
    Handle<Canape::GameObject> m_Ball{};
    Handle<Canape::GameObject> m_Bridges{};      // 홈들의 부모
    Handle<Canape::GameObject> m_DeathZones{};   // 죽는 영역 박스들의 부모
    Handle<Canape::GameObject> m_Goal{};
    Handle<Canape::GameObject> m_StartPoint{};
    Handle<Canape::GameObject> m_FollowCamera{};
    std::string m_IdleMaterial;
    std::string m_ActiveMaterial;
    std::string m_SolvedSound = "Content/Audio/ClearBell.mp3"; // 클리어 순간 2D로 재생
    float m_SlideDistance = 0.5f;   // 홈이 뒤로 물러나는 거리(m)
    float m_SlideSpeed = 5.0f;      // 홈이 나오고 들어가는 속도(m/s)
    float m_ReleaseDelay = 1.0f;    // 플레이를 시작하고 공을 놓기까지(초)
    float m_FailDelay = 0.8f;       // 떨어진 뒤 다시 시작하기까지(초)
    float m_GoalHeight = 0.4f;      // goal 표면 위로 판정하는 높이(m)
    float m_CameraDistance = 3.0f;  // 공 앞(퍼즐 루트 +Z) 카메라 거리(m)
    float m_CameraHeight = 0.3f;    // 공보다 위로(m)
    float m_CameraSharpness = 5.0f; // 따라가는 빠르기(1/s)

    //***** Runtime (직렬화 안 함)
    std::vector<Bridge> m_BridgeList;
    bool m_StateB = false;
    bool m_Playing = false;
    Phase m_Phase = Phase::Holding;
    float m_Timer = 0.0f;
    Canape::PhysicsPose m_StartPose{};
    bool m_GoalShown = true; // 첫 갱신에서 머티리얼을 반드시 적용
    bool m_CameraSnapped = false;
};
