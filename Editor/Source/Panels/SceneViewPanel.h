#pragma once

#include "EditorContext.h"
#include "Panels/RenderDebugMenu.h"
#include "Snapping/SceneSnapper.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <vector>

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
    void GetGizmoSettings(int& operation, int& mode, float* snapStep, const float*& snap) const;
    Canape::Matrix4 BuildMultiGizmoMatrix(EditorContext& context, Canape::Scene& scene, Canape::GameObject& primary) const;
    void DrawMultiGizmo(EditorContext& context, Canape::GameObject& primary, const Canape::RenderView& view, const ImVec2& min,
        const ImVec2& max);
    void ApplyMultiDrag(Canape::Scene& scene);

    // Snapping: surface snap while dragging the move gizmo, and a vertex handle that drags the selection by one of its vertices.
    SnapMode GetEffectiveSnapMode() const;
    bool IsGridSnapActive() const;
    SceneSnapper::Query MakeSnapQuery(const Canape::RenderView& view, const ImVec2& min, const ImVec2& max,
        bool excludeSelection) const;
    void UpdateVertexHandle(Canape::GameObject* selected, const Canape::RenderView& view, const ImVec2& min, const ImVec2& max,
        bool hovered);
    void DrawSnapOverlay(const Canape::RenderView& view, const ImVec2& min, const ImVec2& max);
    float SurfaceRestOffset(Canape::GameObject& target, const Canape::Quaternion& newRotation, const Canape::Vector3& normal) const;
    static void SetWorldPose(Canape::TransformComponent& transform, const Canape::Vector3& position,
        const Canape::Quaternion* rotation);
    // A vertex-snap drag edits the primary selection; the rest of a multi-selection follows it by the same change.
    void ApplyToOtherSelected(EditorContext& context, Canape::GameObject& primary, const Canape::Matrix4& worldBefore);
    void DrawSelectionBounds(Canape::GameObject& selected, const Canape::RenderView& view, const ImVec2& min, const ImVec2& max);
    void RequestPick(Canape::Renderer& renderer, uint32_t x, uint32_t y);
    // RMB: fly (WASD/QE, wheel = speed), Alt+LMB: orbit, MMB: pan, wheel: zoom. viewportHeight scales panning.
    void UpdateCamera(bool hovered, float deltaTime, float viewportHeight);
    bool IsCameraBusy() const { return m_Navigating || m_Orbiting || m_Panning; }
    Canape::Vector3 GetOrbitPivot() const;

    // Content browser -> scene view: a prefab or mesh file dropped on the image is placed on the surface under the mouse
    // (or on the ground plane y = 0 when nothing is under it).
    static bool IsPlaceableAsset(const std::string& assetPath);
    void PlaceDroppedAsset(EditorContext& context, Canape::Scene& scene, const Canape::RenderView& view, const ImVec2& min,
        const ImVec2& max);
    Canape::GameObject* CreateFromAsset(Canape::Scene& scene, const std::string& assetPath);
    void FocusOn(Canape::GameObject& target);
    void UpdateFocus(float deltaTime);
    Canape::Quaternion GetRotation() const;
    Canape::RenderView BuildView(float aspectRatio) const;

    GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
    bool m_GizmoLocal = false;
    bool m_PivotCenter = true;
    bool m_ShowGrid = true;
    bool m_ShowSelectionOutline = true;
    bool m_ShowBounds = true;
    bool m_PickPending = false;
    bool m_PickAdditive = false; // Ctrl+click: toggle the picked object in the selection
    bool m_GridPlanes[Canape::EditorGridPlaneCount] = { true, false, false };

    RenderDebugMenu m_RenderDebugMenu;
    Canape::Vector3 m_Position{ 0.0f, 3.0f, -10.0f };
    float m_Pitch = 15.0f;
    float m_Yaw = 0.0f;
    float m_MoveSpeed = 8.0f;
    float m_FieldOfView = 60.0f;
    bool m_Navigating = false;
    bool m_Orbiting = false;
    bool m_Panning = false;
    float m_OrbitDistance = 10.0f; // distance from the camera to the point Alt+LMB orbits around (set by focus and zoom)
    std::string m_PendingDropPath;  // game-relative asset dropped this frame

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
    struct MultiDragItem
    {
        uint64_t id = 0;
        Canape::Vector3 position;
        Canape::Quaternion rotation = Canape::Quaternion::Identity;
        Canape::Vector3 scale = Canape::Vector3::One;
    };
    Canape::Matrix4 m_MultiGizmo = Canape::Matrix4::Identity;
    Canape::Matrix4 m_MultiGizmoStart = Canape::Matrix4::Identity;
    std::vector<MultiDragItem> m_MultiDragItems;
    std::vector<const Canape::GameObject*> m_SnapExclude;
    Canape::Vector3 m_DragStartPosition{};
    Canape::Quaternion m_DragStartRotation = Canape::Quaternion::Identity;
    bool m_VertexDragging = false;
    SnapMode m_VertexDragMode = SnapMode::Vertex;
    Canape::Vector3 m_VertexDragSource{};
    Canape::Vector3 m_VertexDragStartPosition{};
};
