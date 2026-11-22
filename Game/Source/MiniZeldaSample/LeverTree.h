#pragma once

#include <Engine.h>

#include <string>
#include <vector>

// 검으로 베는 나무. 일정 횟수 맞으면 정해진 방향(받침돌 위)으로 쓰러져 지렛대가 된다.
//
// - trunk: 서 있는 나무 메쉬(모델). 맞을 때마다 hitMaterial로 잠깐 바뀌었다 원래 머티리얼로 돌아온다.
// - leaves: (선택) 따로 둔 잎 메쉬들의 부모. 리셋 때 다시 보이게 할 뿐, 쓰러질 때 숨기지 않는다.
// - pivot: 줄기 밑동의 회전축 오브젝트(trunk는 그 자식). 쓰러질 때 pivot이 자기 로컬 fallAxis를 축으로
//   넘어가는 막대처럼 점점 빨라지며(각가속도 = fallAcceleration * sin(각도)) fallAngle까지 회전한다.
// - lever: 처음에는 비활성인 지렛대(다이나믹 RigidBody + Hinge). 쓰러진 줄기가 받침돌에 걸친 자세와 같은
//   자세로 배치해 두고, 다 쓰러지면 trunk를 숨기고 lever를 켠다.
// - 맞았는지는 플레이어의 StateMachine이 AttackState일 때, 한 번 휘두를 때마다(애니메이션 클립이 새로 시작될 때)
//   그 휘두름이 hitMoment에 이르는 순간 플레이어가 hitRadius 안에 있으면 1회로 센다. 물리 이벤트는 쓰지 않는다.
class LeverTree final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    bool IsFelled() const { return m_State != State::Standing; }
    // 개발자 모드: 다시 서 있는 나무로 되돌리고 지렛대를 없앤다.
    void ResetTree();

protected:
    void Start() override;
    void Update() override;

private:
    enum class State { Standing, Falling, Fallen };

    void DetectHit();
    void RegisterHit();
    void StartFall();
    void UpdateFall();
    void SetHitMaterial(bool hit);
    Canape::GameObject* GetPlayer();

    //***** Authoring
    Handle<Canape::GameObject> m_Trunk{};
    Handle<Canape::GameObject> m_Pivot{};
    Handle<Canape::GameObject> m_Leaves{};
    Handle<Canape::GameObject> m_Lever{};
    std::string m_HitMaterial;
    std::string m_HitSound = "Content/Audio/AxeChop.wav"; // 맞을 때마다 나무 위치에서 3D로 재생
    float m_HitSoundVolume = 1.0f;
    int m_HitsToFell = 3;
    float m_FlashTime = 0.15f;      // 빨갛게 보이는 시간(초)
    Canape::Vector3 m_FallAxis{ 0.0f, 0.0f, 1.0f }; // pivot 로컬 회전축
    float m_FallAngle = 61.35f;     // 다 쓰러졌을 때의 회전(도). 받침돌에 걸친 지렛대 자세와 맞춘다
    float m_FallAcceleration = 3.0f; // 넘어가는 빠르기(1/s^2). 클수록 빨리 쓰러진다
    float m_FallStartAngle = 3.0f;  // 쓰러지기 시작할 때 이미 기울어 있는 각도(도)
    std::string m_PlayerName = "Player";
    std::string m_AnimatorName = "Knight"; // 공격 애니메이션이 재생되는 오브젝트
    float m_HitRadius = 2.5f;       // 줄기에서 이 거리(수평, m) 안에서 휘둘러야 맞는다
    float m_HitMoment = 0.35f;      // 한 번 휘두르는 애니메이션 중 맞는 순간(0~1)

    //***** Runtime (직렬화 안 함)
    State m_State = State::Standing;
    int m_Hits = 0;
    float m_FlashTimer = 0.0f;
    std::vector<std::string> m_TrunkMaterials; // 나무 메쉬의 원래 오버라이드(비어 있으면 모델 기본 머티리얼)
    Handle<Canape::GameObject> m_Player{};
    bool m_WasAttacking = false;
    std::string m_SwingClip;
    float m_LastAnimTime = 0.0f;
    bool m_SwingCounted = false;
    Canape::Quaternion m_PivotStart = Canape::Quaternion::Identity;
    Canape::Quaternion m_PivotAuthored = Canape::Quaternion::Identity;
    Canape::Vector3 m_LeverPosition{};   // 배치된 지렛대의 로컬 자세(리셋용)
    Canape::Quaternion m_LeverRotation = Canape::Quaternion::Identity;
    float m_FallRadians = 0.0f;
    float m_FallSpeed = 0.0f; // rad/s
};
