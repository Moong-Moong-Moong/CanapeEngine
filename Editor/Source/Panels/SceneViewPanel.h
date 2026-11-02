#pragma once

#include "EditorContext.h"
#include "Panels/RenderDebugMenu.h"

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

    PlayRequest Draw(Canape::Renderer& renderer, float deltaTime, bool playing);
    void FrameMainCamera(Canape::Scene* scene);

private:
    PlayRequest DrawToolbar(Canape::Renderer& renderer, bool playing);
    void UpdateCamera(bool hovered, float deltaTime);
    Canape::Quaternion GetRotation() const;
    Canape::RenderView BuildView(float aspectRatio) const;

    RenderDebugMenu m_RenderDebugMenu;
    Canape::Vector3 m_Position{ 0.0f, 3.0f, -10.0f };
    float m_Pitch = 15.0f;
    float m_Yaw = 0.0f;
    float m_MoveSpeed = 8.0f;
    float m_FieldOfView = 60.0f;
    bool m_Navigating = false;
};
