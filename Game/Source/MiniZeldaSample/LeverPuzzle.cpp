#include "MiniZeldaSample/LeverPuzzle.h"

#include "CNDebug/DebugLog.h"
#include "Renderer/Renderer.h"

#include <DirectXMath.h>

#include <algorithm>
#include <cmath>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<LeverPuzzle>("LeverPuzzle")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("bigStone", &LeverPuzzle::m_BigStone)
        .property("smallStone", &LeverPuzzle::m_SmallStone)
        .property("target", &LeverPuzzle::m_Target)
        .property("carryCenter", &LeverPuzzle::m_CarryCenter)
        .property("viewCamera", &LeverPuzzle::m_ViewCamera)
        .property("idleMaterial", &LeverPuzzle::m_IdleMaterial)
        .property("activeMaterial", &LeverPuzzle::m_ActiveMaterial)
        .property("solvedSound", &LeverPuzzle::m_SolvedSound)
        .property("pickLift", &LeverPuzzle::m_PickLift)
        .property("verticalSpeed", &LeverPuzzle::m_VerticalSpeed)
        .property("minCarryHeight", &LeverPuzzle::m_MinCarryHeight)
        .property("maxCarryHeight", &LeverPuzzle::m_MaxCarryHeight)
        .property("carryRadius", &LeverPuzzle::m_CarryRadius)
        .property("moveSpeed", &LeverPuzzle::m_MoveSpeed)
        .property("liftSpeed", &LeverPuzzle::m_LiftSpeed)
        .property("targetMargin", &LeverPuzzle::m_TargetMargin)
        .property("offscreenResetTime", &LeverPuzzle::m_OffscreenResetTime)
        .property("lostDepth", &LeverPuzzle::m_LostDepth);
}

namespace
{
    constexpr float kMaxPickDistance = 200.0f;
}

bool LeverPuzzle::IsOnScreen(const Vector3& point) const
{
    GameObject* cameraObject = GetGameObject(m_ViewCamera);
    const CameraComponent* camera = cameraObject ? cameraObject->GetComponent<CameraComponent>() : nullptr;
    const Renderer& renderer = Application::Get().GetRenderer();
    if (!camera || renderer.GetWidth() == 0 || renderer.GetHeight() == 0)
    {
        return true; // 판정할 수 없으면 리셋하지 않는다
    }
    const float aspect = static_cast<float>(renderer.GetWidth()) / static_cast<float>(renderer.GetHeight());
    const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(
        DirectX::XMVectorSet(point.X, point.Y, point.Z, 1.0f), camera->GetViewProjectionMatrix(aspect).ToXM());
    const float w = DirectX::XMVectorGetW(clip);
    if (w <= 0.0f)
    {
        return false; // 카메라 뒤
    }
    const float x = DirectX::XMVectorGetX(clip) / w;
    const float y = DirectX::XMVectorGetY(clip) / w;
    return std::fabs(x) <= 1.0f && std::fabs(y) <= 1.0f;
}

void LeverPuzzle::ResetStone(int index)
{
    if (m_Carried == index)
    {
        EndCarry();
    }
    if (RigidBodyComponent* body = GetStoneBody(index); body && m_StoneStarts[index].valid)
    {
        body->Teleport(m_StoneStarts[index].pose);
    }
    m_OffscreenTime[index] = 0.0f;
}

GameObject* LeverPuzzle::GetStone(int index) const
{
    return GetGameObject(index == 0 ? m_BigStone : m_SmallStone);
}

RigidBodyComponent* LeverPuzzle::GetStoneBody(int index) const
{
    GameObject* stone = GetStone(index);
    return stone ? stone->GetComponent<RigidBodyComponent>() : nullptr;
}

void LeverPuzzle::Start()
{
    for (int i = 0; i < 2; ++i)
    {
        if (const GameObject* stone = GetStone(i))
        {
            const TransformComponent& transform = *stone->GetTransform();
            m_StoneStarts[i] = { true, { transform.GetWorldPosition(), transform.GetWorldRotation().Normalized() } };
        }
    }
}

bool LeverPuzzle::TryPick(const Vector3& origin, const Vector3& direction)
{
    const auto& physics = GetOwnerObject()->GetScene()->GetPhysics();
    const std::optional<PhysicsRayHit> hit = physics.RayCast(origin, direction, kMaxPickDistance);
    if (!hit)
    {
        return false;
    }
    for (int i = 0; i < 2; ++i)
    {
        if (hit->body == GetStoneBody(i))
        {
            const TransformComponent& transform = *GetStone(i)->GetTransform();
            m_Carried = i;
            m_CarryPose = { transform.GetWorldPosition(), transform.GetWorldRotation().Normalized() };
            m_CarryMove = {};
            m_CarryVertical = 0.0f;
            m_CarryTargetY = m_CarryPose.position.Y + m_PickLift;
            return true;
        }
    }
    return false;
}

void LeverPuzzle::EndCarry()
{
    m_Carried = -1;
    m_CarryMove = {};
    m_CarryVertical = 0.0f;
}

void LeverPuzzle::ResetStones()
{
    EndCarry();
    for (int i = 0; i < 2; ++i)
    {
        ResetStone(i);
    }
}

bool LeverPuzzle::IsInsideTarget(const Vector3& point) const
{
    GameObject* target = GetGameObject(m_Target);
    if (!target)
    {
        return false;
    }
    const TransformComponent& transform = *target->GetTransform();
    const Vector3 local = transform.GetWorldRotation().Inverse().Rotate(point - transform.GetWorldPosition());
    const Vector3 half = transform.GetLossyScale() * 0.5f;
    return std::fabs(local.X) <= std::fabs(half.X) + m_TargetMargin
        && std::fabs(local.Y) <= std::fabs(half.Y) + m_TargetMargin
        && std::fabs(local.Z) <= std::fabs(half.Z) + m_TargetMargin;
}

void LeverPuzzle::ShowTarget(bool active)
{
    if (m_TargetShown == active)
    {
        return;
    }
    GameObject* target = GetGameObject(m_Target);
    auto* renderer = target ? target->GetComponent<MeshRendererComponent>() : nullptr;
    const std::string& material = active ? m_ActiveMaterial : m_IdleMaterial;
    if (renderer && !material.empty())
    {
        renderer->SetMaterial(0, material);
    }
    m_TargetShown = active;
}

void LeverPuzzle::FixedUpdate()
{
    const float dt = Time::FixedDeltaTime();
    const float rootY = GetTransform()->GetWorldPosition().Y;

    // 들고 있는 돌: 목표 높이까지 올리고 수평으로 옮긴다.
    if (m_Carried >= 0)
    {
        RigidBodyComponent* body = GetStoneBody(m_Carried);
        if (!body)
        {
            EndCarry();
        }
        else
        {
            Vector3 position = m_CarryPose.position;
            // Q/E 입력으로 목표 높이를 옮기고(범위 제한), 돌은 그 높이를 따라간다.
            m_CarryTargetY = std::clamp(m_CarryTargetY + std::clamp(m_CarryVertical, -1.0f, 1.0f) * m_VerticalSpeed * dt,
                rootY + m_MinCarryHeight, rootY + std::max(m_MaxCarryHeight, m_MinCarryHeight));
            const float targetY = m_CarryTargetY;
            const float lift = m_LiftSpeed * dt;
            position.Y = std::fabs(targetY - position.Y) <= lift ? targetY : position.Y + (targetY > position.Y ? lift : -lift);

            Vector3 move{ m_CarryMove.X, 0.0f, m_CarryMove.Z };
            if (move.Length() > 1.0f) move = move.Normalized();
            position = position + move * (m_MoveSpeed * dt);

            if (const GameObject* center = GetGameObject(m_CarryCenter))
            {
                const Vector3 c = center->GetTransform()->GetWorldPosition();
                Vector3 offset{ position.X - c.X, 0.0f, position.Z - c.Z };
                const float distance = offset.Length();
                if (distance > m_CarryRadius)
                {
                    offset = offset * (m_CarryRadius / distance);
                    position.X = c.X + offset.X;
                    position.Z = c.Z + offset.Z;
                }
            }

            m_CarryPose.position = position;
            body->Teleport(m_CarryPose);
        }
    }

    // 잃어버린 돌은 제자리로: 너무 아래로 떨어졌거나, 들고 있지 않은데 화면 밖에 일정 시간 있었을 때.
    for (int i = 0; i < 2; ++i)
    {
        const GameObject* stone = GetStone(i);
        if (!stone || !m_StoneStarts[i].valid)
        {
            continue;
        }
        const Vector3 position = stone->GetTransform()->GetWorldPosition();
        if (position.Y < rootY - m_LostDepth)
        {
            ResetStone(i);
            continue;
        }
        if (m_Carried == i || IsOnScreen(position))
        {
            m_OffscreenTime[i] = 0.0f;
            continue;
        }
        m_OffscreenTime[i] += dt;
        if (m_OffscreenTime[i] >= m_OffscreenResetTime)
        {
            ResetStone(i);
        }
    }

    // 날아간 작은 돌이 타겟에 들어오면 클리어(들고 있는 동안은 세지 않는다).
    if (!m_Solved && m_Carried != 1)
    {
        if (const GameObject* small = GetStone(1); small && IsInsideTarget(small->GetTransform()->GetWorldPosition()))
        {
            m_Solved = true;
            if (AudioSystem* audio = AudioSystem::Get()) audio->Play2D(m_SolvedSound);
            CN_LOG_GAME("LeverPuzzle", "'", GetOwnerObject()->GetName(), "' solved");
        }
    }
    ShowTarget(m_Solved);
}
