#include "MiniZeldaSample/MarbleConsole.h"

#include "MiniZeldaSample/AncientGate.h"

#include "MiniZeldaSample/MarbleRun.h"
#include "MoveSample/MoveAnimationController.h"
#include "Scripts/Camera/BackViewCamera.h"
#include "CNDebug/DebugLog.h"

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<MarbleConsole>("MarbleConsole")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("puzzle", &MarbleConsole::m_Puzzle)
        .property("followCamera", &MarbleConsole::m_FollowCamera)
        .property("playerName", &MarbleConsole::m_PlayerName)
        .property("interactRadius", &MarbleConsole::m_InteractRadius);
}

void MarbleConsole::Start()
{
    m_Player = FindGameObjectHandleByName(m_PlayerName);
    m_Hud.Create(*this, { { "마우스 좌클릭", "장치 제어" }, { "E", "나가기" } });
}

MarbleRun* MarbleConsole::GetPuzzle() const
{
    GameObject* puzzle = GetGameObject(m_Puzzle);
    return puzzle ? puzzle->GetComponent<MarbleRun>() : nullptr;
}

GameObject* MarbleConsole::GetPlayer()
{
    GameObject* player = GetGameObject(m_Player);
    if (!player)
    {
        m_Player = FindGameObjectHandleByName(m_PlayerName);
        player = GetGameObject(m_Player);
    }
    return player;
}

bool MarbleConsole::IsPlayerInRange()
{
    const GameObject* player = GetPlayer();
    if (!player)
    {
        return false;
    }
    const Vector3 offset = player->GetTransform()->GetWorldPosition() - GetTransform()->GetWorldPosition();
    return offset.X * offset.X + offset.Z * offset.Z <= m_InteractRadius * m_InteractRadius;
}

void MarbleConsole::SetPlayerControl(bool enabled)
{
    if (GameObject* player = GetPlayer())
    {
        if (auto* mover = player->GetComponent<MoveAnimationController>())
        {
            mover->SetInputEnabled(enabled);
        }
    }
    if (GameObject* camera = GetGameObject(m_PreviousCamera))
    {
        if (auto* orbit = camera->GetComponent<BackViewCamera>())
        {
            orbit->SetEnabled(enabled);
        }
    }
}

void MarbleConsole::Enter()
{
    MarbleRun* puzzle = GetPuzzle();
    GameObject* cameraObject = GetGameObject(m_FollowCamera);
    CameraComponent* followCamera = cameraObject ? cameraObject->GetComponent<CameraComponent>() : nullptr;
    if (!puzzle || !followCamera)
    {
        CN_LOG_GAME("MarbleRun", "'", GetOwnerObject()->GetName(), "': puzzle or followCamera is not set");
        return;
    }

    Scene& scene = *GetOwnerObject()->GetScene();
    CameraComponent* previous = scene.GetMainCamera();
    m_PreviousCamera = previous ? previous->GetOwnerObject()->GetHandle() : Handle<GameObject>{};
    scene.SetMainCamera(followCamera);
    SetPlayerControl(false);
    puzzle->SetPlaying(true);
    m_Active = true;
    m_Hud.SetShown(true);
}

void MarbleConsole::Exit()
{
    if (MarbleRun* puzzle = GetPuzzle())
    {
        puzzle->SetPlaying(false);
    }

    Scene& scene = *GetOwnerObject()->GetScene();
    if (GameObject* previous = GetGameObject(m_PreviousCamera))
    {
        if (auto* camera = previous->GetComponent<CameraComponent>())
        {
            scene.SetMainCamera(camera);
        }
    }
    SetPlayerControl(true);
    m_PreviousCamera = {};
    m_Active = false;
    m_Hud.SetShown(false);
}

void MarbleConsole::Update()
{
    const InputManager& input = Application::Get().GetInputManager();
    if (input.IsKeyPressed(Key::E))
    {
        if (m_Active)
        {
            Exit();
            return;
        }
        if (!AncientGate::IsCinematicPlaying() && IsPlayerInRange())
        {
            Enter();
            return;
        }
    }

    if (m_Active && (input.IsKeyPressed(Key::Space) || input.IsMousePressed(MouseButton::Left)))
    {
        if (MarbleRun* puzzle = GetPuzzle())
        {
            puzzle->Swap();
        }
    }
}

void MarbleConsole::OnDestroy()
{
    if (m_Active)
    {
        Exit();
    }
}
