#include "MiniZeldaSample/TiltMazeConsole.h"

#include "MiniZeldaSample/AncientGate.h"

#include "MiniZeldaSample/TiltMazePuzzle.h"
#include "MoveSample/MoveAnimationController.h"
#include "Scripts/Camera/BackViewCamera.h"
#include "CNDebug/DebugLog.h"

#include <cmath>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<TiltMazeConsole>("TiltMazeConsole")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("puzzle", &TiltMazeConsole::m_Puzzle)
        .property("puzzleCamera", &TiltMazeConsole::m_PuzzleCamera)
        .property("playerName", &TiltMazeConsole::m_PlayerName)
        .property("interactRadius", &TiltMazeConsole::m_InteractRadius)
        .property("mouseSensitivity", &TiltMazeConsole::m_MouseSensitivity);
}

void TiltMazeConsole::Start()
{
    m_Player = FindGameObjectHandleByName(m_PlayerName);
    m_Hud.Create(*this, { { "마우스 이동", "판 움직이기" }, { "E", "나가기" } });
}

TiltMazePuzzle* TiltMazeConsole::GetPuzzle() const
{
    GameObject* puzzle = GetGameObject(m_Puzzle);
    return puzzle ? puzzle->GetComponent<TiltMazePuzzle>() : nullptr;
}

GameObject* TiltMazeConsole::GetPlayer()
{
    GameObject* player = GetGameObject(m_Player);
    if (!player)
    {
        m_Player = FindGameObjectHandleByName(m_PlayerName);
        player = GetGameObject(m_Player);
        if (!player && !m_LoggedNoPlayer)
        {
            CN_LOG_GAME("TiltMaze", "'", GetOwnerObject()->GetName(), "': no object named '", m_PlayerName, "'");
            m_LoggedNoPlayer = true;
        }
    }
    return player;
}

bool TiltMazeConsole::IsPlayerInRange()
{
    const GameObject* player = GetPlayer();
    if (!player)
    {
        return false;
    }
    const Vector3 offset = player->GetTransform()->GetWorldPosition() - GetTransform()->GetWorldPosition();
    return offset.X * offset.X + offset.Z * offset.Z <= m_InteractRadius * m_InteractRadius;
}

void TiltMazeConsole::SetPlayerControl(bool enabled)
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

void TiltMazeConsole::Enter()
{
    TiltMazePuzzle* puzzle = GetPuzzle();
    GameObject* cameraObject = GetGameObject(m_PuzzleCamera);
    CameraComponent* puzzleCamera = cameraObject ? cameraObject->GetComponent<CameraComponent>() : nullptr;
    if (!puzzle || !puzzleCamera)
    {
        CN_LOG_GAME("TiltMaze", "'", GetOwnerObject()->GetName(), "': puzzle or puzzleCamera is not set");
        return;
    }

    Scene& scene = *GetOwnerObject()->GetScene();
    CameraComponent* previous = scene.GetMainCamera();
    m_PreviousCamera = previous ? previous->GetOwnerObject()->GetHandle() : Handle<GameObject>{};
    scene.SetMainCamera(puzzleCamera);

    SetPlayerControl(false);
    puzzle->SetControlled(true);
    m_Active = true;
    m_Hud.SetShown(true);
}

void TiltMazeConsole::Exit()
{
    if (TiltMazePuzzle* puzzle = GetPuzzle())
    {
        puzzle->SetControlled(false);
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

void TiltMazeConsole::Update()
{
    const InputManager& input = Application::Get().GetInputManager();

    // E는 이 스크립트 하나만 처리해서, 같은 프레임에 들어갔다 바로 나오는 일이 없다.
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
            return; // 들어간 프레임의 마우스 이동은 버린다
        }
    }

    if (!m_Active)
    {
        return;
    }

    if (TiltMazePuzzle* puzzle = GetPuzzle())
    {
        // 화면 좌표는 아래가 +Y라 위로 밀면(dy < 0) 공이 위쪽(+Z)으로 구르도록 부호를 뒤집는다.
        const IntVector2& delta = input.GetMouseDelta();
        puzzle->AddTilt(static_cast<float>(delta.X) * m_MouseSensitivity,
            -static_cast<float>(delta.Y) * m_MouseSensitivity);
    }
}

void TiltMazeConsole::OnDestroy()
{
    if (m_Active)
    {
        Exit();
    }
}
