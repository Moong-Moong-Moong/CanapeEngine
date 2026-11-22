#include "MiniZeldaSample/DemoFlow.h"

#include "MiniZeldaSample/AncientGate.h"
#include "MoveSample/MoveAnimationController.h"
#include "Scripts/Camera/BackViewCamera.h"
#include "CNDebug/DebugLog.h"

#include <algorithm>
#include <filesystem>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::class_<DemoFlow>("DemoFlow")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("introCamera", &DemoFlow::m_IntroCamera)
        .property("mainCamera", &DemoFlow::m_MainCamera)
        .property("gate", &DemoFlow::m_Gate)
        .property("gateTrigger", &DemoFlow::m_GateTrigger)
        .property("playerName", &DemoFlow::m_PlayerName)
        .property("triggerRadius", &DemoFlow::m_TriggerRadius)
        .property("blendTime", &DemoFlow::m_BlendTime)
        .property("controlsTime", &DemoFlow::m_ControlsTime)
        .property("restartScene", &DemoFlow::m_RestartScene);
}

namespace
{
    constexpr Vector2 kReferenceResolution{ 1920.0f, 1080.0f };
    constexpr UIColor kTextColor{ 255, 255, 255 };
    constexpr UIColor kShadowColor{ 0, 0, 0, 170 };

    UIText* MakeText(UIElement& root, const std::string& name, float y, float size, UIColor color, float shadowOffset)
    {
        auto* text = root.AddChild<UIText>(name);
        text->SetAnchor({ 0.5f, 0.0f });
        text->SetPivot({ 0.5f, 0.0f });
        text->SetPosition({ shadowOffset, y + shadowOffset });
        text->SetSize({ 1600.0f, size * 1.6f });
        text->SetFontSize(size);
        text->SetColor(color);
        text->SetHorizontalAlign(UITextAlign::Center);
        text->SetVerticalAlign(UITextAlign::Center);
        text->SetHitTestable(false);
        return text;
    }
}

void DemoFlow::Start()
{
    GameObject* intro = GetGameObject(m_IntroCamera);
    if (!intro || !intro->GetComponent<CameraComponent>() || !GetGameObject(m_MainCamera))
    {
        CN_LOG_GAME("DemoFlow", "introCamera or mainCamera is not set; the demo starts in play");
        m_Phase = Phase::Playing;
        return;
    }
    m_IntroPosition = intro->GetTransform()->GetPosition();
    m_IntroRotation = intro->GetTransform()->GetRotation();

    BuildUi();
    m_ControlsHud.Create(*this, { { "W A S D", "이동" }, { "E", "상호작용" }, { "F", "공격" }, { "마우스", "카메라 조작" } });

    SetCamera(m_IntroCamera);
    SetPlayerControl(false);
    ShowIntroText();
    m_Phase = Phase::Intro;
}

void DemoFlow::BuildUi()
{
    GameObject* intro = GetGameObject(m_IntroCamera);
    m_TextCanvas = intro ? intro->AddComponent<UICanvasComponent>(kReferenceResolution) : nullptr;
    if (!m_TextCanvas)
    {
        return;
    }
    UIElement& root = m_TextCanvas->GetRoot();
    // 그림자(살짝 어긋난 검은 글씨)를 먼저 그려 밝은 배경에서도 읽히게 한다.
    m_TitleShadow = MakeText(root, "DemoTitleShadow", 120.0f, 56.0f, kShadowColor, 3.0f);
    m_Title = MakeText(root, "DemoTitle", 120.0f, 56.0f, kTextColor, 0.0f);
    m_SubShadow = MakeText(root, "DemoSubShadow", 220.0f, 34.0f, kShadowColor, 2.0f);
    m_Sub = MakeText(root, "DemoSub", 220.0f, 34.0f, kTextColor, 0.0f);
}

void DemoFlow::ShowIntroText()
{
    if (!m_TextCanvas) return;
    m_Title->SetText("좌클릭 시 데모 시작");
    m_TitleShadow->SetText("좌클릭 시 데모 시작");
    m_Sub->SetText("");
    m_SubShadow->SetText("");
    m_TextCanvas->SetEnabled(true);
}

void DemoFlow::ShowEndingText()
{
    if (!m_TextCanvas) return;
    m_Title->SetText("데모를 플레이해주셔서 감사합니다");
    m_TitleShadow->SetText("데모를 플레이해주셔서 감사합니다");
    m_Sub->SetText("좌클릭 시 재시작");
    m_SubShadow->SetText("좌클릭 시 재시작");
    m_TextCanvas->SetEnabled(true);
}

void DemoFlow::HideTexts()
{
    if (m_TextCanvas) m_TextCanvas->SetEnabled(false);
}

void DemoFlow::SetPlayerControl(bool enabled)
{
    if (GameObject* player = FindGameObjectByName(m_PlayerName))
    {
        if (auto* mover = player->GetComponent<MoveAnimationController>())
        {
            mover->SetInputEnabled(enabled);
        }
    }
    if (GameObject* camera = GetGameObject(m_MainCamera))
    {
        if (auto* orbit = camera->GetComponent<BackViewCamera>())
        {
            orbit->SetEnabled(enabled);
        }
    }
}

void DemoFlow::SetCamera(const Handle<GameObject>& cameraObject)
{
    GameObject* object = GetGameObject(cameraObject);
    if (auto* camera = object ? object->GetComponent<CameraComponent>() : nullptr)
    {
        GetOwnerObject()->GetScene()->SetMainCamera(camera);
    }
}

bool DemoFlow::IsPlayerInsideGate()
{
    GameObject* gateObject = GetGameObject(m_Gate);
    const auto* gate = gateObject ? gateObject->GetComponent<AncientGate>() : nullptr;
    if (!gate || !gate->IsGateOpened() || AncientGate::IsCinematicPlaying())
    {
        return false;
    }
    GameObject* trigger = GetGameObject(m_GateTrigger);
    GameObject* player = FindGameObjectByName(m_PlayerName);
    if (!trigger || !player)
    {
        return false;
    }
    const Vector3 offset = player->GetTransform()->GetWorldPosition() - trigger->GetTransform()->GetWorldPosition();
    return offset.X * offset.X + offset.Z * offset.Z <= m_TriggerRadius * m_TriggerRadius;
}

void DemoFlow::UpdateBlend(const Vector3& fromPosition, const Quaternion& fromRotation,
    const Vector3& toPosition, const Quaternion& toRotation)
{
    GameObject* intro = GetGameObject(m_IntroCamera);
    if (!intro)
    {
        return;
    }
    const float u = std::clamp(m_Timer / std::max(m_BlendTime, 0.01f), 0.0f, 1.0f);
    const float s = u * u * (3.0f - 2.0f * u); // 천천히 출발해서 천천히 도착한다
    intro->GetTransform()->SetPosition(Lerp(fromPosition, toPosition, s));
    intro->GetTransform()->SetRotation(
        Quaternion::FromXM(DirectX::XMQuaternionSlerp(fromRotation.ToXM(), toRotation.ToXM(), s)).Normalized());
}

void DemoFlow::Restart()
{
    // 에디터 플레이 중이면 플레이를 시작한 순간의 씬으로, 아니면 씬 파일로 다시 연다.
    SceneManager& sceneManager = Application::Get().GetSceneManager();
    std::error_code error;
    const std::filesystem::path editorSnapshot = std::filesystem::temp_directory_path(error) / "CanapeEditorPlaySnapshot.json";
    std::string path = sceneManager.GetSceneDirectory() + m_RestartScene;
    if (!error && std::filesystem::exists(editorSnapshot, error))
    {
        path = editorSnapshot.string();
    }

    Scene* current = GetOwnerObject()->GetScene();
    if (!sceneManager.LoadSceneFromFile(path))
    {
        CN_LOG_GAME("DemoFlow", "restart failed: cannot load ", path);
        return;
    }
    sceneManager.UnloadScene(current);
}

void DemoFlow::Update()
{
    const float dt = Time::DeltaTime();
    const InputManager& input = Application::Get().GetInputManager();
    GameObject* main = GetGameObject(m_MainCamera);

    switch (m_Phase)
    {
    case Phase::Intro:
        if (input.IsMousePressed(MouseButton::Left))
        {
            HideTexts();
            // 주인공 카메라가 목적지 자리를 계속 갱신하도록 먼저 켠다(조작은 도착한 뒤에 넘긴다).
            if (main)
            {
                if (auto* orbit = main->GetComponent<BackViewCamera>()) orbit->SetEnabled(true);
            }
            m_BlendFromPosition = m_IntroPosition;
            m_BlendFromRotation = m_IntroRotation;
            m_Timer = 0.0f;
            m_Phase = Phase::BlendIn;
        }
        break;

    case Phase::BlendIn:
        m_Timer += dt;
        if (main)
        {
            UpdateBlend(m_BlendFromPosition, m_BlendFromRotation,
                main->GetTransform()->GetWorldPosition(), main->GetTransform()->GetWorldRotation());
        }
        if (m_Timer >= m_BlendTime)
        {
            SetCamera(m_MainCamera);
            SetPlayerControl(true);
            m_ControlsTimer = m_ControlsTime;
            m_Phase = Phase::Playing;
        }
        break;

    case Phase::Playing:
        if (m_ControlsTimer > 0.0f)
        {
            m_ControlsTimer -= dt;
            // 퍼즐 석판이나 연출 카메라로 바뀌어 있으면 그 패널과 겹치지 않게 숨긴다.
            const CameraComponent* active = GetOwnerObject()->GetScene()->GetMainCamera();
            const bool onPlayerCamera = main && active && active->GetOwnerObject() == main;
            m_ControlsHud.SetShown(m_ControlsTimer > 0.0f && onPlayerCamera);
        }
        if (GetGameObject(m_IntroCamera) && IsPlayerInsideGate())
        {
            m_ControlsHud.SetShown(false);
            m_ControlsTimer = 0.0f;
            SetPlayerControl(false);
            if (main)
            {
                m_BlendFromPosition = main->GetTransform()->GetWorldPosition();
                m_BlendFromRotation = main->GetTransform()->GetWorldRotation();
            }
            UpdateBlend(m_BlendFromPosition, m_BlendFromRotation, m_BlendFromPosition, m_BlendFromRotation);
            SetCamera(m_IntroCamera);
            ShowEndingText();
            m_Timer = 0.0f;
            m_Phase = Phase::BlendOut;
        }
        break;

    case Phase::BlendOut:
        m_Timer += dt;
        UpdateBlend(m_BlendFromPosition, m_BlendFromRotation, m_IntroPosition, m_IntroRotation);
        if (m_Timer >= m_BlendTime)
        {
            m_Phase = Phase::Ending;
        }
        break;

    case Phase::Ending:
        if (input.IsMousePressed(MouseButton::Left))
        {
            Restart();
        }
        break;
    }
}
