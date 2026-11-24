#include "MiniZeldaSample/AncientGate.h"

#include "MiniZeldaSample/DevMode.h"
#include "MiniZeldaSample/LeverConsole.h"
#include "MiniZeldaSample/LeverPuzzle.h"
#include "MiniZeldaSample/MarbleConsole.h"
#include "MiniZeldaSample/MarbleRun.h"
#include "MiniZeldaSample/TiltMazeConsole.h"
#include "MiniZeldaSample/TiltMazePuzzle.h"
#include "MoveSample/MoveAnimationController.h"
#include "Scripts/Camera/BackViewCamera.h"
#include "CNDebug/DebugLog.h"
#include "Simulation/ParticleEffectComponent.h"

#include <algorithm>
#include <cmath>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<AncientGate>("AncientGate")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("puzzle1Name", &AncientGate::m_Puzzle1Name)
        .property("puzzle2Name", &AncientGate::m_Puzzle2Name)
        .property("puzzle3Name", &AncientGate::m_Puzzle3Name)
        .property("fire1", &AncientGate::m_Fire1)
        .property("fire2", &AncientGate::m_Fire2)
        .property("fire3", &AncientGate::m_Fire3)
        .property("torchCamera1", &AncientGate::m_TorchCamera1)
        .property("torchCamera2", &AncientGate::m_TorchCamera2)
        .property("torchCamera3", &AncientGate::m_TorchCamera3)
        .property("gateCamera", &AncientGate::m_GateCamera)
        .property("leftPivot", &AncientGate::m_LeftPivot)
        .property("rightPivot", &AncientGate::m_RightPivot)
        .property("playerName", &AncientGate::m_PlayerName)
        .property("torchSound", &AncientGate::m_TorchSound)
        .property("solveDelay", &AncientGate::m_SolveDelay)
        .property("torchLookTime", &AncientGate::m_TorchLookTime)
        .property("torchHoldTime", &AncientGate::m_TorchHoldTime)
        .property("gateLookTime", &AncientGate::m_GateLookTime)
        .property("doorOpenTime", &AncientGate::m_DoorOpenTime)
        .property("doorOpenAngle", &AncientGate::m_DoorOpenAngle)
        .property("gateHoldTime", &AncientGate::m_GateHoldTime)
        .property("shakeAmplitude", &AncientGate::m_ShakeAmplitude)
        .property("shakeFrequency", &AncientGate::m_ShakeFrequency);
}

bool AncientGate::s_CinematicPlaying = false;

namespace
{
    constexpr float kDegToRad = 0.017453292f;
    constexpr float kTwoPi = 6.2831853f;

    Quaternion AxisY(float degrees)
    {
        return Quaternion::FromXM(DirectX::XMQuaternionRotationAxis(DirectX::XMVectorSet(0, 1, 0, 0), degrees * kDegToRad));
    }

    void SetWorldPosition(TransformComponent& transform, const Vector3& position)
    {
        const TransformComponent* parent = transform.GetParent();
        transform.SetPosition(parent ? parent->GetWorldToLocalMatrix().TransformPoint(position) : position);
    }
}

void AncientGate::Start()
{
    s_CinematicPlaying = false;
    for (int i = 0; i < 3; ++i)
    {
        SetFire(i, false);
    }

    // 개발자 모드는 문과 함께 다닌다. 예전에 배치한 문에는 없으므로 없으면 붙인다.
    if (!GetComponent<DevMode>())
    {
        AddComponent<DevMode>();
    }
}

void AncientGate::OnDestroy()
{
    if (m_Step != Step::None)
    {
        s_CinematicPlaying = false;
    }
}

bool AncientGate::IsPuzzleSolved(const std::string& name) const
{
    GameObject* puzzle = name.empty() ? nullptr : FindGameObjectByName(name);
    if (!puzzle)
    {
        return false;
    }
    if (const auto* tilt = puzzle->GetComponent<TiltMazePuzzle>()) return tilt->IsSolved();
    if (const auto* lever = puzzle->GetComponent<LeverPuzzle>()) return lever->IsSolved();
    if (const auto* marble = puzzle->GetComponent<MarbleRun>()) return marble->IsSolved();
    return false;
}

void AncientGate::CheckPuzzles()
{
    const std::string* names[3] = { &m_Puzzle1Name, &m_Puzzle2Name, &m_Puzzle3Name };
    for (int i = 0; i < 3; ++i)
    {
        if (!m_Counted[i] && IsPuzzleSolved(*names[i]))
        {
            m_Counted[i] = true;
            m_Pending.push_back({ m_SolveDelay });
        }
    }
}

void AncientGate::SetFire(int index, bool lit)
{
    const Handle<GameObject>* fires[3] = { &m_Fire1, &m_Fire2, &m_Fire3 };
    if (GameObject* fire = GetGameObject(*fires[index]))
    {
        for (ParticleEffectComponent* effect : fire->GetComponents<ParticleEffectComponent>())
        {
            effect->SetEnabled(lit);
        }
        if (lit)
        {
            if (AudioSystem* audio = AudioSystem::Get())
            {
                audio->PlayOneShot(m_TorchSound, fire->GetTransform()->GetWorldPosition(), 1.0f, 1.0f, 3.0f, 40.0f);
            }
        }
    }
}

void AncientGate::SwitchCamera(const Handle<GameObject>& cameraObject)
{
    GameObject* object = GetGameObject(cameraObject);
    if (auto* camera = object ? object->GetComponent<CameraComponent>() : nullptr)
    {
        GetOwnerObject()->GetScene()->SetMainCamera(camera);
    }
}

void AncientGate::SetPlayerControl(bool enabled)
{
    if (GameObject* player = FindGameObjectByName(m_PlayerName))
    {
        if (auto* mover = player->GetComponent<MoveAnimationController>())
        {
            mover->SetInputEnabled(enabled);
        }
    }
    if (GameObject* camera = GetGameObject(m_ReturnCamera))
    {
        if (auto* orbit = camera->GetComponent<BackViewCamera>())
        {
            orbit->SetEnabled(enabled);
        }
    }
}

void AncientGate::CloseConsoles()
{
    for (GameObject* object : GetOwnerObject()->GetScene()->GetAllGameObjects())
    {
        if (!object) continue;
        if (auto* tilt = object->GetComponent<TiltMazeConsole>()) tilt->Close();
        if (auto* lever = object->GetComponent<LeverConsole>()) lever->Close();
        if (auto* marble = object->GetComponent<MarbleConsole>()) marble->Close();
    }
}

void AncientGate::BeginCinematic()
{
    // 석판을 먼저 닫아 주인공 카메라와 조작을 되돌린 뒤, 그 카메라를 돌아올 곳으로 기억한다.
    CloseConsoles();
    s_CinematicPlaying = true;
    CameraComponent* current = GetOwnerObject()->GetScene()->GetMainCamera();
    m_ReturnCamera = current ? current->GetOwnerObject()->GetHandle() : Handle<GameObject>{};
    SetPlayerControl(false);
}

void AncientGate::EndCinematic()
{
    SwitchCamera(m_ReturnCamera);
    SetPlayerControl(true);
    m_ReturnCamera = {};
    s_CinematicPlaying = false;
    m_Step = Step::None;
}

void AncientGate::UpdateGateOpen(float dt)
{
    m_OpenTime += dt;
    const float u = std::clamp(m_OpenTime / std::max(m_DoorOpenTime, 0.01f), 0.0f, 1.0f);
    const float s = u * u * (3.0f - 2.0f * u); // 천천히 시작해서 천천히 멈춘다
    const float angle = m_DoorOpenAngle * s;
    if (GameObject* left = GetGameObject(m_LeftPivot))
    {
        left->GetTransform()->SetRotation((m_LeftStart * AxisY(angle)).Normalized());
    }
    if (GameObject* right = GetGameObject(m_RightPivot))
    {
        right->GetTransform()->SetRotation((m_RightStart * AxisY(-angle)).Normalized());
    }

    // 문이 열리는 동안 카메라를 흔든다(끝으로 갈수록 약해진다).
    if (GameObject* camera = GetGameObject(m_GateCamera))
    {
        const float strength = m_ShakeAmplitude * (1.0f - 0.7f * u);
        const float t = m_OpenTime * m_ShakeFrequency * kTwoPi;
        const Vector3 offset{
            strength * (std::sin(t) + 0.5f * std::sin(t * 2.3f + 1.7f)),
            strength * (std::sin(t * 1.37f + 0.6f) + 0.5f * std::sin(t * 3.1f)),
            0.0f
        };
        SetWorldPosition(*camera->GetTransform(), u < 1.0f ? m_GateCameraBase + offset : m_GateCameraBase);
    }
}

int AncientGate::FirstUnlitTorch() const
{
    for (int i = 0; i < 3; ++i)
    {
        if (!m_Lit[i]) return i;
    }
    return -1;
}

void AncientGate::BeginGateSequence()
{
    SwitchCamera(m_GateCamera);
    if (GameObject* camera = GetGameObject(m_GateCamera))
    {
        m_GateCameraBase = camera->GetTransform()->GetWorldPosition();
    }
    m_Step = Step::GateLook;
    m_Timer = m_GateLookTime;
}

void AncientGate::DevLightTorch(int index)
{
    if (index < 0 || index >= 3 || m_Lit[index])
    {
        return;
    }
    SetFire(index, true);
    m_Lit[index] = true;
    if (AllLit() && !m_GateOpened && m_Step == Step::None)
    {
        BeginCinematic();
        BeginGateSequence();
    }
}

void AncientGate::DevForgetPuzzle(int index)
{
    if (index >= 0 && index < 3)
    {
        m_Counted[index] = false;
    }
}

void AncientGate::Update()
{
    const float dt = Time::DeltaTime();
    CheckPuzzles();
    for (PendingTorch& pending : m_Pending)
    {
        pending.delay -= dt;
    }

    if (m_Step == Step::None && !m_Pending.empty() && m_Pending.front().delay <= 0.0f && FirstUnlitTorch() >= 0)
    {
        m_Pending.pop_front();
        BeginCinematic();
        m_CurrentTorch = FirstUnlitTorch();
        const Handle<GameObject>* cameras[3] = { &m_TorchCamera1, &m_TorchCamera2, &m_TorchCamera3 };
        SwitchCamera(*cameras[m_CurrentTorch]);
        m_Step = Step::TorchLook;
        m_Timer = m_TorchLookTime;
    }

    switch (m_Step)
    {
    case Step::None:
        break;
    case Step::TorchLook:
        m_Timer -= dt;
        if (m_Timer <= 0.0f)
        {
            SetFire(m_CurrentTorch, true);
            m_Lit[m_CurrentTorch] = true;
            m_Step = Step::TorchHold;
            m_Timer = m_TorchHoldTime;
        }
        break;
    case Step::TorchHold:
        m_Timer -= dt;
        if (m_Timer <= 0.0f)
        {
            if (AllLit() && !m_GateOpened)
            {
                BeginGateSequence();
            }
            else
            {
                EndCinematic();
            }
        }
        break;
    case Step::GateLook:
        m_Timer -= dt;
        if (m_Timer <= 0.0f)
        {
            if (GameObject* left = GetGameObject(m_LeftPivot)) m_LeftStart = left->GetTransform()->GetRotation();
            if (GameObject* right = GetGameObject(m_RightPivot)) m_RightStart = right->GetTransform()->GetRotation();
            m_OpenTime = 0.0f;
            m_Step = Step::GateOpen;
        }
        break;
    case Step::GateOpen:
        UpdateGateOpen(dt);
        if (m_OpenTime >= m_DoorOpenTime)
        {
            m_GateOpened = true;
            CN_LOG_GAME("AncientGate", "the gate is open");
            m_Step = Step::GateHold;
            m_Timer = m_GateHoldTime;
        }
        break;
    case Step::GateHold:
        m_Timer -= dt;
        if (m_Timer <= 0.0f)
        {
            EndCinematic();
        }
        break;
    }
}
