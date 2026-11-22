#include "MiniZeldaSample/MarbleRun.h"

#include "CNDebug/DebugLog.h"
#include "Math/Rotator.h"

#include <cmath>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<MarbleRun>("MarbleRun")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("ball", &MarbleRun::m_Ball)
        .property("bridges", &MarbleRun::m_Bridges)
        .property("deathZones", &MarbleRun::m_DeathZones)
        .property("goal", &MarbleRun::m_Goal)
        .property("startPoint", &MarbleRun::m_StartPoint)
        .property("followCamera", &MarbleRun::m_FollowCamera)
        .property("idleMaterial", &MarbleRun::m_IdleMaterial)
        .property("activeMaterial", &MarbleRun::m_ActiveMaterial)
        .property("solvedSound", &MarbleRun::m_SolvedSound)
        .property("slideDistance", &MarbleRun::m_SlideDistance)
        .property("slideSpeed", &MarbleRun::m_SlideSpeed)
        .property("releaseDelay", &MarbleRun::m_ReleaseDelay)
        .property("failDelay", &MarbleRun::m_FailDelay)
        .property("goalHeight", &MarbleRun::m_GoalHeight)
        .property("cameraDistance", &MarbleRun::m_CameraDistance)
        .property("cameraHeight", &MarbleRun::m_CameraHeight)
        .property("cameraSharpness", &MarbleRun::m_CameraSharpness);
}

namespace
{
    Vector3 MoveTowards(const Vector3& current, const Vector3& target, float maxStep)
    {
        const Vector3 delta = target - current;
        const float distance = delta.Length();
        return distance <= maxStep || distance < 1.0e-6f ? target : current + delta * (maxStep / distance);
    }

    bool EndsWith(const std::string& text, const char* suffix)
    {
        const std::string tail(suffix);
        return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
    }

    // "Bridge_L2_G3_A" / "DeathZone_L2_G3" -> "L2_G3"
    std::string GapKey(const std::string& name)
    {
        const size_t start = name.find("_L");
        if (start == std::string::npos) return {};
        const size_t gap = name.find("_G", start + 2);
        if (gap == std::string::npos) return {};
        size_t end = gap + 2;
        while (end < name.size() && name[end] >= '0' && name[end] <= '9') ++end;
        return name.substr(start + 1, end - start - 1);
    }

    bool IsInsideBox(const TransformComponent& box, const Vector3& world)
    {
        const Vector3 local = box.GetWorldRotation().Inverse().Rotate(world - box.GetWorldPosition());
        const Vector3 half = box.GetLossyScale() * 0.5f;
        return std::fabs(local.X) <= std::fabs(half.X)
            && std::fabs(local.Y) <= std::fabs(half.Y)
            && std::fabs(local.Z) <= std::fabs(half.Z);
    }

    void SetWorldPose(TransformComponent& transform, const Vector3& position, const Quaternion& rotation)
    {
        if (const TransformComponent* parent = transform.GetParent())
        {
            transform.SetPosition(parent->GetWorldToLocalMatrix().TransformPoint(position));
            transform.SetRotation((parent->GetWorldRotation().Inverse() * rotation).Normalized());
        }
        else
        {
            transform.SetPosition(position);
            transform.SetRotation(rotation);
        }
    }
}

Vector3 MarbleRun::ToRootLocal(const Vector3& world) const
{
    return GetTransform()->GetWorldToLocalMatrix().TransformPoint(world);
}

void MarbleRun::CollectBridges()
{
    m_BridgeList.clear();
    GameObject* group = GetGameObject(m_Bridges);
    if (!group)
    {
        CN_LOG_GAME("MarbleRun", "'", GetOwnerObject()->GetName(), "': bridges is not set");
        return;
    }
    for (TransformComponent* child : group->GetTransform()->GetChildren())
    {
        GameObject* object = child ? child->GetOwnerObject() : nullptr;
        if (!object || !object->GetComponent<RigidBodyComponent>())
        {
            continue;
        }
        Bridge bridge;
        bridge.object = object->GetHandle();
        bridge.groupB = EndsWith(object->GetName(), "_B");
        bridge.extended = child->GetPosition();
        bridge.retracted = bridge.extended + Vector3{ 0.0f, 0.0f, -m_SlideDistance };
        bridge.current = bridge.extended;
        bridge.worldRotation = child->GetWorldRotation().Normalized();
        m_BridgeList.push_back(bridge);
    }

    // 같은 구멍 번호의 죽는 영역을 홈에 붙인다.
    GameObject* zones = GetGameObject(m_DeathZones);
    if (!zones)
    {
        CN_LOG_GAME("MarbleRun", "'", GetOwnerObject()->GetName(), "': deathZones is not set");
        return;
    }
    for (TransformComponent* child : zones->GetTransform()->GetChildren())
    {
        GameObject* zone = child ? child->GetOwnerObject() : nullptr;
        const std::string key = zone ? GapKey(zone->GetName()) : std::string();
        if (key.empty()) continue;
        for (Bridge& bridge : m_BridgeList)
        {
            const GameObject* object = GetGameObject(bridge.object);
            if (object && GapKey(object->GetName()) == key)
            {
                bridge.deathZone = zone->GetHandle();
            }
        }
    }
}

void MarbleRun::Start()
{
    if (GameObject* start = GetGameObject(m_StartPoint))
    {
        m_StartPose = { start->GetTransform()->GetWorldPosition(), Quaternion::Identity };
    }
    else if (GameObject* ball = GetGameObject(m_Ball))
    {
        m_StartPose = { ball->GetTransform()->GetWorldPosition(), Quaternion::Identity };
    }

    CollectBridges();
    // 처음 상태(A만 나와 있음)로 바로 맞춘다.
    for (Bridge& bridge : m_BridgeList)
    {
        bridge.current = bridge.groupB ? bridge.retracted : bridge.extended;
        GameObject* object = GetGameObject(bridge.object);
        const TransformComponent* parent = object ? object->GetTransform()->GetParent() : nullptr;
        if (auto* body = object ? object->GetComponent<RigidBodyComponent>() : nullptr; body && parent)
        {
            body->Teleport({ parent->GetLocalToWorldMatrix().TransformPoint(bridge.current), bridge.worldRotation });
        }
    }
    Restart();
    ShowGoal(false);
}

void MarbleRun::SetPlaying(bool playing)
{
    m_Playing = playing;
    m_CameraSnapped = false;
    if (m_Phase == Phase::Solved)
    {
        return;
    }
    Restart();
}

void MarbleRun::DevReset()
{
    m_Phase = Phase::Holding;
    ShowGoal(false);
    Restart();
}

void MarbleRun::Restart()
{
    m_StateB = false;
    HoldBall();
    m_Phase = m_Playing ? Phase::Waiting : Phase::Holding;
    m_Timer = m_ReleaseDelay;
}

void MarbleRun::HoldBall()
{
    GameObject* ball = GetGameObject(m_Ball);
    if (auto* body = ball ? ball->GetComponent<RigidBodyComponent>() : nullptr)
    {
        body->Teleport(m_StartPose);
    }
}

void MarbleRun::MoveBridges(float dt)
{
    for (Bridge& bridge : m_BridgeList)
    {
        GameObject* object = GetGameObject(bridge.object);
        auto* body = object ? object->GetComponent<RigidBodyComponent>() : nullptr;
        const TransformComponent* parent = object ? object->GetTransform()->GetParent() : nullptr;
        if (!body || !parent)
        {
            continue;
        }
        const bool out = bridge.groupB == m_StateB;
        bridge.current = MoveTowards(bridge.current, out ? bridge.extended : bridge.retracted, m_SlideSpeed * dt);
        body->SetKinematicTarget({ parent->GetLocalToWorldMatrix().TransformPoint(bridge.current), bridge.worldRotation });
    }
}

bool MarbleRun::FellThroughGap(const Vector3& ballWorld) const
{
    for (const Bridge& bridge : m_BridgeList)
    {
        // 완전히 나와 있는 홈 위로는 떨어지지 않는다.
        const bool filled = std::fabs(bridge.current.Z - bridge.extended.Z) < 0.05f;
        if (filled)
        {
            continue;
        }
        const GameObject* zone = GetGameObject(bridge.deathZone);
        if (zone && IsInsideBox(*zone->GetTransform(), ballWorld))
        {
            return true;
        }
    }
    return false;
}

bool MarbleRun::IsInsideGoal(const Vector3& ball) const
{
    GameObject* goal = GetGameObject(m_Goal);
    if (!goal)
    {
        return false;
    }
    const TransformComponent& transform = *goal->GetTransform();
    const Vector3 local = transform.GetWorldRotation().Inverse().Rotate(ball - transform.GetWorldPosition());
    const Vector3 half = transform.GetLossyScale() * 0.5f;
    return std::fabs(local.X) <= std::fabs(half.X)
        && std::fabs(local.Z) <= std::fabs(half.Z) + 0.2f
        && local.Y >= -std::fabs(half.Y)
        && local.Y <= std::fabs(half.Y) + m_GoalHeight;
}

void MarbleRun::ShowGoal(bool active)
{
    if (m_GoalShown == active)
    {
        return;
    }
    GameObject* goal = GetGameObject(m_Goal);
    auto* renderer = goal ? goal->GetComponent<MeshRendererComponent>() : nullptr;
    const std::string& material = active ? m_ActiveMaterial : m_IdleMaterial;
    if (renderer && !material.empty())
    {
        renderer->SetMaterial(0, material);
    }
    m_GoalShown = active;
}

void MarbleRun::FixedUpdate()
{
    const float dt = Time::FixedDeltaTime();
    MoveBridges(dt);

    GameObject* ball = GetGameObject(m_Ball);
    switch (m_Phase)
    {
    case Phase::Holding:
        HoldBall();
        break;
    case Phase::Waiting:
        HoldBall();
        m_Timer -= dt;
        if (m_Timer <= 0.0f)
        {
            m_Phase = Phase::Rolling; // 붙잡지 않으면 공이 떨어진다
        }
        break;
    case Phase::Rolling:
        if (ball)
        {
            const Vector3 world = ball->GetTransform()->GetWorldPosition();
            const Vector3 local = ToRootLocal(world);
            if (IsInsideGoal(world))
            {
                m_Phase = Phase::Solved;
                if (AudioSystem* audio = AudioSystem::Get()) audio->Play2D(m_SolvedSound);
                ShowGoal(true);
                CN_LOG_GAME("MarbleRun", "'", GetOwnerObject()->GetName(), "' solved");
            }
            else if (FellThroughGap(world) || local.Y < -1.0f)
            {
                m_Phase = Phase::Failed;
                m_Timer = m_FailDelay;
            }
        }
        break;
    case Phase::Failed:
        m_Timer -= dt;
        if (m_Timer <= 0.0f)
        {
            Restart();
        }
        break;
    case Phase::Solved:
        break;
    }
}

void MarbleRun::LateUpdate()
{
    if (!m_Playing)
    {
        return;
    }
    GameObject* ball = GetGameObject(m_Ball);
    GameObject* camera = GetGameObject(m_FollowCamera);
    if (!ball || !camera)
    {
        return;
    }

    // 공 앞(퍼즐 루트 +Z 쪽)에서 루트 -Z를 바라본다.
    const TransformComponent& root = *GetTransform();
    const Vector3 ballLocal = ToRootLocal(ball->GetTransform()->GetWorldPosition());
    const Vector3 desired = root.GetLocalToWorldMatrix().TransformPoint(
        { ballLocal.X, ballLocal.Y + m_CameraHeight, m_CameraDistance });
    const Quaternion facing = (root.GetWorldRotation() * Rotator{ 0.0f, 180.0f, 0.0f }.ToQuaternion()).Normalized();

    TransformComponent& cameraTransform = *camera->GetTransform();
    Vector3 position = desired;
    if (m_CameraSnapped)
    {
        const float blend = 1.0f - std::exp(-m_CameraSharpness * Time::DeltaTime());
        position = Lerp(cameraTransform.GetWorldPosition(), desired, blend);
    }
    m_CameraSnapped = true;
    SetWorldPose(cameraTransform, position, facing);
}
