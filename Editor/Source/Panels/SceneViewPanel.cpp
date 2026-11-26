#include "Panels/SceneViewPanel.h"
#include "EditorUtils.h"
#include "Asset/Mesh/MeshSerializer.h"
#include "ScenePicking.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace Canape;

namespace
{
    constexpr float kLookSensitivity = 0.15f;
    constexpr float kFastMultiplier = 3.0f;
    constexpr float kNearClip = 0.1f;
    constexpr float kFarClip = 2000.0f;
    constexpr float kFocusDuration = 0.3f;
    constexpr float kFocusPadding = 1.2f;
    constexpr float kMinFocusRadius = 0.5f;
    constexpr float kLocalButtonWidth = 52.0f;
    constexpr float kSnapButtonWidth = 118.0f;
    constexpr float kOrbitSensitivity = 0.3f;    // degrees per pixel
    constexpr float kMinOrbitDistance = 0.1f;
    constexpr float kZoomStep = 0.15f;           // fraction of the orbit distance per wheel notch
    constexpr float kDropFallbackDistance = 10.0f;
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
    renderer.SetEditorGridCellSize(m_GridCellSize);
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
            GameObject* picked = ScenePicking::FindOwnerOfComponent(*scene, pickedId);
            if (m_PickAdditive)
            {
                // Ctrl+click adds or removes the picked object; Ctrl+click on empty space keeps the selection.
                if (picked)
                {
                    context.ToggleSelect(picked);
                }
            }
            else if (picked)
            {
                context.Select(picked);
            }
            else
            {
                context.ClearSelection();
            }
        }
    }

    if (const uint64_t focusId = context.TakeFocusRequest(); focusId != 0 && scene && !playing)
    {
        if (GameObject* target = scene->GetGameObject(scene->FindGameObjectById(focusId)))
        {
            FocusOn(*target);
        }
    }

    GameObject* selected = scene && !playing ? context.GetSelected(scene) : nullptr;
    OutlineStyle selectionStyle;
    selectionStyle.Color = { 1.0f, 0.55f, 0.1f };
    selectionStyle.Thickness = 2;
    selectionStyle.Opacity = 1.0f;
    std::vector<uint64_t> outlineIds;
    if (scene && !playing && m_ShowSelectionOutline)
    {
        for (GameObject* gameObject : context.GetSelectedObjects(scene))
        {
            const std::vector<uint64_t> ids = ScenePicking::SelectableComponentIds(*gameObject);
            outlineIds.insert(outlineIds.end(), ids.begin(), ids.end());
        }
    }
    renderer.SetSelectionOutline(std::move(outlineIds), selectionStyle);

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
            m_Orbiting = false;
            m_Panning = false;
            m_PendingDropPath.clear();
            m_Focusing = false;
            m_VertexDragging = false;
            m_GizmoDragging = false;
            m_SnapPreview.reset();
            m_HoverSourceVertex.reset();
            renderer.ClearViewOverride();
            ImGui::GetWindowDrawList()->AddRect(min, max, IM_COL32(80, 200, 120, 255), 0.0f, 0, 2.0f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 8.0f, min.y + 6.0f), IM_COL32(80, 200, 120, 255),
                "Playing (Main Camera)");
        }
        else
        {
            const bool hovered = ImGui::IsItemHovered();

            // Prefab / mesh files dragged from the content browser. Only placeable files light the image up.
            if (scene)
            {
                const ImGuiPayload* dragging = ImGui::GetDragDropPayload();
                if (dragging && dragging->IsDataType(EditorUtils::kAssetPathPayload)
                    && IsPlaceableAsset(std::string(static_cast<const char*>(dragging->Data), static_cast<size_t>(dragging->DataSize)))
                    && ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kAssetPathPayload))
                    {
                        m_PendingDropPath.assign(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
                    }
                    ImGui::EndDragDropTarget();
                }
            }

            UpdateCamera(hovered, deltaTime, static_cast<float>(height));
            UpdateFocus(deltaTime);
            HandleGizmoShortcuts(hovered);
            if (hovered && !IsCameraBusy() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false) && selected)
            {
                FocusOn(*selected);
            }
            m_VertexKeyHeld = !IsCameraBusy() && !ImGui::GetIO().WantTextInput && ImGui::IsKeyDown(ImGuiKey_V)
                && (hovered || m_GizmoDragging || m_VertexDragging);
            const RenderView view = BuildView(static_cast<float>(width) / static_cast<float>(height));
            renderer.SetViewOverride(view);
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 8.0f, max.y - ImGui::GetTextLineHeight() - 6.0f),
                IM_COL32(255, 255, 255, 160),
                "W/E/R: move/rotate/scale  T: local/world  G: grid  V(hold): vertex snap  Ctrl(hold): grid snap  F: focus  "
                "RMB: look  WASD/QE: move  Alt+LMB: orbit  MMB: pan  Wheel: zoom  RMB+Wheel: speed");

            if (!m_PendingDropPath.empty() && scene)
            {
                PlaceDroppedAsset(context, *scene, view, min, max);
                selected = context.GetSelected(scene);
            }

            if (selected && m_ShowBounds)
            {
                DrawSelectionBounds(*selected, view, min, max);
            }
            m_SnapPreview.reset();
            // Alt+LMB orbits the camera: the gizmo must not grab that click.
            ImGuizmo::Enable(!IsCameraBusy() && !ImGui::GetIO().KeyAlt);
            const Matrix4 worldBefore = selected ? selected->GetTransform()->GetLocalToWorldMatrix() : Matrix4::Identity;
            const Vector3 scaleBefore = selected ? selected->GetTransform()->GetScale() : Vector3::One;
            DrawGizmo(selected, view, min, max);
            UpdateVertexHandle(selected, view, min, max, hovered);
            if (selected && (m_GizmoDragging || m_VertexDragging))
            {
                ApplyToOtherSelected(context, *selected, worldBefore, scaleBefore);
                const char* verb = m_VertexDragging ? "Snap"
                    : m_GizmoOperation == GizmoOperation::Rotate ? "Rotate"
                    : m_GizmoOperation == GizmoOperation::Scale ? "Scale" : "Move";
                const size_t count = context.GetSelectionCount();
                context.SetUndoLabel(std::string(verb) + " "
                    + (count > 1 ? std::to_string(count) + " Objects" : selected->GetName()));
            }
            DrawSnapOverlay(view, min, max);

            const bool overGizmo = selected && (ImGuizmo::IsOver() || ImGuizmo::IsUsing());
            const bool overVertexHandle = m_VertexDragging || m_HoverSourceVertex.has_value();
            if (hovered && !IsCameraBusy() && !ImGui::GetIO().KeyAlt && !overGizmo && !overVertexHandle
                && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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

bool SceneViewPanel::IsPlaceableAsset(const std::string& assetPath)
{
    std::string extension = EditorUtils::ToUtf8(EditorUtils::FromUtf8(assetPath).extension());
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
    // Static meshes only: a skinned mesh (.cnskmesh) needs a skeleton, so it is placed through its prefab.
    return extension == ".prefab" || extension == MeshSerializer::StaticExtension || extension == ".fbx" || extension == ".gltf";
}

GameObject* SceneViewPanel::CreateFromAsset(Scene& scene, const std::string& assetPath)
{
    const std::filesystem::path file = EditorUtils::GetGameDirectory() / EditorUtils::FromUtf8(assetPath);
    const std::string fileUtf8 = EditorUtils::ToUtf8(file);
    if (EditorUtils::ToUtf8(file.extension()) == ".prefab")
    {
        GameObject* created = SceneSerializer::InstantiatePrefab(scene, fileUtf8, nullptr);
        if (!created)
        {
            CN_LOG_GAME("Editor", "Cannot place prefab ", assetPath);
        }
        return created;
    }

    // A mesh file becomes an object with a MeshRenderer, named after the file.
    GameObject* created = scene.GetGameObject(scene.AddGameObject(EditorUtils::ToUtf8(file.stem())));
    MeshRendererComponent* renderer = created ? created->AddComponent<MeshRendererComponent>() : nullptr;
    if (!renderer || !renderer->SetMesh(fileUtf8))
    {
        CN_LOG_GAME("Editor", "Cannot place mesh ", assetPath, " (the mesh did not load)");
        if (created)
        {
            scene.RemoveGameObject(created->GetHandle());
        }
        return nullptr;
    }
    return created;
}

void SceneViewPanel::PlaceDroppedAsset(EditorContext& context, Scene& scene, const RenderView& view, const ImVec2& min,
    const ImVec2& max)
{
    const std::string assetPath = std::exchange(m_PendingDropPath, {});
    const SceneSnapper::Query query = MakeSnapQuery(view, min, max, nullptr);

    // The surface under the mouse, else the ground plane (y = 0), else a point in front of the camera.
    Vector3 point;
    Vector3 normal = Vector3::Up;
    if (const std::optional<SnapTarget> hit = m_Snapper.FindTarget(scene, SnapMode::Face, query))
    {
        point = hit->position;
        normal = hit->normal;
    }
    else
    {
        const SnapMath::Ray& ray = query.ray;
        const float t = std::abs(ray.direction.Y) > 1e-4f ? -ray.origin.Y / ray.direction.Y : -1.0f;
        point = (t > 0.0f && t < kFarClip) ? ray.origin + ray.direction * t
            : ray.origin + ray.direction * kDropFallbackDistance;
    }

    GameObject* created = CreateFromAsset(scene, assetPath);
    if (!created)
    {
        return;
    }
    // Rest the object's bounds on the surface instead of sinking its pivot into it; the rotation is kept as authored.
    TransformComponent* transform = created->GetTransform();
    const float offset = SurfaceRestOffset(*created, transform->GetWorldRotation(), normal);
    SetWorldPose(*transform, point + normal * offset, nullptr);

    context.Select(created);
    context.SetUndoLabel("Place " + created->GetName());
}

void SceneViewPanel::RequestPick(Renderer& renderer, uint32_t x, uint32_t y)
{
    renderer.RequestPick(x, y);
    m_PickPending = true;
    m_PickAdditive = ImGui::GetIO().KeyCtrl;
}

void SceneViewPanel::ApplyToOtherSelected(EditorContext& context, GameObject& primary, const Matrix4& worldBefore,
    const Vector3& scaleBefore)
{
    if (context.GetSelectionCount() < 2 || !primary.GetScene())
    {
        return;
    }
    TransformComponent* primaryTransform = primary.GetTransform();
    const Matrix4 worldAfter = primaryTransform->GetLocalToWorldMatrix();
    if (std::memcmp(worldBefore.Data(), worldAfter.Data(), sizeof(float) * 16) == 0)
    {
        return;
    }

    // Top-most selected objects only (children follow their parents), and never the primary's own ancestors or
    // descendants: those already move with it.
    std::vector<TransformComponent*> others;
    for (GameObject* gameObject : context.GetSelectedRoots(primary.GetScene()))
    {
        TransformComponent* transform = gameObject->GetTransform();
        if (gameObject != &primary && !transform->IsChildOf(primaryTransform) && !primaryTransform->IsChildOf(transform))
        {
            others.push_back(transform);
        }
    }
    if (others.empty())
    {
        return;
    }

    if (m_GizmoOperation == GizmoOperation::Scale && !m_VertexDragging)
    {
        // Scale: every object scales along its own axes by the same factor as the primary.
        const Vector3 scaleAfter = primaryTransform->GetScale();
        const auto ratio = [](float after, float before) { return std::fabs(before) > 1e-6f ? after / before : 1.0f; };
        const Vector3 factor{ ratio(scaleAfter.X, scaleBefore.X), ratio(scaleAfter.Y, scaleBefore.Y), ratio(scaleAfter.Z, scaleBefore.Z) };
        for (TransformComponent* transform : others)
        {
            const Vector3 scale = transform->GetScale();
            transform->SetScale({ scale.X * factor.X, scale.Y * factor.Y, scale.Z * factor.Z });
        }
        return;
    }

    // Move / rotate / snap: the primary's world change (a translation, or a rotation about its pivot) applied to each.
    const Matrix4 delta = worldBefore.Inverse() * worldAfter;
    for (TransformComponent* transform : others)
    {
        Matrix4 world = transform->GetLocalToWorldMatrix() * delta;
        if (const TransformComponent* parent = transform->GetParent())
        {
            world = world * parent->GetWorldToLocalMatrix();
        }
        DirectX::XMVECTOR scale, rotation, translation;
        if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, world.ToXM()))
        {
            transform->SetPosition(Vector3::FromXM(translation));
            transform->SetRotation(Quaternion::FromXM(rotation).Normalized());
            transform->SetScale(Vector3::FromXM(scale));
        }
    }
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

float SceneViewPanel::GetGizmoToolbarWidth()
{
    // Must match the buttons DrawGizmoToolbar draws.
    const ImGuiStyle& style = ImGui::GetStyle();
    float width = kLocalButtonWidth + kSnapButtonWidth;
    for (const char* label : { "Move", "Rotate", "Scale", "Grid", "XZ", "XY", "ZY", "Outline", "Bounds" })
    {
        width += ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
    }
    return width + style.ItemSpacing.x * 10.0f;
}

void SceneViewPanel::DrawGizmoToolbar(float scale)
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
    if (ImGui::Button(m_GizmoLocal ? "Local" : "World", ImVec2(kLocalButtonWidth * scale, 0.0f)))
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

    ImGui::SameLine();
    char label[48];
    std::snprintf(label, sizeof(label), "Snap: %s%s##SnapSettings", GetSnapModeName(m_SnapMode), m_GridSnap ? "+Grid" : "");
    const bool snapOn = m_SnapMode != SnapMode::Off || m_GridSnap;
    if (snapOn) ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(label, ImVec2(kSnapButtonWidth * scale, 0.0f)))
    {
        // Opened after the toolbar pops its scaled font, so the popup keeps the normal size.
        m_OpenSnapPopup = true;
        m_SnapPopupPos = ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + 2.0f);
    }
    if (snapOn) ImGui::PopStyleColor();
}

void SceneViewPanel::DrawSnapPopup()
{
    constexpr const char* kPopupId = "##SnapSettingsPopup";
    if (m_OpenSnapPopup)
    {
        m_OpenSnapPopup = false;
        ImGui::OpenPopup(kPopupId);
        ImGui::SetNextWindowPos(m_SnapPopupPos);
    }
    if (!ImGui::BeginPopup(kPopupId))
    {
        return;
    }

    ImGui::SeparatorText("Surface snap (move)");
    for (SnapMode mode : { SnapMode::Off, SnapMode::Vertex, SnapMode::Edge, SnapMode::Face })
    {
        if (mode != SnapMode::Off) ImGui::SameLine();
        if (ImGui::RadioButton(GetSnapModeName(mode), m_SnapMode == mode)) m_SnapMode = mode;
    }
    ImGui::BeginDisabled(m_SnapMode != SnapMode::Face);
    ImGui::Checkbox("Align up axis to surface normal", &m_AlignToSurface);
    ImGui::EndDisabled();
    ImGui::TextDisabled("Hold V for vertex snap in any mode.");
    ImGui::TextDisabled("Vertex/Edge: drag from a vertex of the selection (white box)\nor drag the move gizmo to snap its pivot.");

    ImGui::SeparatorText("Grid snap");
    ImGui::Checkbox("Snap to grid", &m_GridSnap);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Cell size", &m_GridCellSize, 0.01f, 0.01f, 100.0f, "%.2f m", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Rotate step", &m_RotateSnap, 0.5f, 1.0f, 90.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Scale step", &m_ScaleSnap, 0.01f, 0.01f, 10.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::TextDisabled("Hold Ctrl to toggle grid snap while dragging.\nThe scene grid uses the same cell size.");
    ImGui::EndPopup();
}

void SceneViewPanel::HandleGizmoShortcuts(bool hovered)
{
    // RMB + WASD/QE flies the camera, so the shortcuts only apply while not navigating.
    if (IsCameraBusy() || !hovered || ImGui::GetIO().WantTextInput || ImGuizmo::IsUsing())
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
        m_GizmoDragging = false;
        return;
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(min.x, min.y, max.x - min.x, max.y - min.y);

    TransformComponent* transform = selected->GetTransform();
    Matrix4 world = transform->GetLocalToWorldMatrix();

    const bool gridSnap = IsGridSnapActive();
    ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
    float snapStep[3] = { 0.0f, 0.0f, 0.0f };
    const float* snap = nullptr; // translation snaps to the absolute world grid below, not relative to the drag start
    if (m_GizmoOperation == GizmoOperation::Rotate)
    {
        operation = ImGuizmo::ROTATE;
        snapStep[0] = snapStep[1] = snapStep[2] = m_RotateSnap;
        snap = gridSnap ? snapStep : nullptr;
    }
    else if (m_GizmoOperation == GizmoOperation::Scale)
    {
        operation = ImGuizmo::SCALE;
        snapStep[0] = snapStep[1] = snapStep[2] = m_ScaleSnap;
        snap = gridSnap ? snapStep : nullptr;
    }
    // Scale is always applied in the object's own axes.
    const ImGuizmo::MODE mode = (m_GizmoLocal || operation == ImGuizmo::SCALE) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;

    // Engine matrices are row-major with row vectors (translation in the last row), the layout ImGuizmo expects.
    const bool changed = ImGuizmo::Manipulate(view.view.Data(), view.projection.Data(), operation, mode, world.Data(), nullptr, snap);
    const bool dragging = ImGuizmo::IsUsing();
    if (dragging && !m_GizmoDragging)
    {
        m_DragStartPosition = transform->GetWorldPosition();
        m_DragStartRotation = transform->GetWorldRotation();
    }
    m_GizmoDragging = dragging;

    if (m_GizmoOperation == GizmoOperation::Translate)
    {
        if (!dragging || m_VertexDragging)
        {
            return;
        }
        // ImGuizmo recomputes the dragged position from the mouse ray every frame, so overriding it here never accumulates.
        Vector3 position = world.GetTranslation();
        const SnapMode snapMode = GetEffectiveSnapMode();
        std::optional<Quaternion> rotation;
        if (m_AlignToSurface && snapMode == SnapMode::Face)
        {
            rotation = m_DragStartRotation;
        }

        std::optional<SnapTarget> target;
        if (snapMode != SnapMode::Off && selected->GetScene())
        {
            target = m_Snapper.FindTarget(*selected->GetScene(), snapMode, MakeSnapQuery(view, min, max, selected));
        }
        if (target)
        {
            m_SnapPreview = target;
            if (snapMode == SnapMode::Face)
            {
                if (rotation)
                {
                    rotation = SnapMath::RotationBetween(m_DragStartRotation.Rotate(Vector3::Up).Normalized(), target->normal)
                        * m_DragStartRotation;
                }
                position = target->position
                    + target->normal * SurfaceRestOffset(*selected, rotation.value_or(transform->GetWorldRotation()), target->normal);
            }
            else
            {
                position = target->position;
            }
        }
        else if (gridSnap)
        {
            position = SnapMath::SnapMovedAxesToGrid(position, m_DragStartPosition, m_GridCellSize);
        }
        else if (!changed && !rotation)
        {
            return;
        }
        SetWorldPose(*transform, position, rotation ? &*rotation : nullptr);
        return;
    }

    if (changed)
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

SnapMode SceneViewPanel::GetEffectiveSnapMode() const
{
    return m_VertexKeyHeld ? SnapMode::Vertex : m_SnapMode;
}

bool SceneViewPanel::IsGridSnapActive() const
{
    // Holding Ctrl flips the grid snap setting for the current drag.
    return m_GridSnap != ImGui::GetIO().KeyCtrl;
}

SceneSnapper::Query SceneViewPanel::MakeSnapQuery(const RenderView& view, const ImVec2& min, const ImVec2& max,
    const GameObject* exclude) const
{
    const ImVec2 mouse = ImGui::GetMousePos();
    SceneSnapper::Query query;
    query.viewProjection = view.view * view.projection;
    query.rect = { min.x, min.y, max.x - min.x, max.y - min.y };
    query.mouse = { mouse.x, mouse.y };
    query.ray = SnapMath::ScreenRay(query.viewProjection, query.rect, mouse.x, mouse.y);
    query.exclude = exclude;
    return query;
}

float SceneViewPanel::SurfaceRestOffset(GameObject& target, const Quaternion& newRotation, const Vector3& normal) const
{
    // Distance from the pivot to the object's lowest point along the normal once it has the new rotation.
    const TransformComponent* transform = target.GetTransform();
    const Vector3 pivot = transform->GetWorldPosition();
    const Quaternion delta = newRotation * transform->GetWorldRotation().Inverse();
    std::vector<Vector3> points;
    for (const ScenePicking::OrientedBox& box : ScenePicking::SelectionBounds(target))
    {
        for (int corner = 0; corner < 8; ++corner)
        {
            const Vector3 local{ (corner & 1) ? box.max.X : box.min.X, (corner & 2) ? box.max.Y : box.min.Y,
                (corner & 4) ? box.max.Z : box.min.Z };
            points.push_back(delta.Rotate(box.toWorld.TransformPoint(local) - pivot));
        }
    }
    return -SnapMath::MinExtentAlong(points, Vector3{}, normal);
}

void SceneViewPanel::SetWorldPose(TransformComponent& transform, const Vector3& position, const Quaternion* rotation)
{
    const TransformComponent* parent = transform.GetParent();
    const Matrix4 parentInverse = parent ? parent->GetLocalToWorldMatrix().Inverse() : Matrix4::Identity;
    if (rotation)
    {
        DirectX::XMVECTOR scale, currentRotation, translation;
        if (DirectX::XMMatrixDecompose(&scale, &currentRotation, &translation, transform.GetLocalToWorldMatrix().ToXM()))
        {
            const Matrix4 local = Matrix4::TRS(position, *rotation, Vector3::FromXM(scale)) * parentInverse;
            DirectX::XMVECTOR localScale, localRotation, localTranslation;
            if (DirectX::XMMatrixDecompose(&localScale, &localRotation, &localTranslation, local.ToXM()))
            {
                transform.SetRotation(Quaternion::FromXM(localRotation).Normalized());
            }
        }
    }
    transform.SetPosition(parentInverse.TransformPoint(position));
}

void SceneViewPanel::UpdateVertexHandle(GameObject* selected, const RenderView& view, const ImVec2& min, const ImVec2& max,
    bool hovered)
{
    constexpr float kHandleRadius = 10.0f;
    m_HoverSourceVertex.reset();
    if (!selected || !selected->GetScene() || IsCameraBusy() || (!m_VertexDragging && ImGui::GetIO().KeyAlt))
    {
        m_VertexDragging = false;
        return;
    }

    if (m_VertexDragging)
    {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_VertexDragging = false;
            return;
        }
        const SceneSnapper::Query query = MakeSnapQuery(view, min, max, selected);
        Vector3 target;
        if (const std::optional<SnapTarget> snapped = m_Snapper.FindTarget(*selected->GetScene(), m_VertexDragMode, query))
        {
            m_SnapPreview = snapped;
            target = snapped->position;
        }
        else
        {
            // Nothing to snap to: the grabbed vertex follows the cursor on the camera-facing plane through it.
            const Vector3 forward = GetRotation().Rotate(Vector3::Forward);
            const float denominator = Vector3::Dot(query.ray.direction, forward);
            if (std::abs(denominator) < 1e-5f)
            {
                return;
            }
            const float t = Vector3::Dot(m_VertexDragSource - query.ray.origin, forward) / denominator;
            if (t <= 0.0f)
            {
                return;
            }
            target = query.ray.origin + query.ray.direction * t;
        }
        SetWorldPose(*selected->GetTransform(), m_VertexDragStartPosition + (target - m_VertexDragSource), nullptr);
        m_HoverSourceVertex = target;
        return;
    }

    const SnapMode mode = GetEffectiveSnapMode();
    if ((mode != SnapMode::Vertex && mode != SnapMode::Edge) || !hovered || ImGuizmo::IsOver() || ImGuizmo::IsUsing())
    {
        return;
    }
    m_HoverSourceVertex = m_Snapper.FindNearestVertex(*selected, MakeSnapQuery(view, min, max, nullptr), kHandleRadius);
    if (m_HoverSourceVertex && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        m_VertexDragging = true;
        m_VertexDragMode = mode;
        m_VertexDragSource = *m_HoverSourceVertex;
        m_VertexDragStartPosition = selected->GetTransform()->GetWorldPosition();
    }
}

void SceneViewPanel::DrawSnapOverlay(const RenderView& view, const ImVec2& min, const ImVec2& max)
{
    if (!m_SnapPreview && !m_HoverSourceVertex)
    {
        return;
    }
    const Matrix4 viewProjection = view.view * view.projection;
    const SnapMath::ScreenRect rect{ min.x, min.y, max.x - min.x, max.y - min.y };
    const auto toScreen = [&](const Vector3& world) -> std::optional<ImVec2>
    {
        if (const std::optional<Vector2> screen = SnapMath::WorldToScreen(viewProjection, rect, world))
        {
            return ImVec2(screen->X, screen->Y);
        }
        return std::nullopt;
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(min, max, true);

    if (m_SnapPreview)
    {
        ImU32 color = IM_COL32(80, 220, 255, 255);
        if (m_SnapPreview->mode == SnapMode::Edge) color = IM_COL32(255, 210, 60, 255);
        if (m_SnapPreview->mode == SnapMode::Face) color = IM_COL32(120, 255, 120, 255);

        if (m_SnapPreview->hasEdge)
        {
            const std::optional<ImVec2> a = toScreen(m_SnapPreview->edgeA);
            const std::optional<ImVec2> b = toScreen(m_SnapPreview->edgeB);
            if (a && b)
            {
                drawList->AddLine(*a, *b, color, 3.0f);
            }
        }
        if (const std::optional<ImVec2> point = toScreen(m_SnapPreview->position))
        {
            if (m_SnapPreview->mode == SnapMode::Face)
            {
                if (const std::optional<ImVec2> tip = toScreen(m_SnapPreview->position + m_SnapPreview->normal * 0.5f))
                {
                    drawList->AddLine(*point, *tip, color, 2.0f);
                }
                drawList->AddCircle(*point, 7.0f, color, 0, 2.0f);
            }
            else
            {
                drawList->AddCircleFilled(*point, 5.0f, color);
                drawList->AddCircle(*point, 8.0f, IM_COL32(0, 0, 0, 200), 0, 1.5f);
            }
        }
    }

    if (m_HoverSourceVertex)
    {
        if (const std::optional<ImVec2> point = toScreen(*m_HoverSourceVertex))
        {
            constexpr float kHalf = 6.0f;
            drawList->AddRect(ImVec2(point->x - kHalf, point->y - kHalf), ImVec2(point->x + kHalf, point->y + kHalf),
                IM_COL32(255, 255, 255, 255), 0.0f, 0, 2.0f);
        }
    }
    drawList->PopClipRect();
}

SceneViewPanel::PlayRequest SceneViewPanel::DrawToolbar(Renderer& renderer, bool playing)
{
    constexpr float kButtonWidth = 64.0f;
    constexpr float kMargin = 6.0f;
    constexpr float kMinScale = 0.35f;
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 framePadding = style.FramePadding;
    const ImVec2 itemSpacing = style.ItemSpacing;
    const float contentWidth = ImGui::GetWindowContentRegionMax().x;
    const float startX = ImGui::GetCursorPosX();
    const float startY = ImGui::GetCursorPosY();

    // Natural (unscaled) widths of the three groups: gizmo settings on the left, Play/Stop centred,
    // render debug menu on the right. A narrow window shrinks every button by the same factor so the
    // groups keep their left / centre / right placement on one row instead of overlapping.
    const float gizmoWidth = playing ? 0.0f : GetGizmoToolbarWidth();
    const float playWidth = kButtonWidth * 2.0f + itemSpacing.x;
    const float debugWidth = m_RenderDebugMenu.GetButtonWidth();
    const float sideWidth = kMargin + std::max(gizmoWidth, debugWidth) + itemSpacing.x * 2.0f;
    const float scale = std::clamp(contentWidth / (sideWidth * 2.0f + playWidth), kMinScale, 1.0f);

    const float margin = kMargin * scale;
    const float toolbarY = startY + framePadding.y * scale;

    ImGui::PushFont(nullptr, style.FontSizeBase * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(framePadding.x * scale, framePadding.y * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(itemSpacing.x * scale, itemSpacing.y * scale));
    const float rowHeight = ImGui::GetFrameHeightWithSpacing();

    if (!playing)
    {
        ImGui::SetCursorPos(ImVec2(margin, toolbarY));
        DrawGizmoToolbar(scale);
    }

    PlayRequest request = PlayRequest::None;
    const float buttonWidth = kButtonWidth * scale;
    ImGui::SetCursorPos(ImVec2(std::max((contentWidth - playWidth * scale) * 0.5f, 0.0f), toolbarY));

    ImGui::BeginDisabled(playing);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.25f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.6f, 0.3f, 1.0f));
    if (ImGui::Button("Play", ImVec2(buttonWidth, 0.0f)))
    {
        request = PlayRequest::Play;
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!playing);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.25f, 0.25f, 1.0f));
    if (ImGui::Button("Stop", ImVec2(buttonWidth, 0.0f)))
    {
        request = PlayRequest::Stop;
    }
    ImGui::PopStyleColor(2);
    ImGui::EndDisabled();

    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    DrawSnapPopup();

    // The debug menu scales only its button; its popup keeps the normal size.
    ImGui::SetCursorPos(ImVec2(std::max(contentWidth - debugWidth * scale - margin, 0.0f), toolbarY));
    m_RenderDebugMenu.Draw(renderer, scale);

    ImGui::SetCursorPos(ImVec2(startX, toolbarY + rowHeight + framePadding.y * scale));
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

void SceneViewPanel::FocusOn(GameObject& target)
{
    // World-space bounds of everything the selection outline covers (meshes in the subtree, or a proxy box).
    Vector3 boundsMin{ FLT_MAX, FLT_MAX, FLT_MAX };
    Vector3 boundsMax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const ScenePicking::OrientedBox& box : ScenePicking::SelectionBounds(target))
    {
        for (int corner = 0; corner < 8; ++corner)
        {
            const Vector3 local{ (corner & 1) ? box.max.X : box.min.X, (corner & 2) ? box.max.Y : box.min.Y,
                (corner & 4) ? box.max.Z : box.min.Z };
            const Vector3 world = box.toWorld.TransformPoint(local);
            boundsMin = { std::min(boundsMin.X, world.X), std::min(boundsMin.Y, world.Y), std::min(boundsMin.Z, world.Z) };
            boundsMax = { std::max(boundsMax.X, world.X), std::max(boundsMax.Y, world.Y), std::max(boundsMax.Z, world.Z) };
        }
    }
    if (boundsMin.X > boundsMax.X)
    {
        boundsMin = boundsMax = target.GetTransform()->GetWorldPosition();
    }

    const Vector3 center = (boundsMin + boundsMax) * 0.5f;
    const float radius = std::max((boundsMax - boundsMin).Length() * 0.5f, kMinFocusRadius);
    // Keep the current view direction and back off until the bounding sphere fits the vertical field of view.
    const float distance = radius / std::sin(Math::ToRadians(m_FieldOfView) * 0.5f) * kFocusPadding;
    const Vector3 forward = GetRotation().Rotate(Vector3::Forward);

    m_FocusFrom = m_Position;
    m_FocusTo = center - forward * distance;
    m_OrbitDistance = distance; // Alt+LMB then orbits around the focused object
    m_FocusElapsed = 0.0f;
    m_Focusing = true;
}

void SceneViewPanel::UpdateFocus(float deltaTime)
{
    if (!m_Focusing)
    {
        return;
    }
    // Moving the camera by hand takes over from the focus animation.
    if (IsCameraBusy())
    {
        m_Focusing = false;
        return;
    }

    m_FocusElapsed += deltaTime;
    const float t = std::clamp(m_FocusElapsed / kFocusDuration, 0.0f, 1.0f);
    const float eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    m_Position = m_FocusFrom + (m_FocusTo - m_FocusFrom) * eased;
    if (t >= 1.0f)
    {
        m_Focusing = false;
    }
}

Vector3 SceneViewPanel::GetOrbitPivot() const
{
    return m_Position + GetRotation().Rotate(Vector3::Forward) * m_OrbitDistance;
}

void SceneViewPanel::UpdateCamera(bool hovered, float deltaTime, float viewportHeight)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool idle = !IsCameraBusy();

    // A drag that starts on the image keeps going while the button is held, even outside the image.
    if (hovered && idle && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
    {
        m_Navigating = true;
    }
    if (hovered && idle && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGuizmo::IsUsing() && !m_VertexDragging)
    {
        m_Orbiting = true;
    }
    if (hovered && idle && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
    {
        m_Panning = true;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
    {
        m_Navigating = false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        m_Orbiting = false;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
    {
        m_Panning = false;
    }

    if (hovered && io.MouseWheel != 0.0f)
    {
        if (m_Navigating)
        {
            // RMB + wheel: fly speed (the previous wheel behaviour).
            m_MoveSpeed = std::clamp(m_MoveSpeed * (io.MouseWheel > 0.0f ? 1.2f : 1.0f / 1.2f), 0.5f, 200.0f);
        }
        else if (!m_Orbiting && !m_Panning)
        {
            // Wheel: zoom toward / away from the orbit pivot. Zooming in past the pivot pushes the pivot ahead.
            m_Focusing = false;
            const Vector3 forward = GetRotation().Rotate(Vector3::Forward);
            const float step = std::max(m_OrbitDistance * kZoomStep, kMinOrbitDistance) * io.MouseWheel;
            m_Position = m_Position + forward * step;
            m_OrbitDistance = std::max(m_OrbitDistance - step, kMinOrbitDistance);
        }
    }

    if (m_Orbiting)
    {
        const Vector3 pivot = GetOrbitPivot();
        m_Yaw += io.MouseDelta.x * kOrbitSensitivity;
        m_Pitch = std::clamp(m_Pitch + io.MouseDelta.y * kOrbitSensitivity, -89.0f, 89.0f);
        m_Position = pivot - GetRotation().Rotate(Vector3::Forward) * m_OrbitDistance;
        return;
    }

    if (m_Panning)
    {
        // The point under the mouse at the pivot's depth stays under the mouse.
        const Quaternion rotation = GetRotation();
        const float worldPerPixel = 2.0f * m_OrbitDistance * std::tan(Math::ToRadians(m_FieldOfView) * 0.5f)
            / std::max(viewportHeight, 1.0f);
        m_Position = m_Position - rotation.Rotate(Vector3::Right) * (io.MouseDelta.x * worldPerPixel)
            + rotation.Rotate(Vector3::Up) * (io.MouseDelta.y * worldPerPixel);
        return;
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
