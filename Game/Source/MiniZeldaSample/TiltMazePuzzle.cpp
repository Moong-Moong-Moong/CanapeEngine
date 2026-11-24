#include "MiniZeldaSample/TiltMazePuzzle.h"

#include "CNDebug/DebugLog.h"
#include "Math/Rotator.h"

#include <algorithm>
#include <cmath>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<TiltMazePuzzle>("TiltMazePuzzle")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("board", &TiltMazePuzzle::m_Board)
        .property("ballA", &TiltMazePuzzle::m_BallA)
        .property("ballB", &TiltMazePuzzle::m_BallB)
        .property("zoneA", &TiltMazePuzzle::m_ZoneA)
        .property("zoneB", &TiltMazePuzzle::m_ZoneB)
        .property("fallZone", &TiltMazePuzzle::m_FallZone)
        .property("idleMaterial", &TiltMazePuzzle::m_IdleMaterial)
        .property("activeMaterial", &TiltMazePuzzle::m_ActiveMaterial)
        .property("solvedSound", &TiltMazePuzzle::m_SolvedSound)
        .property("maxTilt", &TiltMazePuzzle::m_MaxTilt)
        .property("tiltSpeed", &TiltMazePuzzle::m_TiltSpeed)
        .property("tiltAcceleration", &TiltMazePuzzle::m_TiltAcceleration)
        .property("zoneHeight", &TiltMazePuzzle::m_ZoneHeight)
        .property("keyboardControl", &TiltMazePuzzle::m_KeyboardControl)
        .property("keyboardTiltRate", &TiltMazePuzzle::m_KeyboardTiltRate);
}

namespace
{
    // 판 아래로 이만큼(m) 떨어진 공은 낙하 영역을 놓쳤더라도 리셋한다.
    constexpr float kLostDepth = 30.0f;

    float MoveTowards(float current, float target, float maxStep)
    {
        if (std::fabs(target - current) <= maxStep) return target;
        return current + (target > current ? maxStep : -maxStep);
    }

    // 가속도 제한이 있는 추종: 목표까지 남은 거리에서 제때 멈출 수 있는 속도(sqrt(2 a d))와 최대 속도 중 작은 값을
    // 원하는 속도로 잡고, 실제 속도는 가속도만큼씩만 그쪽으로 바꾼다. 출발은 서서히 빨라지고 도착 전에는 감속한다.
    void StepTowards(float& angle, float& velocity, float target, float maxSpeed, float acceleration, float dt)
    {
        const float remaining = target - angle;
        const float stoppingSpeed = std::sqrt(2.0f * acceleration * std::fabs(remaining));
        const float desired = std::copysign(std::min(maxSpeed, stoppingSpeed), remaining);
        velocity = MoveTowards(velocity, desired, acceleration * dt);
        angle += velocity * dt;

        // 목표를 지나쳤거나 거의 도착했으면 그 자리에 멈춘다.
        const float after = target - angle;
        if ((after != 0.0f && std::signbit(after) != std::signbit(remaining))
            || (std::fabs(after) < 1.0e-3f && std::fabs(velocity) < acceleration * dt))
        {
            angle = target;
            velocity = 0.0f;
        }
    }

    PhysicsPose WorldPoseOf(const TransformComponent& transform)
    {
        return { transform.GetWorldPosition(), transform.GetWorldRotation().Normalized() };
    }
}

RigidBodyComponent* TiltMazePuzzle::GetBody(const Handle<GameObject>& handle) const
{
    GameObject* object = GetGameObject(handle);
    return object ? object->GetComponent<RigidBodyComponent>() : nullptr;
}

void TiltMazePuzzle::Start()
{
    // 석판 없이 테스트할 때(keyboardControl)는 처음부터 조작 가능. 아니면 석판이 SetControlled로 켠다.
    m_Controlled = m_KeyboardControl;

    if (const GameObject* board = GetGameObject(m_Board))
    {
        m_BaseWorldPose = WorldPoseOf(*board->GetTransform());
        m_HasBase = true;
    }
    else
    {
        CN_LOG_GAME("TiltMaze", "'", GetOwnerObject()->GetName(), "': board is not set");
    }

    const Handle<GameObject>* balls[2] = { &m_BallA, &m_BallB };
    for (int i = 0; i < 2; ++i)
    {
        if (const GameObject* ball = GetGameObject(*balls[i]))
        {
            m_BallStarts[i] = { true, WorldPoseOf(*ball->GetTransform()) };
        }
    }
}

void TiltMazePuzzle::AddTilt(float rightDegrees, float forwardDegrees)
{
    // 공을 +Z로 굴리려면 +Z 쪽을 내린다(+pitch), +X로 굴리려면 +X 쪽을 내린다(-roll).
    m_TargetPitch = std::clamp(m_TargetPitch + forwardDegrees, -m_MaxTilt, m_MaxTilt);
    m_TargetRoll = std::clamp(m_TargetRoll - rightDegrees, -m_MaxTilt, m_MaxTilt);
}

void TiltMazePuzzle::Update()
{
    if (!m_Controlled)
    {
        return;
    }

    const InputManager& input = Application::Get().GetInputManager();
    if (m_KeyboardControl)
    {
        const float step = m_KeyboardTiltRate * Time::DeltaTime();
        float right = 0.0f;
        float forward = 0.0f;
        if (input.IsKeyDown(Key::Right)) right += step;
        if (input.IsKeyDown(Key::Left)) right -= step;
        if (input.IsKeyDown(Key::Up)) forward += step;
        if (input.IsKeyDown(Key::Down)) forward -= step;
        AddTilt(right, forward);
    }

    // 수동 리셋 (조작 중일 때만)
    if (input.IsKeyPressed(Key::R))
    {
        m_ResetPending = true;
    }
}

void TiltMazePuzzle::ResetPuzzle()
{
    const Handle<GameObject>* balls[2] = { &m_BallA, &m_BallB };
    for (int i = 0; i < 2; ++i)
    {
        if (RigidBodyComponent* body = GetBody(*balls[i]); body && m_BallStarts[i].valid)
        {
            body->Teleport(m_BallStarts[i].pose);
        }
    }

    m_TargetPitch = m_TargetRoll = 0.0f;
    m_Pitch = m_Roll = 0.0f;
    m_PitchVelocity = m_RollVelocity = 0.0f;
    if (RigidBodyComponent* board = GetBody(m_Board); board && m_HasBase)
    {
        board->Teleport(m_BaseWorldPose);
    }
}

void TiltMazePuzzle::ApplyBoardPose()
{
    RigidBodyComponent* board = GetBody(m_Board);
    if (!board || !m_HasBase)
    {
        return;
    }
    // 기울기는 판 자신의 축 기준: 배치된 회전 뒤에 로컬 기울기를 적용한다.
    const Quaternion tilt = Rotator{ m_Pitch, 0.0f, m_Roll }.ToQuaternion();
    board->SetKinematicTarget({ m_BaseWorldPose.position, (m_BaseWorldPose.rotation * tilt).Normalized() });
}

bool TiltMazePuzzle::IsInsideZone(const GameObject& zone, const Vector3& point) const
{
    const TransformComponent& transform = *zone.GetTransform();
    const Vector3 local = transform.GetWorldRotation().Inverse().Rotate(point - transform.GetWorldPosition());
    const Vector3 half = transform.GetLossyScale() * 0.5f;
    return std::fabs(local.X) <= std::fabs(half.X)
        && std::fabs(local.Z) <= std::fabs(half.Z)
        && local.Y >= -std::fabs(half.Y)
        && local.Y <= std::fabs(half.Y) + m_ZoneHeight;
}

bool TiltMazePuzzle::IsInsideFallZone(const Vector3& point) const
{
    if (m_HasBase && point.Y < m_BaseWorldPose.position.Y - kLostDepth)
    {
        return true;
    }
    const GameObject* fallZone = GetGameObject(m_FallZone);
    if (!fallZone)
    {
        return false;
    }
    const TransformComponent& transform = *fallZone->GetTransform();
    const Vector3 center = transform.GetWorldPosition();
    const Vector3 half = transform.GetLossyScale() * 0.5f;
    return std::fabs(point.X - center.X) <= std::fabs(half.X)
        && std::fabs(point.Y - center.Y) <= std::fabs(half.Y)
        && std::fabs(point.Z - center.Z) <= std::fabs(half.Z);
}

void TiltMazePuzzle::ShowZone(int index, bool active)
{
    if (m_ZoneShown[index] == active)
    {
        return;
    }
    const Handle<GameObject>& handle = index == 0 ? m_ZoneA : m_ZoneB;
    GameObject* zone = GetGameObject(handle);
    auto* renderer = zone ? zone->GetComponent<MeshRendererComponent>() : nullptr;
    const std::string& material = active ? m_ActiveMaterial : m_IdleMaterial;
    if (renderer && !material.empty())
    {
        renderer->SetMaterial(0, material);
    }
    m_ZoneShown[index] = active;
}

void TiltMazePuzzle::FixedUpdate()
{
    if (m_ResetPending)
    {
        m_ResetPending = false;
        ResetPuzzle();
    }

    // 판 기울이기: 목표 기울기를 향해 가속/감속하며 움직인다(최대 tiltSpeed).
    const float dt = Time::FixedDeltaTime();
    const float acceleration = std::max(m_TiltAcceleration, 1.0e-3f);
    StepTowards(m_Pitch, m_PitchVelocity, m_TargetPitch, m_TiltSpeed, acceleration, dt);
    StepTowards(m_Roll, m_RollVelocity, m_TargetRoll, m_TiltSpeed, acceleration, dt);
    ApplyBoardPose();

    // 공 위치 검사
    const GameObject* balls[2] = { GetGameObject(m_BallA), GetGameObject(m_BallB) };
    const GameObject* zones[2] = { GetGameObject(m_ZoneA), GetGameObject(m_ZoneB) };
    bool inZone[2][2] = {}; // [ball][zone]
    for (int b = 0; b < 2; ++b)
    {
        if (!balls[b]) continue;
        const Vector3 position = balls[b]->GetTransform()->GetWorldPosition();
        if (IsInsideFallZone(position))
        {
            ResetPuzzle();
            return;
        }
        for (int z = 0; z < 2; ++z)
        {
            inZone[b][z] = zones[z] && IsInsideZone(*zones[z], position);
        }
    }

    // 두 영역에 서로 다른 공이 동시에 있으면 클리어(이후 고정).
    if (!m_Solved && ((inZone[0][0] && inZone[1][1]) || (inZone[0][1] && inZone[1][0])))
    {
        m_Solved = true;
        if (AudioSystem* audio = AudioSystem::Get()) audio->Play2D(m_SolvedSound);
        CN_LOG_GAME("TiltMaze", "'", GetOwnerObject()->GetName(), "' solved");
    }

    for (int z = 0; z < 2; ++z)
    {
        ShowZone(z, m_Solved || inZone[0][z] || inZone[1][z]);
    }
}
