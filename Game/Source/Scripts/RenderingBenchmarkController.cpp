#include "Scripts/RenderingBenchmarkController.h"

#include "Asset/Mesh/MeshAsset.h"
#include "Core/Time.h"
#include "Scene/Component/LightComponent.h"
#include "Scene/Component/MeshRendererComponent.h"
#include "Scene/Component/SkyboxComponent.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"
#include "UI/UICanvasComponent.h"
#include "UI/UIImage.h"
#include "UI/UIText.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

using namespace Canape;

RTTR_REGISTRATION
{
    rttr::registration::enumeration<RenderingBenchmarkController::Mode>("RenderingBenchmarkMode")(
        rttr::value("PointLights", RenderingBenchmarkController::Mode::PointLights),
        rttr::value("DrawCalls", RenderingBenchmarkController::Mode::DrawCalls),
        rttr::value("Polygons", RenderingBenchmarkController::Mode::Polygons));
    rttr::registration::class_<RenderingBenchmarkController>("RenderingBenchmarkController")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr)
        .property("mode", &RenderingBenchmarkController::m_Mode)
        .property("pointLevel", &RenderingBenchmarkController::m_PointLevel)
        .property("drawLevel", &RenderingBenchmarkController::m_DrawLevel)
        .property("polygonLevel", &RenderingBenchmarkController::m_PolygonLevel)
        .property("submeshLevel", &RenderingBenchmarkController::m_SubmeshLevel)
        .property("pointRange", &RenderingBenchmarkController::m_PointRange)
        .property("pointIntensity", &RenderingBenchmarkController::m_PointIntensity)
        .property("concentrated", &RenderingBenchmarkController::m_Concentrated);
}

namespace
{
    constexpr std::array<int, 10> kPointCounts{ 0, 4, 8, 16, 24, 32, 40, 48, 56, 64 };
    constexpr std::array<int, 5> kDrawCounts{ 100, 500, 1000, 2000, 5000 };
    constexpr std::array<int, 4> kSubmeshCounts{ 1, 2, 4, 8 };
    constexpr std::array<const char*, 4> kCubeMeshes{
        "Builtin/Cube",
        "Content/Models/RenderingBenchmark/CubeSplit2.gltf",
        "Content/Models/RenderingBenchmark/CubeSplit4.gltf",
        "Content/Models/RenderingBenchmark/CubeSplit8.gltf"
    };
    constexpr std::array<const char*, 4> kSphereMeshes{
        "Content/Models/RenderingBenchmark/Sphere1024.gltf",
        "Content/Models/RenderingBenchmark/Sphere10000.gltf",
        "Content/Models/RenderingBenchmark/Sphere50000.gltf",
        "Content/Models/RenderingBenchmark/Sphere100000.gltf"
    };
    constexpr char kDarkMaterial[] = "Content/Materials/M_BenchmarkDark.mat";
    constexpr char kObjectMaterial[] = "Content/Materials/M_BenchmarkObject.mat";

    const char* ModeName(RenderingBenchmarkController::Mode mode)
    {
        switch (mode)
        {
        case RenderingBenchmarkController::Mode::PointLights: return "Point Lights";
        case RenderingBenchmarkController::Mode::DrawCalls: return "Draw Calls";
        case RenderingBenchmarkController::Mode::Polygons: return "Polygons";
        }
        return "Unknown";
    }
}

void RenderingBenchmarkController::Start()
{
    Renderer& renderer = Application::Get().GetRenderer();
    m_PreviousGpuTimingCapture = renderer.GetDebugSettings().captureGpuTimings;
    renderer.SetGpuTimingCapture(true);

    Scene* scene = GetOwnerObject()->GetScene();
    if (GameObject* sunObject = scene->FindGameObjectByName("Directional Light"))
    {
        m_Sun = sunObject->GetComponent<LightComponent>();
        if (m_Sun)
        {
            m_OriginalSunIntensity = m_Sun->GetIntensity();
            m_OriginalSunShadow = m_Sun->GetCastShadow();
            m_Sun->SetIntensity(0.08f);
            m_Sun->SetCastShadow(false);
        }
    }

    auto* sky = AddComponent<SkyboxComponent>();
    sky->SetCubemapPath("Content/HDRI/galileo_cross.dds");
    sky->SetBrightness(0.06f);
    sky->SetAmbientColor({ 1.0f, 1.0f, 1.0f });
    sky->SetAmbientIntensity(0.035f);
    sky->SetEnabled(sky->GetCubemap().IsValid());

    m_HudCanvas = AddComponent<UICanvasComponent>(Vector2{ 1280.0f, 720.0f });
    UIElement& root = m_HudCanvas->GetRoot();
    auto* background = root.AddChild<UIImage>("BenchmarkBackground");
    background->SetPosition({ 12.0f, 12.0f });
    background->SetSize({ 600.0f, 217.0f });
    background->SetColor({ 8, 12, 18, 205 });
    m_HudText = root.AddChild<UIText>("BenchmarkStats");
    m_HudText->SetPosition({ 22.0f, 16.0f });
    m_HudText->SetSize({ 580.0f, 210.0f });
    m_HudText->SetFontSize(17.0f);
    m_HudText->SetText("Building benchmark...");

    ApplySettings();
    RefreshHud(0.0f);
}

void RenderingBenchmarkController::HandleInput()
{
    const InputManager& input = Application::Get().GetInputManager();
    if (input.IsKeyPressed(Key::F1)) m_Mode = Mode::PointLights;
    if (input.IsKeyPressed(Key::F2)) m_Mode = Mode::DrawCalls;
    if (input.IsKeyPressed(Key::F3)) m_Mode = Mode::Polygons;
    int* level = m_Mode == Mode::PointLights ? &m_PointLevel
        : m_Mode == Mode::DrawCalls ? &m_DrawLevel : &m_PolygonLevel;
    if (input.IsKeyPressed(Key::Right)) ++*level;
    if (input.IsKeyPressed(Key::Left)) --*level;
    if (m_Mode == Mode::DrawCalls)
    {
        if (input.IsKeyPressed(Key::Up)) ++m_SubmeshLevel;
        if (input.IsKeyPressed(Key::Down)) --m_SubmeshLevel;
    }
    if (input.IsKeyPressed(Key::L)) m_Concentrated = !m_Concentrated;
    if (input.IsKeyPressed(Key::H))
    {
        m_HudVisible = !m_HudVisible;
        if (m_HudCanvas) m_HudCanvas->SetEnabled(m_HudVisible);
    }
}

void RenderingBenchmarkController::Update()
{
    HandleInput();
    ApplySettings();
    RefreshHud(Time::UnscaledDeltaTime());
}

void RenderingBenchmarkController::OnDestroy()
{
    ClearGenerated();
    if (m_Sun)
    {
        m_Sun->SetIntensity(m_OriginalSunIntensity);
        m_Sun->SetCastShadow(m_OriginalSunShadow);
    }
    Application::Get().GetRenderer().SetGpuTimingCapture(m_PreviousGpuTimingCapture);
}

void RenderingBenchmarkController::ClearGenerated()
{
    for (const Handle<GameObject>& handle : m_Lights)
        if (GameObject* object = GetGameObject(handle)) { object->SetActive(false); Destroy(handle); }
    for (const Handle<GameObject>& handle : m_Meshes)
        if (GameObject* object = GetGameObject(handle)) { object->SetActive(false); Destroy(handle); }
    m_Lights.clear();
    m_Meshes.clear();
    m_TrianglesPerMesh = 0;
    m_TargetMeshCount = 0;
    m_Error.clear();
}

GameObject* RenderingBenchmarkController::AddMesh(const std::string& name, const char* meshPath,
    const char* materialPath, const Vector3& position, const Vector3& scale)
{
    const Handle<GameObject> handle = CreateGameObject(name);
    GameObject* object = GetGameObject(handle);
    if (!object) return nullptr;
    object->GetTransform()->SetPosition(position);
    object->GetTransform()->SetScale(scale);
    auto* meshRenderer = object->AddComponent<MeshRendererComponent>();
    meshRenderer->SetMeshPath(meshPath);
    if (!meshRenderer->GetMesh().IsValid() || !meshRenderer->SetMaterial(0, materialPath))
    {
        m_Error = std::format("Cannot load mesh/material: {}", meshPath);
        Destroy(handle);
        return nullptr;
    }
    m_Meshes.push_back(handle);
    return object;
}

Vector3 RenderingBenchmarkController::CameraPlane(float x, float y, float distance) const
{
    Scene* scene = GetOwnerObject()->GetScene();
    const TransformComponent* camera = scene->GetMainCamera()->GetTransform();
    return camera->GetWorldPosition() + camera->GetForward() * distance
        + camera->GetRight() * x + camera->GetUp() * y;
}

void RenderingBenchmarkController::BuildPointEnvironment()
{
    AddMesh("Benchmark Floor", "Builtin/Cube", kDarkMaterial, { 0.0f, -0.15f, 7.0f }, { 19.0f, 0.3f, 16.0f });
    AddMesh("Benchmark Back Wall", "Builtin/Cube", kDarkMaterial, { 0.0f, 4.0f, 15.0f }, { 19.0f, 8.0f, 0.25f });
    AddMesh("Benchmark Left Wall", "Builtin/Cube", kDarkMaterial, { -9.5f, 4.0f, 7.0f }, { 0.25f, 8.0f, 16.0f });
    AddMesh("Benchmark Right Wall", "Builtin/Cube", kDarkMaterial, { 9.5f, 4.0f, 7.0f }, { 0.25f, 8.0f, 16.0f });
    for (int i = 0; i < 35; ++i)
    {
        const float x = -7.5f + static_cast<float>(i % 7) * 2.5f;
        const float z = 1.0f + static_cast<float>(i / 7) * 2.8f;
        AddMesh(std::format("Benchmark Sphere {}", i), "Builtin/Sphere", kObjectMaterial,
            { x, 0.65f, z }, { 1.1f, 1.1f, 1.1f });
    }
    m_TargetMeshCount = 39;
}

void RenderingBenchmarkController::SyncPointLights()
{
    const size_t target = static_cast<size_t>(kPointCounts[m_PointLevel]);
    while (m_Lights.size() > target)
    {
        const Handle<GameObject> handle = m_Lights.back();
        if (GameObject* object = GetGameObject(handle)) { object->SetActive(false); Destroy(handle); }
        m_Lights.pop_back();
    }
    while (m_Lights.size() < target)
    {
        const size_t index = m_Lights.size();
        const Handle<GameObject> handle = CreateGameObject(std::format("Benchmark Point Light {}", index));
        GameObject* object = GetGameObject(handle);
        if (!object) break;
        auto* light = object->AddComponent<LightComponent>();
        light->SetType(LightType::Point);
        light->SetColor({ 1.0f, 0.86f, 0.70f });
        light->SetCastShadow(false);
        light->SetProxyVisible(false);
        m_Lights.push_back(handle);
    }
    for (size_t index = 0; index < m_Lights.size(); ++index)
    {
        GameObject* object = GetGameObject(m_Lights[index]);
        if (!object) continue;
        const float x = m_Concentrated ? -1.4f + 0.4f * static_cast<float>(index % 8)
            : -7.0f + 2.0f * static_cast<float>(index % 8);
        const float z = m_Concentrated ? 6.0f + 0.4f * static_cast<float>(index / 8)
            : 0.5f + 1.85f * static_cast<float>(index / 8);
        object->GetTransform()->SetPosition({ x, 2.1f, z });
        auto* light = object->GetComponent<LightComponent>();
        light->SetRange(m_PointRange);
        light->SetIntensity(m_PointIntensity);
    }
}

void RenderingBenchmarkController::BuildDrawCalls()
{
    const char* path = kCubeMeshes[m_SubmeshLevel];
    m_TargetMeshCount = static_cast<uint32_t>(kDrawCounts[m_DrawLevel]);
    AssetManager& assets = Application::Get().GetAssetManager();
    const MeshAssetHandle meshHandle = assets.GetMeshHandle(path,
        { "StressTest", "Benchmark Controller", "RenderingBenchmarkController" });
    const MeshAsset* mesh = assets.GetMesh(meshHandle);
    if (!mesh || mesh->GetSubMeshes().size() != static_cast<size_t>(kSubmeshCounts[m_SubmeshLevel])
        || mesh->GetIndexCount() != 36)
    {
        m_Error = std::format("Missing or invalid cube asset: {}", path);
        return;
    }
    m_TrianglesPerMesh = mesh->GetIndexCount() / 3;
    for (uint32_t i = 0; i < m_TargetMeshCount; ++i)
    {
        // Multiplication by a value coprime to 5000 spreads each prefix over the same fixed grid.
        const uint32_t cell = (i * 1973u) % 5000u;
        const float x = (static_cast<float>(cell % 100u) - 49.5f) * 0.22f;
        const float y = (static_cast<float>(cell / 100u) - 24.5f) * 0.22f;
        GameObject* object = AddMesh(std::format("Benchmark Cube {}", i), path, kObjectMaterial,
            CameraPlane(x, y, 22.0f), { 0.14f, 0.14f, 0.14f });
        if (!object) break;
    }
}

void RenderingBenchmarkController::BuildPolygons()
{
    const char* path = kSphereMeshes[m_PolygonLevel];
    m_TargetMeshCount = 100;
    AssetManager& assets = Application::Get().GetAssetManager();
    const MeshAssetHandle meshHandle = assets.GetMeshHandle(path,
        { "StressTest", "Benchmark Controller", "RenderingBenchmarkController" });
    const MeshAsset* mesh = assets.GetMesh(meshHandle);
    if (!mesh || mesh->GetSubMeshes().size() != 1 || mesh->GetIndexCount() % 3 != 0)
    {
        m_Error = std::format("Missing or invalid sphere asset: {}", path);
        return;
    }
    m_TrianglesPerMesh = mesh->GetIndexCount() / 3;
    for (int i = 0; i < 100; ++i)
    {
        const float x = (static_cast<float>(i % 10) - 4.5f) * 1.75f;
        const float y = (static_cast<float>(i / 10) - 4.5f) * 1.75f;
        GameObject* object = AddMesh(std::format("Benchmark Polygon Sphere {}", i), path, kObjectMaterial,
            CameraPlane(x, y, 20.0f), { 1.0f, 1.0f, 1.0f });
        if (!object) break;
    }
}

void RenderingBenchmarkController::ApplySettings()
{
    m_PointLevel = std::clamp(m_PointLevel, 0, static_cast<int>(kPointCounts.size()) - 1);
    m_DrawLevel = std::clamp(m_DrawLevel, 0, static_cast<int>(kDrawCounts.size()) - 1);
    m_PolygonLevel = std::clamp(m_PolygonLevel, 0, static_cast<int>(kSphereMeshes.size()) - 1);
    m_SubmeshLevel = std::clamp(m_SubmeshLevel, 0, static_cast<int>(kSubmeshCounts.size()) - 1);
    if (!std::isfinite(m_PointRange)) m_PointRange = 2.8f;
    if (!std::isfinite(m_PointIntensity)) m_PointIntensity = 4.0f;
    m_PointRange = std::clamp(m_PointRange, 0.1f, 30.0f);
    m_PointIntensity = std::clamp(m_PointIntensity, 0.0f, 20.0f);

    const bool modeChanged = !m_ModeBuilt || m_ActiveMode != m_Mode;
    const int level = m_Mode == Mode::PointLights ? m_PointLevel
        : m_Mode == Mode::DrawCalls ? m_DrawLevel : m_PolygonLevel;
    const bool levelChanged = m_ActiveLevel != level;
    const bool splitChanged = m_Mode == Mode::DrawCalls && m_ActiveSubmeshLevel != m_SubmeshLevel;
    const bool lightChanged = m_Mode == Mode::PointLights && (m_ActiveRange != m_PointRange
        || m_ActiveIntensity != m_PointIntensity || m_ActiveConcentrated != m_Concentrated);
    if (!modeChanged && !levelChanged && !splitChanged && !lightChanged) return;

    if (modeChanged || (m_Mode != Mode::PointLights && (levelChanged || splitChanged)))
    {
        // Pending-destroy objects are inactive immediately; newly added components enter the registry next frame.
        ClearGenerated();
        if (m_Mode == Mode::PointLights) BuildPointEnvironment();
        else if (m_Mode == Mode::DrawCalls) BuildDrawCalls();
        else BuildPolygons();
        m_ModeBuilt = true;
    }
    if (m_Mode == Mode::PointLights) SyncPointLights();

    m_ActiveMode = m_Mode;
    m_ActiveLevel = level;
    m_ActiveSubmeshLevel = m_SubmeshLevel;
    m_ActiveRange = m_PointRange;
    m_ActiveIntensity = m_PointIntensity;
    m_ActiveConcentrated = m_Concentrated;
    m_WarmupRemaining = 2.0f;
    m_SampleElapsed = 0.0f;
    m_SampleFrames = 0;
    m_HudRefreshElapsed = 1.0f;
    m_LastFps = 0.0f;
    m_LastFrameMs = 0.0f;
}

void RenderingBenchmarkController::RefreshHud(float elapsed)
{
    if (!m_HudText) return;
    m_HudRefreshElapsed += elapsed;
    if (m_WarmupRemaining > 0.0f)
    {
        m_WarmupRemaining -= elapsed;
        m_SampleElapsed = 0.0f;
        m_SampleFrames = 0;
    }
    else if (elapsed > 0.0f)
    {
        m_SampleElapsed += elapsed;
        ++m_SampleFrames;
        if (m_SampleElapsed >= 1.0f)
        {
            m_LastFps = static_cast<float>(m_SampleFrames) / m_SampleElapsed;
            m_LastFrameMs = m_SampleElapsed * 1000.0f / static_cast<float>(m_SampleFrames);
            m_SampleElapsed = 0.0f;
            m_SampleFrames = 0;
        }
    }
    if (m_HudRefreshElapsed < 1.0f) return;
    m_HudRefreshElapsed = 0.0f;

    const RendererDebugStats stats = Application::Get().GetRenderer().GetDebugStats();
    const int level = m_Mode == Mode::PointLights ? m_PointLevel
        : m_Mode == Mode::DrawCalls ? m_DrawLevel : m_PolygonLevel;
    const int requestedLights = m_Mode == Mode::PointLights ? kPointCounts[m_PointLevel] : 0;
    const std::string gpu = stats.gpuTimingValid
        ? std::format("GPU {:.3f}  Scene {:.3f}  Output {:.3f} ms",
            stats.gpuFrameMs, stats.drawSceneMs, stats.finalOutputMs)
        : "GPU timing pending";
    const std::string state = !m_Error.empty() ? m_Error
        : m_WarmupRemaining > 0.0f ? "Warming up (ignore measurements)" : "Ready";
    m_HudText->SetText(std::format(
        "{}  level {}  |  F1/F2/F3 mode, Left/Right level\n"
        "FPS {:.1f}  Frame {:.2f} ms  |  {}\n"
        "Meshes placed {}/{}  RenderItems {}  Submeshes/mesh {}\n"
        "Draw Calls {}  Triangles {}  Mesh triangles {}\n"
        "Point Lights requested {}  renderer accepted {}  range {:.1f}  intensity {:.1f}\n"
        "Layout {}  MSAA {}x  |  Up/Down split, L layout, H HUD\n{}",
        ModeName(m_Mode), level, m_LastFps, m_LastFrameMs, gpu,
        m_Meshes.size(), m_TargetMeshCount, stats.renderItems,
        m_Mode == Mode::DrawCalls ? kSubmeshCounts[m_SubmeshLevel] : 1,
        stats.drawCalls, stats.triangleCount, m_TrianglesPerMesh,
        requestedLights, stats.pointLights, m_PointRange, m_PointIntensity,
        m_Concentrated ? "concentrated" : "distributed", stats.sceneSamples, state));
}
