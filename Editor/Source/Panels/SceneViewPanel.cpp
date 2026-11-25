#include "Panels/SceneViewPanel.h"
#include "ScenePicking.h"

#include <imgui.h>
#include <ImGuizmo.h>

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

SceneViewPanel::PlayRequest SceneViewPanel::Draw(EditorContext& context, Scene* scene, Renderer& renderer, UISystem& ui,
    float deltaTime, bool playing)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(kName);
    ImGui::PopStyleVar();

    // Hidden scene view: game UI gets no pointer.
    ui.SetPointerViewport({}, false);

    renderer.SetEditorGridEnabled(visible && !playing && m_ShowGrid);
    for (uint32_t plane = 0; plane < EditorGridPlaneCount; ++plane)
    {
        renderer.SetEditorGridPlaneVisible(static_cast<EditorGridPlane>(plane), m_GridPlanes[plane]);
    }

    uint64_t pickedId = 0;
    if (renderer.TakePickResult(pickedId) && m_PickPending)
    {
        m_PickPending = false;
        if (scene && !playing)
        {
            if (GameObject* picked = ScenePicking::FindOwnerOfComponent(*scene, pickedId))
            {
                context.Select(picked);
            }
            else
            {
                context.ClearSelection();
            }
        }
    }

    GameObject* selected = scene && !playing ? context.GetSelected(scene) : nullptr;
    OutlineStyle selectionStyle;
    selectionStyle.Color = { 1.0f, 0.55f, 0.1f };
    selectionStyle.Thickness = 2;
    selectionStyle.Opacity = 1.0f;
    renderer.SetSelectionOutline(selected && m_ShowSelectionOutline ? ScenePicking::SelectableComponentIds(*selected)
        : std::vector<uint64_t>{}, selectionStyle);

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
        // ImGui positions are relative to the main viewport; InputManager reports window client coordinates.
        // AllowWhenBlockedByActiveItem keeps the pointer while a click held on the image would otherwise block hover.
        const ImVec2 origin = ImGui::GetMainViewport()->Pos;
        const bool pointerOverImage = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        ui.SetPointerViewport({ min.x - origin.x, min.y - origin.y, max.x - min.x, max.y - min.y },
            playing && pointerOverImage);
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
            const bool hovered = ImGui::IsItemHovered();
            UpdateCamera(hovered, deltaTime);
            HandleGizmoShortcuts(hovered);
            const RenderView view = BuildView(static_cast<float>(width) / static_cast<float>(height));
            renderer.SetViewOverride(view);
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 8.0f, max.y - ImGui::GetTextLineHeight() - 6.0f),
                IM_COL32(255, 255, 255, 160),
                "W/E/R: move/rotate/scale  T: local/world  G: grid  RMB: look  WASD/QE: move  Wheel: speed");

            if (selected && m_ShowBounds)
            {
                DrawSelectionBounds(*selected, view, min, max);
            }
            DrawGizmo(selected, view, min, max);

            const bool overGizmo = selected && (ImGuizmo::IsOver() || ImGuizmo::IsUsing());
            if (hovered && !m_Navigating && !overGizmo && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const float x = mouse.x - min.x;
                const float y = mouse.y - min.y;
                if (x >= 0.0f && y >= 0.0f && x < static_cast<float>(width) && y < static_cast<float>(height))
                {
                    RequestPick(renderer, static_cast<uint32_t>(x), static_cast<uint32_t>(y));
                }
            }
        }
    }
    ImGui::End();
    return request;
}

void SceneViewPanel::RequestPick(Renderer& renderer, uint32_t x, uint32_t y)
{
    renderer.RequestPick(x, y);
    m_PickPending = true;
}

void SceneViewPanel::RequestPickAt(Renderer& renderer, float u, float v)
{
    const uint32_t width = renderer.GetWidth();
    const uint32_t height = renderer.GetHeight();
    RequestPick(renderer, static_cast<uint32_t>(std::clamp(u, 0.0f, 0.999f) * width),
        static_cast<uint32_t>(std::clamp(v, 0.0f, 0.999f) * height));
}

void SceneViewPanel::DrawSelectionBounds(GameObject& selected, const RenderView& view, const ImVec2& min, const ImVec2& max)
{
    const Matrix4 viewProjection = view.view * view.projection;
    const float width = max.x - min.x;
    const float height = max.y - min.y;
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(min, max, true);

    constexpr int kEdges[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
    for (const ScenePicking::OrientedBox& box : ScenePicking::SelectionBounds(selected))
    {
        ImVec2 screen[8];
        bool inFront[8];
        for (int corner = 0; corner < 8; ++corner)
        {
            const Vector3 local{ (corner & 1) ? box.max.X : box.min.X, (corner & 2) ? box.max.Y : box.min.Y,
                (corner & 4) ? box.max.Z : box.min.Z };
            const Vector3 world = box.toWorld.TransformPoint(local);
            const DirectX::XMVECTOR clip = DirectX::XMVector4Transform(
                DirectX::XMVectorSet(world.X, world.Y, world.Z, 1.0f), viewProjection.ToXM());
            const float w = DirectX::XMVectorGetW(clip);
            inFront[corner] = w > 1e-4f;
            const float invW = inFront[corner] ? 1.0f / w : 0.0f;
            screen[corner] = ImVec2(min.x + (DirectX::XMVectorGetX(clip) * invW * 0.5f + 0.5f) * width,
                min.y + (0.5f - DirectX::XMVectorGetY(clip) * invW * 0.5f) * height);
        }
        for (const auto& edge : kEdges)
        {
            if (inFront[edge[0]] && inFront[edge[1]])
            {
                drawList->AddLine(screen[edge[0]], screen[edge[1]], IM_COL32(255, 170, 40, 220), 1.5f);
            }
        }
    }
    drawList->PopClipRect();
}

void SceneViewPanel::DrawGizmoToolbar()
{
    const auto modeButton = [this](const char* label, GizmoOperation operation)
    {
        const bool active = m_GizmoOperation == operation;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) m_GizmoOperation = operation;
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    };
    modeButton("Move", GizmoOperation::Translate);
    modeButton("Rotate", GizmoOperation::Rotate);
    modeButton("Scale", GizmoOperation::Scale);
    if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(52.0f, 0.0f)))
    {
        m_GizmoLocal = !m_GizmoLocal;
    }
    ImGui::SameLine();
    const bool gridOn = m_ShowGrid;
    if (gridOn) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button("Grid")) m_ShowGrid = !m_ShowGrid;
    if (gridOn) ImGui::PopStyleColor();
    const char* planeLabels[EditorGridPlaneCount] = { "XZ", "XY", "ZY" };
    ImGui::BeginDisabled(!m_ShowGrid);
    for (uint32_t plane = 0; plane < EditorGridPlaneCount; ++plane)
    {
        ImGui::SameLine();
        const bool planeOn = m_GridPlanes[plane];
        if (planeOn) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(planeLabels[plane])) m_GridPlanes[plane] = !m_GridPlanes[plane];
        if (planeOn) ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();

    const auto toggleButton = [](const char* label, bool& value)
    {
        ImGui::SameLine();
        const bool on = value;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(label)) value = !value;
        if (on) ImGui::PopStyleColor();
    };
    toggleButton("Outline", m_ShowSelectionOutline);
    toggleButton("Bounds", m_ShowBounds);
}

void SceneViewPanel::HandleGizmoShortcuts(bool hovered)
{
    // RMB + WASD/QE flies the camera, so the shortcuts only apply while not navigating.
    if (m_Navigating || !hovered || ImGui::GetIO().WantTextInput || ImGuizmo::IsUsing())
    {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) m_GizmoOperation = GizmoOperation::Translate;
    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) m_GizmoOperation = GizmoOperation::Rotate;
    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) m_GizmoOperation = GizmoOperation::Scale;
    if (ImGui::IsKeyPressed(ImGuiKey_T, false)) m_GizmoLocal = !m_GizmoLocal;
    if (ImGui::IsKeyPressed(ImGuiKey_G, false)) m_ShowGrid = !m_ShowGrid;
}

void SceneViewPanel::DrawGizmo(GameObject* selected, const RenderView& view, const ImVec2& min, const ImVec2& max)
{
    if (!selected)
    {
        return;
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(min.x, min.y, max.x - min.x, max.y - min.y);

    TransformComponent* transform = selected->GetTransform();
    Matrix4 world = transform->GetLocalToWorldMatrix();

    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    float snapStep[3] = { 0.5f, 0.5f, 0.5f };
    if (m_GizmoOperation == GizmoOperation::Rotate)
    {
        operation = ImGuizmo::ROTATE;
        snapStep[0] = snapStep[1] = snapStep[2] = 15.0f;
    }
    else if (m_GizmoOperation == GizmoOperation::Scale)
    {
        operation = ImGuizmo::SCALE;
        snapStep[0] = snapStep[1] = snapStep[2] = 0.1f;
    }
    // Scale is always applied in the object's own axes.
    const ImGuizmo::MODE mode = (m_GizmoLocal || operation == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    const bool snap = ImGui::GetIO().KeyCtrl;

    // Engine matrices are row-major with row vectors (translation in the last row), the layout ImGuizmo expects.
    if (ImGuizmo::Manipulate(view.view.Data(), view.projection.Data(), operation, mode, world.Data(), nullptr,
        snap ? snapStep : nullptr))
    {
        // The gizmo edits the world matrix; bring it back into the parent's space.
        Matrix4 local = world;
        if (const TransformComponent* parent = transform->GetParent())
        {
            local = world * parent->GetLocalToWorldMatrix().Inverse();
        }

        DirectX::XMVECTOR scale, rotation, translation;
        if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, local.ToXM()))
        {
            // Write back only what this operation changes, so moving never adds rounding to rotation or scale.
            switch (m_GizmoOperation)
            {
            case GizmoOperation::Translate: transform->SetPosition(Vector3::FromXM(translation)); break;
            case GizmoOperation::Rotate: transform->SetRotation(Quaternion::FromXM(rotation).Normalized()); break;
            case GizmoOperation::Scale: transform->SetScale(Vector3::FromXM(scale)); break;
            }
        }
    }
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

    if (!playing)
    {
        ImGui::SetCursorPos(ImVec2(kRightMargin, toolbarY));
        DrawGizmoToolbar();
    }

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
    m_Position = transform->GetWorldPosition();
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
