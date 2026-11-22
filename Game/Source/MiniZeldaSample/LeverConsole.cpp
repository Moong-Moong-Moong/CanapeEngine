#include "MiniZeldaSample/LeverConsole.h"

#include "MiniZeldaSample/AncientGate.h"

#include "MiniZeldaSample/LeverPuzzle.h"
#include "MoveSample/MoveAnimationController.h"
#include "Scripts/Camera/BackViewCamera.h"
#include "CNDebug/DebugLog.h"
#include "Renderer/Renderer.h"
#include "UI/UISystem.h"

#include <DirectXMath.h>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<LeverConsole>("LeverConsole")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("puzzle", &LeverConsole::m_Puzzle)
        .property("puzzleCamera", &LeverConsole::m_PuzzleCamera)
        .property("playerName", &LeverConsole::m_PlayerName)
        .property("interactRadius", &LeverConsole::m_InteractRadius);
}

void LeverConsole::Start()
{
    m_Player = FindGameObjectHandleByName(m_PlayerName);
    m_Hud.Create(*this, {
        { "마우스 좌클릭 누르고 있기", "돌 들고 있기" },
        { "돌을 든 채 마우스 좌클릭 떼기", "돌 놓기" },
        { "WASD", "돌 움직이기" },
        { "Q / E", "돌 내리기 / 올리기" },
        { "E (돌을 들고 있지 않을 때)", "나가기" },
    });
}

LeverPuzzle* LeverConsole::GetPuzzle() const
{
    GameObject* puzzle = GetGameObject(m_Puzzle);
    return puzzle ? puzzle->GetComponent<LeverPuzzle>() : nullptr;
}

GameObject* LeverConsole::GetPlayer()
{
    GameObject* player = GetGameObject(m_Player);
    if (!player)
    {
        m_Player = FindGameObjectHandleByName(m_PlayerName);
        player = GetGameObject(m_Player);
    }
    return player;
}

bool LeverConsole::IsPlayerInRange()
{
    const GameObject* player = GetPlayer();
    if (!player)
    {
        return false;
    }
    const Vector3 offset = player->GetTransform()->GetWorldPosition() - GetTransform()->GetWorldPosition();
    return offset.X * offset.X + offset.Z * offset.Z <= m_InteractRadius * m_InteractRadius;
}

void LeverConsole::SetPlayerControl(bool enabled)
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

void LeverConsole::Enter()
{
    GameObject* cameraObject = GetGameObject(m_PuzzleCamera);
    CameraComponent* puzzleCamera = cameraObject ? cameraObject->GetComponent<CameraComponent>() : nullptr;
    if (!GetPuzzle() || !puzzleCamera)
    {
        CN_LOG_GAME("LeverPuzzle", "'", GetOwnerObject()->GetName(), "': puzzle or puzzleCamera is not set");
        return;
    }

    Scene& scene = *GetOwnerObject()->GetScene();
    CameraComponent* previous = scene.GetMainCamera();
    m_PreviousCamera = previous ? previous->GetOwnerObject()->GetHandle() : Handle<GameObject>{};
    scene.SetMainCamera(puzzleCamera);
    SetPlayerControl(false);
    m_Active = true;
    m_Hud.SetShown(true);
}

void LeverConsole::Exit()
{
    if (LeverPuzzle* puzzle = GetPuzzle())
    {
        puzzle->EndCarry();
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

bool LeverConsole::GetMouseRay(Vector3& origin, Vector3& direction) const
{
    Application& app = Application::Get();
    float ndcX = 0.0f, ndcY = 0.0f;
    if (!app.GetUISystem().GetPointerNdc(app.GetInputManager(), ndcX, ndcY))
    {
        return false;
    }

    CameraComponent* camera = GetOwnerObject()->GetScene()->GetMainCamera();
    const Renderer& renderer = app.GetRenderer();
    if (!camera || renderer.GetWidth() == 0 || renderer.GetHeight() == 0)
    {
        return false;
    }
    const float aspect = static_cast<float>(renderer.GetWidth()) / static_cast<float>(renderer.GetHeight());
    // NDC의 가까운 면(z=0)과 먼 면(z=1)을 월드로 되돌려 그 두 점을 잇는다(행벡터 규약, view * projection).
    const DirectX::XMMATRIX inverse = (camera->GetViewMatrix() * camera->GetProjectionMatrix(aspect)).Inverse().ToXM();
    const DirectX::XMVECTOR nearPoint = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), inverse);
    const DirectX::XMVECTOR farPoint = DirectX::XMVector3TransformCoord(DirectX::XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), inverse);
    origin = Vector3::FromXM(nearPoint);
    direction = (Vector3::FromXM(farPoint) - origin).Normalized();
    return true;
}

void LeverConsole::UpdateStoneControl()
{
    LeverPuzzle* puzzle = GetPuzzle();
    if (!puzzle)
    {
        return;
    }
    const InputManager& input = Application::Get().GetInputManager();

    if (input.IsKeyPressed(Key::R))
    {
        puzzle->ResetStones();
        return;
    }

    if (!puzzle->IsCarrying())
    {
        Vector3 origin, direction;
        if (input.IsMousePressed(MouseButton::Left) && GetMouseRay(origin, direction))
        {
            puzzle->TryPick(origin, direction);
        }
        return;
    }

    if (!input.IsMouseDown(MouseButton::Left))
    {
        puzzle->EndCarry();
        return;
    }

    // Q: 내리기, E: 올리기(들고 있는 동안에는 E가 나가기 대신 올리기다).
    float vertical = 0.0f;
    if (input.IsKeyDown(Key::Q)) vertical -= 1.0f;
    if (input.IsKeyDown(Key::E)) vertical += 1.0f;
    puzzle->SetCarryVertical(vertical);

    // WASD: 카메라가 보는 방향 기준으로 수평 이동.
    Vector3 forward = Vector3::Forward;
    if (const CameraComponent* camera = GetOwnerObject()->GetScene()->GetMainCamera())
    {
        const Vector3 cameraForward = camera->GetTransform()->GetForward();
        const Vector3 flat{ cameraForward.X, 0.0f, cameraForward.Z };
        if (!flat.IsNearlyZero()) forward = flat.Normalized();
    }
    const Vector3 right{ forward.Z, 0.0f, -forward.X };
    Vector3 move{};
    if (input.IsKeyDown(Key::W)) move = move + forward;
    if (input.IsKeyDown(Key::S)) move = move - forward;
    if (input.IsKeyDown(Key::D)) move = move + right;
    if (input.IsKeyDown(Key::A)) move = move - right;
    puzzle->SetCarryMove(move);
}

void LeverConsole::Update()
{
    const InputManager& input = Application::Get().GetInputManager();
    if (input.IsKeyPressed(Key::E))
    {
        LeverPuzzle* puzzle = GetPuzzle();
        if (m_Active)
        {
            // 돌을 들고 있을 때 E는 올리기(UpdateStoneControl에서 처리)이고, 나가기는 들고 있지 않을 때만.
            if (!puzzle || !puzzle->IsCarrying())
            {
                // 직접 나가면 돌을 처음 자리로 되돌린다. 클리어한 뒤에는 그대로 둔다.
                if (puzzle && !puzzle->IsSolved())
                {
                    puzzle->ResetStones();
                }
                Exit();
                return;
            }
        }
        else if (!AncientGate::IsCinematicPlaying() && IsPlayerInRange())
        {
            Enter();
            return;
        }
    }

    if (m_Active)
    {
        UpdateStoneControl();
    }
}

void LeverConsole::OnDestroy()
{
    if (m_Active)
    {
        Exit();
    }
}
