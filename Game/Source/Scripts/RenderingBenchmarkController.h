#pragma once

#include <Engine.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Canape { class UIText; class UICanvasComponent; class LightComponent; }

class RenderingBenchmarkController final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

public:
    enum class Mode { PointLights, DrawCalls, Polygons };

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;

private:
    void BuildControls();
    void HandleInput();
    void ApplySettings();
    void ClearGenerated();
    void BuildPointEnvironment();
    void SyncPointLights();
    void BuildDrawCalls();
    void BuildPolygons();
    Canape::GameObject* AddMesh(const std::string& name, const char* meshPath, const char* materialPath,
        const Canape::Vector3& position, const Canape::Vector3& scale);
    Canape::Vector3 CameraPlane(float x, float y, float distance) const;
    void RefreshHud(float elapsed);

    // Exposed through RTTR. Levels are zero-based indices into the documented presets.
    Mode m_Mode = Mode::PointLights;
    int m_PointLevel = 0;
    int m_DrawLevel = 0;
    int m_PolygonLevel = 0;
    int m_PolygonInstanceLevel = 0;
    int m_SubmeshLevel = 0;
    float m_PointRange = 2.8f;
    float m_PointIntensity = 4.0f;
    bool m_Concentrated = false;

    Mode m_ActiveMode = Mode::PointLights;
    int m_ActiveLevel = -1;
    int m_ActiveSubmeshLevel = -1;
    int m_ActiveInstanceLevel = -1;
    float m_ActiveRange = -1.0f;
    float m_ActiveIntensity = -1.0f;
    bool m_ActiveConcentrated = false;
    bool m_ActiveSunShadow = false;
    bool m_ModeBuilt = false;
    bool m_PreviousGpuTimingCapture = false;
    bool m_PreviousCursorRequested = false;
    bool m_HudVisible = true;
    uint32_t m_TrianglesPerMesh = 0;
    uint32_t m_TargetMeshCount = 0;
    std::string m_Error;
    std::vector<Handle<Canape::GameObject>> m_Meshes;
    std::vector<Handle<Canape::GameObject>> m_Lights;
    Canape::UICanvasComponent* m_HudCanvas = nullptr;
    Canape::UIText* m_HudText = nullptr;
    std::vector<std::function<void()>> m_ControlRefreshers;
    Canape::LightComponent* m_Sun = nullptr;
    float m_OriginalSunIntensity = 1.0f;
    bool m_OriginalSunShadow = false;
    float m_WarmupRemaining = 0.0f;
    float m_SampleElapsed = 0.0f;
    uint32_t m_SampleFrames = 0;
    float m_HudRefreshElapsed = 0.0f;
    float m_LastFps = 0.0f;
    float m_LastFrameMs = 0.0f;
};
