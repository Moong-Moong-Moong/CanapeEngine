#include "MiniZeldaSample/LeverTree.h"

#include "CNDebug/DebugLog.h"
#include "FSM/StateMachine.h"
#include "Scene/Component/AnimatorComponent.h"

#include <algorithm>
#include <cmath>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<LeverTree>("LeverTree")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("trunk", &LeverTree::m_Trunk)
        .property("pivot", &LeverTree::m_Pivot)
        .property("leaves", &LeverTree::m_Leaves)
        .property("lever", &LeverTree::m_Lever)
        .property("hitMaterial", &LeverTree::m_HitMaterial)
        .property("hitSound", &LeverTree::m_HitSound)
        .property("hitSoundVolume", &LeverTree::m_HitSoundVolume)
        .property("hitsToFell", &LeverTree::m_HitsToFell)
        .property("flashTime", &LeverTree::m_FlashTime)
        .property("fallAxis", &LeverTree::m_FallAxis)
        .property("fallAngle", &LeverTree::m_FallAngle)
        .property("fallAcceleration", &LeverTree::m_FallAcceleration)
        .property("fallStartAngle", &LeverTree::m_FallStartAngle)
        .property("playerName", &LeverTree::m_PlayerName)
        .property("animatorName", &LeverTree::m_AnimatorName)
        .property("hitRadius", &LeverTree::m_HitRadius)
        .property("hitMoment", &LeverTree::m_HitMoment);
}

namespace
{
    constexpr float kDegToRad = 0.017453292f;

    Quaternion AxisAngle(const Vector3& axis, float radians)
    {
        const Vector3 unit = axis.IsNearlyZero() ? Vector3{ 0.0f, 0.0f, 1.0f } : axis.Normalized();
        return Quaternion::FromXM(DirectX::XMQuaternionRotationAxis(unit.ToXM(), radians));
    }

    void SetActiveRecursive(GameObject& object, bool active)
    {
        object.SetActive(active);
        for (TransformComponent* child : object.GetTransform()->GetChildren())
        {
            if (child && child->GetOwnerObject()) SetActiveRecursive(*child->GetOwnerObject(), active);
        }
    }

    void SetVisibleRecursive(GameObject& object, bool visible)
    {
        for (MeshRendererComponent* renderer : object.GetComponents<MeshRendererComponent>())
        {
            renderer->SetVisible(visible);
        }
        for (TransformComponent* child : object.GetTransform()->GetChildren())
        {
            if (child && child->GetOwnerObject()) SetVisibleRecursive(*child->GetOwnerObject(), visible);
        }
    }
}

void LeverTree::Start()
{
    m_Player = FindGameObjectHandleByName(m_PlayerName);

    if (GameObject* trunk = GetGameObject(m_Trunk))
    {
        if (const auto* renderer = trunk->GetComponent<MeshRendererComponent>())
        {
            m_TrunkMaterials = renderer->GetMaterialNames();
        }
    }

    if (GameObject* pivot = GetGameObject(m_Pivot))
    {
        m_PivotAuthored = pivot->GetTransform()->GetRotation();
    }

    // 지렛대는 나무가 쓰러지기 전까지 없다(물리 바디/힌지도 만들어지지 않는다).
    if (GameObject* lever = GetGameObject(m_Lever))
    {
        m_LeverPosition = lever->GetTransform()->GetPosition();
        m_LeverRotation = lever->GetTransform()->GetRotation();
        SetActiveRecursive(*lever, false);
    }
}

void LeverTree::ResetTree()
{
    // 지렛대를 끄고(바디/힌지 해제) 배치된 자세로 되돌려 둔다. 다시 켜질 때 그 자세에서 힌지가 만들어진다.
    if (GameObject* lever = GetGameObject(m_Lever))
    {
        SetActiveRecursive(*lever, false);
        lever->GetTransform()->SetPosition(m_LeverPosition);
        lever->GetTransform()->SetRotation(m_LeverRotation);
    }
    if (GameObject* pivot = GetGameObject(m_Pivot))
    {
        pivot->GetTransform()->SetRotation(m_PivotAuthored);
    }
    if (GameObject* trunk = GetGameObject(m_Trunk))
    {
        SetVisibleRecursive(*trunk, true);
    }
    if (GameObject* leaves = GetGameObject(m_Leaves))
    {
        SetVisibleRecursive(*leaves, true);
    }
    SetHitMaterial(false);
    m_State = State::Standing;
    m_Hits = 0;
    m_FlashTimer = 0.0f;
    m_FallSpeed = 0.0f;
    m_WasAttacking = false;
}

GameObject* LeverTree::GetPlayer()
{
    GameObject* player = GetGameObject(m_Player);
    if (!player)
    {
        m_Player = FindGameObjectHandleByName(m_PlayerName);
        player = GetGameObject(m_Player);
    }
    return player;
}

void LeverTree::SetHitMaterial(bool hit)
{
    GameObject* trunk = GetGameObject(m_Trunk);
    auto* renderer = trunk ? trunk->GetComponent<MeshRendererComponent>() : nullptr;
    if (!renderer)
    {
        return;
    }
    if (hit)
    {
        if (!m_HitMaterial.empty()) renderer->SetMaterial(0, m_HitMaterial);
    }
    else
    {
        // 원래 오버라이드 목록으로 되돌린다. 비어 있으면 모델 기본 머티리얼(텍스처)이 다시 쓰인다.
        renderer->SetMaterialNames(m_TrunkMaterials);
    }
}

void LeverTree::DetectHit()
{
    GameObject* player = GetPlayer();
    const auto* stateMachine = player ? player->GetComponent<StateMachine>() : nullptr;
    const bool attacking = stateMachine && stateMachine->IsStateActive("AttackState");
    if (!attacking)
    {
        m_WasAttacking = false;
        return;
    }

    GameObject* animatorObject = FindGameObjectByName(m_AnimatorName);
    const auto* animator = animatorObject ? animatorObject->GetComponent<AnimatorComponent>() : nullptr;
    if (!animator)
    {
        return;
    }

    // 새 휘두름: 공격을 막 시작했거나, 콤보로 다른 클립이 시작됐거나, 같은 클립이 처음부터 다시 재생될 때.
    const float time = animator->GetTime();
    if (!m_WasAttacking || animator->GetClipPath() != m_SwingClip || time + 1.0e-4f < m_LastAnimTime)
    {
        m_SwingClip = animator->GetClipPath();
        m_SwingCounted = false;
    }
    m_WasAttacking = true;
    m_LastAnimTime = time;

    const float duration = animator->GetDuration();
    const float progress = duration > 0.0f ? time / duration : 1.0f;
    if (m_SwingCounted || progress < m_HitMoment)
    {
        return;
    }
    m_SwingCounted = true;

    GameObject* trunk = GetGameObject(m_Trunk);
    const Vector3 trunkPosition = trunk ? trunk->GetTransform()->GetWorldPosition() : GetTransform()->GetWorldPosition();
    const Vector3 offset = player->GetTransform()->GetWorldPosition() - trunkPosition;
    if (offset.X * offset.X + offset.Z * offset.Z <= m_HitRadius * m_HitRadius)
    {
        RegisterHit();
    }
}

void LeverTree::RegisterHit()
{
    ++m_Hits;
    m_FlashTimer = m_FlashTime;
    SetHitMaterial(true);
    if (AudioSystem* audio = AudioSystem::Get(); audio && !m_HitSound.empty())
    {
        GameObject* trunk = GetGameObject(m_Trunk);
        const Vector3 position = trunk ? trunk->GetTransform()->GetWorldPosition() : GetTransform()->GetWorldPosition();
        audio->PlayOneShot(m_HitSound, position, m_HitSoundVolume, 1.0f, 3.0f, 40.0f);
    }
    CN_LOG_GAME("LeverTree", "hit ", m_Hits, "/", m_HitsToFell);
    if (m_Hits >= m_HitsToFell)
    {
        StartFall();
    }
}

void LeverTree::StartFall()
{
    GameObject* pivot = GetGameObject(m_Pivot);
    if (!pivot || !GetGameObject(m_Trunk))
    {
        CN_LOG_GAME("LeverTree", "'", GetOwnerObject()->GetName(), "': trunk or pivot is not set");
        return;
    }

    m_PivotStart = pivot->GetTransform()->GetRotation();
    m_FallRadians = std::max(m_FallStartAngle, 0.1f) * kDegToRad;
    m_FallSpeed = 0.0f;
    m_State = State::Falling;
}

void LeverTree::UpdateFall()
{
    GameObject* pivot = GetGameObject(m_Pivot);
    GameObject* trunk = GetGameObject(m_Trunk);
    if (!pivot || !trunk)
    {
        return;
    }

    // 밑동을 축으로 넘어가는 막대: 기울수록 각가속도가 커진다(sin 각도에 비례).
    const float dt = Time::DeltaTime();
    const float endRadians = m_FallAngle * kDegToRad;
    m_FallSpeed += m_FallAcceleration * std::sin(m_FallRadians) * dt;
    m_FallRadians = std::min(m_FallRadians + m_FallSpeed * dt, endRadians);
    // 로컬 축 기준 회전: 시작 회전 뒤에 축 회전을 붙인다.
    pivot->GetTransform()->SetRotation((m_PivotStart * AxisAngle(m_FallAxis, m_FallRadians)).Normalized());

    if (m_FallRadians >= endRadians)
    {
        // 받침돌에 걸친 줄기를 물리 지렛대로 바꾼다(지렛대는 같은 자세로 배치되어 있다).
        SetVisibleRecursive(*trunk, false);
        if (GameObject* lever = GetGameObject(m_Lever))
        {
            SetActiveRecursive(*lever, true);
        }
        m_State = State::Fallen;
    }
}

void LeverTree::Update()
{
    if (m_FlashTimer > 0.0f)
    {
        m_FlashTimer -= Time::DeltaTime();
        if (m_FlashTimer <= 0.0f)
        {
            SetHitMaterial(false);
        }
    }

    switch (m_State)
    {
    case State::Standing: DetectHit(); break;
    case State::Falling: UpdateFall(); break;
    case State::Fallen: break;
    }
}
