#include "Panels/SceneViewPanel.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

using namespace Canape;

namespace
{
    constexpr float kLookSensitivity = 0.15f;
    constexpr float kFastMultiplier = 3.0f;
    constexpr float kNearClip = 0.1f;
    constexpr float kFarClip = 2000.0f;
}

SceneViewPanel::PlayRequest SceneViewPanel::Draw(Renderer& renderer, float deltaTime, bool playing)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(kName);
    ImGui::PopStyleVar();

    PlayRequest request = PlayRequest::None;
    if (visible)
    {
        request = DrawToolbar(renderer, playing);

        const ImVec2 size = ImGui::GetContentRegionAvail();
        const uint32_t width = static_cast<uint32_t>(std::max(size.x, 1.0f));
        const uint32_t height = static_cast<uint32_t>(std::max(size.y, 1.0f));
        renderer.SetSceneSize(width, height);

        if (void* texture = renderer.GetSceneTexture())
        {
            ImGui::Image(reinterpret_cast<ImTextureID>(texture), ImVec2(static_cast<float>(width), static_cast<float>(height)));
        }
        else
        {
            ImGui::Dummy(ImVec2(static_cast<float>(width), static_cast<float>(height)));
        }

        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        if (playing)
        {
            m_Navigating = false;
            renderer.ClearViewOverride();
            ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(80, 200, 120, 255), 0.0f, 0, 2.0f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 8.0f, min.y + 6.0f), IM_COL32(80, 200, 120, 255),
                "Playing (Main Camera)");
        }
        else
        {
            UpdateCamera(ImGui::IsItemHovered(), deltaTime);
            renderer.SetViewOverride(BuildView(static_cast<float>(width) / static_cast<float>(height)));
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 8.0f, min.y + 6.0f), IM_COL32(255, 255, 255, 160),
                "RMB: look  WASD/QE: move  Shift: fast  Wheel: speed");
        }
    }
    ImGui::End();
    return request;
}

SceneViewPanel::PlayRequest SceneViewPanel::DrawToolbar(Renderer& renderer, bool playing)
{
    constexpr float kButtonWidth = 64.0f;
    constexpr float kRightMargin = 6.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    const float toolbarWidth = kButtonWidth * 2.0f + style.ItemSpacing.x;
    const float toolbarY = ImGui::GetCursorPosY() + style.FramePadding.y;
    const float contentWidth = ImGui::GetWindowContentRegionMax().x;

    ImGui::SetCursorPos(ImVec2(contentWidth - m_RenderDebugMenu.GetButtonWidth() - kRightMargin, toolbarY));
    m_RenderDebugMenu.Draw(renderer);

    ImGui::SetCursorPos(ImVec2(std::max((contentWidth - toolbarWidth) * 0.5f, 0.0f), toolbarY));

    PlayRequest request = PlayRequest::None;

    ImGui::BeginDisabled(playing);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.6f, 0.3f, 1.0f));
    if (ImGui::Button("Play", ImVec2(kButtonWidth, 0.0f)))
    {
        request = PlayRequest::Play;
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!playing);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.25f, 0.25f, 1.0f));
    if (ImGui::Button("Stop", ImVec2(kButtonWidth, 0.0f)))
    {
        request = PlayRequest::Stop;
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y);
    return request;
}

void SceneViewPanel::FrameMainCamera(Scene* scene)
{
    CameraComponent* camera = scene ? scene->GetMainCamera() : nullptr;
    if (!camera)
    {
        return;
    }

    const TransformComponent* transform = camera->GetTransform();
    const Vector3 forward = transform->GetForward().Normalized();
    m_Position = transform->GetPosition();
    m_Yaw = Math::ToDegrees(std::atan2(forward.X, forward.Z));
    m_Pitch = Math::ToDegrees(std::asin(std::clamp(-forward.Y, -1.0f, 1.0f)));
    m_FieldOfView = camera->GetFieldOfView();
}

void SceneViewPanel::UpdateCamera(bool hovered, float deltaTime)
{
    ImGuiIO& io = ImGui::GetIO();

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        m_Navigating = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        m_Navigating = false;
    }

    if (hovered && io.MouseWheel != 0.0f)
    {
        m_MoveSpeed = std::clamp(m_MoveSpeed * (io.MouseWheel > 0.0f ? 1.2f : 1.0f / 1.2f), 0.5f, 200.0f);
    }

    if (!m_Navigating)
    {
        return;
    }

    m_Yaw += io.MouseDelta.x * kLookSensitivity;
    m_Pitch = std::clamp(m_Pitch + io.MouseDelta.y * kLookSensitivity, -89.0f, 89.0f);

    const Quaternion rotation = GetRotation();
    Vector3 move{};
    if (ImGui::IsKeyDown(ImGuiKey_W)) move = move + rotation.Rotate(Vector3::Forward);
    if (ImGui::IsKeyDown(ImGuiKey_S)) move = move - rotation.Rotate(Vector3::Forward);
    if (ImGui::IsKeyDown(ImGuiKey_D)) move = move + rotation.Rotate(Vector3::Right);
    if (ImGui::IsKeyDown(ImGuiKey_A)) move = move - rotation.Rotate(Vector3::Right);
    if (ImGui::IsKeyDown(ImGuiKey_E)) move = move + Vector3::Up;
    if (ImGui::IsKeyDown(ImGuiKey_Q)) move = move - Vector3::Up;

    const float speed = m_MoveSpeed * (io.KeyShift ? kFastMultiplier : 1.0f);
    m_Position = m_Position + move.Normalized() * (speed * deltaTime);
}

Quaternion SceneViewPanel::GetRotation() const
{
    return Rotator{ m_Pitch, m_Yaw, 0.0f }.ToQuaternion();
}

RenderView SceneViewPanel::BuildView(float aspectRatio) const
{
    const Quaternion rotation = GetRotation();
    RenderView view;
    view.view = Matrix4::LookTo(m_Position, rotation.Rotate(Vector3::Forward), rotation.Rotate(Vector3::Up));
    view.projection = Matrix4::Perspective(Math::ToRadians(m_FieldOfView), aspectRatio, kNearClip, kFarClip);
    view.cameraPosition = m_Position;
    return view;
}
