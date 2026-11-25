#pragma once

#include "EditorContext.h"
#include "Panels/RenderDebugMenu.h"

#include <imgui.h>

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

private:
    enum class GizmoOperation { Translate, Rotate, Scale };

    PlayRequest DrawToolbar(Canape::Renderer& renderer, bool playing);
    void DrawGizmoToolbar();
    void HandleGizmoShortcuts(bool hovered);
    void DrawGizmo(Canape::GameObject* selected, const Canape::RenderView& view, const ImVec2& min, const ImVec2& max);
    void UpdateCamera(bool hovered, float deltaTime);
    Canape::Quaternion GetRotation() const;
    Canape::RenderView BuildView(float aspectRatio) const;

    GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
    bool m_GizmoLocal = false;
    bool m_ShowGrid = true;
    bool m_GridPlanes[Canape::EditorGridPlaneCount] = { true, false, false };

    RenderDebugMenu m_RenderDebugMenu;
    Canape::Vector3 m_Position{ 0.0f, 3.0f, -10.0f };
    float m_Pitch = 15.0f;
    float m_Yaw = 0.0f;
    float m_MoveSpeed = 8.0f;
    float m_FieldOfView = 60.0f;
    bool m_Navigating = false;
};
