#pragma once

#include "EditorContext.h"
#include "Panels/RenderDebugMenu.h"
#include "Snapping/SceneSnapper.h"

#include <imgui.h>

#include <optional>

class SceneViewPanel
{
public:
    static constexpr const char* kName = "Scene";

    enum class PlayRequest
    {
        None,
        Play,
        Stop
    };

    // Also tells the UI system where the scene image sits so game UI receives the mouse while playing.
    // In edit mode the selected object gets a transform gizmo.
    PlayRequest Draw(EditorContext& context, Canape::Scene* scene, Canape::Renderer& renderer, Canape::UISystem& ui,
        float deltaTime, bool playing);
    void FrameMainCamera(Canape::Scene* scene);
    void RequestPickAt(Canape::Renderer& renderer, float u, float v);

private:
    enum class GizmoOperation { Translate, Rotate, Scale };

    PlayRequest DrawToolbar(Canape::Renderer& renderer, bool playing);
    void DrawGizmoToolbar(float scale);
    static float GetGizmoToolbarWidth();
    void DrawSnapPopup();
    void HandleGizmoShortcuts(bool hovered);
    void DrawGizmo(Canape::GameObject* selected, const Canape::RenderView& view, const ImVec2& min, const ImVec2& max);

    // Snapping: surface snap while dragging the move gizmo, and a vertex handle that drags the selection by one of its vertices.
    SnapMode GetEffectiveSnapMode() const;
    bool IsGridSnapActive() const;
    SceneSnapper::Query MakeSnapQuery(const Canape::RenderView& view, const ImVec2& min, const ImVec2& max,
        const Canape::GameObject* exclude) const;
    void UpdateVertexHandle(Canape::GameObject* selected, const Canape::RenderView& view, const ImVec2& min, const ImVec2& max,
        bool hovered);
    void DrawSnapOverlay(const Canape::RenderView& view, const ImVec2& min, const ImVec2& max);
    float SurfaceRestOffset(Canape::GameObject& target, const Canape::Quaternion& newRotation, const Canape::Vector3& normal) const;
    static void SetWorldPose(Canape::TransformComponent& transform, const Canape::Vector3& position,
        const Canape::Quaternion* rotation);
    void DrawSelectionBounds(Canape::GameObject& selected, const Canape::RenderView& view, const ImVec2& min, const ImVec2& max);
    void RequestPick(Canape::Renderer& renderer, uint32_t x, uint32_t y);
    void UpdateCamera(bool hovered, float deltaTime);
    void FocusOn(Canape::GameObject& target);
    void UpdateFocus(float deltaTime);
    Canape::Quaternion GetRotation() const;
    Canape::RenderView BuildView(float aspectRatio) const;

    GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
    bool m_GizmoLocal = false;
    bool m_ShowGrid = true;
    bool m_ShowSelectionOutline = true;
    bool m_ShowBounds = true;
    bool m_PickPending = false;
    bool m_GridPlanes[Canape::EditorGridPlaneCount] = { true, false, false };

    RenderDebugMenu m_RenderDebugMenu;
    Canape::Vector3 m_Position{ 0.0f, 3.0f, -10.0f };
    float m_Pitch = 15.0f;
    float m_Yaw = 0.0f;
    float m_MoveSpeed = 8.0f;
    float m_FieldOfView = 60.0f;
    bool m_Navigating = false;

    bool m_Focusing = false;
    float m_FocusElapsed = 0.0f;
    Canape::Vector3 m_FocusFrom{};
    Canape::Vector3 m_FocusTo{};

    // Snap settings
    SnapMode m_SnapMode = SnapMode::Off;
    bool m_AlignToSurface = false;
    bool m_GridSnap = false;
    float m_GridCellSize = 1.0f;
    float m_RotateSnap = 15.0f;
    float m_ScaleSnap = 0.1f;
    bool m_VertexKeyHeld = false;
    bool m_OpenSnapPopup = false;
    ImVec2 m_SnapPopupPos{};

    // Snap state
    SceneSnapper m_Snapper;
    std::optional<SnapTarget> m_SnapPreview;
    std::optional<Canape::Vector3> m_HoverSourceVertex;
    bool m_GizmoDragging = false;
    Canape::Vector3 m_DragStartPosition{};
    Canape::Quaternion m_DragStartRotation = Canape::Quaternion::Identity;
    bool m_VertexDragging = false;
    SnapMode m_VertexDragMode = SnapMode::Vertex;
    Canape::Vector3 m_VertexDragSource{};
    Canape::Vector3 m_VertexDragStartPosition{};
};
