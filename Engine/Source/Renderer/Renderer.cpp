#include "Renderer/Renderer.h"
#include "Renderer/GpuResourceRegistry.h"
#include "DX11Renderer/DX11Renderer.h"
#include "Asset/AssetManager.h"
#include "CNDebug/DebugLog.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Scene.h"
#if CANAPE_RENDER_DEBUG
#include "Input/InputManager.h"
#include <iomanip>
#include <sstream>
#include <vector>
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace Canape
{
    Renderer::Renderer()
        : m_DX11(std::make_unique<DX11Renderer>()),
        m_Registry(std::make_unique<GpuResourceRegistry>(*m_DX11))
    {
    }

    Renderer::~Renderer() = default;

    bool Renderer::Initialize(void* nativeWindow, uint32_t width, uint32_t height)
    {
        if (!m_DX11->Initialize(nativeWindow, width, height)) return false;
#if CANAPE_RENDER_DEBUG
        m_DebugWindow = nativeWindow;
        const HWND window = static_cast<HWND>(nativeWindow);
        std::vector<wchar_t> title(static_cast<size_t>(GetWindowTextLengthW(window)) + 1);
        GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
        m_DebugBaseTitle = title.data();
        m_DX11->SetDebugSettings(m_DebugSettings);
        CN_LOG_RENDERER("RenderDebug", "F1: view, F2: wireframe, F3: GPU timings, F4: depth range, F10: reset");
        UpdateDebugTitle();
#endif
        return true;
    }

#if CANAPE_RENDER_DEBUG
    void Renderer::UpdateDebugControls(const InputManager& input)
    {
        bool changed = false;
        if (input.IsKeyPressed(Key::F1))
        {
            switch (m_DebugSettings.viewMode)
            {
            case DebugViewMode::Final: m_DebugSettings.viewMode = DebugViewMode::CameraDepth; break;
            case DebugViewMode::CameraDepth: m_DebugSettings.viewMode = DebugViewMode::WorldNormal; break;
            case DebugViewMode::WorldNormal: m_DebugSettings.viewMode = DebugViewMode::UV; break;
            case DebugViewMode::UV: m_DebugSettings.viewMode = DebugViewMode::MipLOD; break;
            default: m_DebugSettings.viewMode = DebugViewMode::Final; break;
            }
            m_DebugSettings.enabled = true;
            changed = true;
        }
        if (input.IsKeyPressed(Key::F2))
        {
            m_DebugSettings.wireframe = !m_DebugSettings.wireframe;
            m_DebugSettings.enabled = true;
            changed = true;
        }
        if (input.IsKeyPressed(Key::F3))
        {
            m_DebugSettings.captureGpuTimings = !m_DebugSettings.captureGpuTimings;
            changed = true;
        }
        if (input.IsKeyPressed(Key::F4))
        {
            // Raw non-linear depth: progressively inspect geometry farther from the camera.
            const float min = m_DebugSettings.depthMin;
            m_DebugSettings.depthMin = min == 0.95f ? 0.99f : min == 0.99f ? 0.999f
                : min == 0.999f ? 0.0f : 0.95f;
            m_DebugSettings.depthMax = 1.0f;
            m_DebugSettings.viewMode = DebugViewMode::CameraDepth;
            m_DebugSettings.enabled = true;
            changed = true;
        }
        if (input.IsKeyPressed(Key::F10))
        {
            m_DebugSettings = {};
            changed = true;
        }
        if (changed)
        {
            // Apply before BeginFrame so GPU query capture starts on this frame.
            m_DX11->SetDebugSettings(m_DebugSettings);
            m_NextDebugTitleMs = 0;
        }
    }

    void Renderer::UpdateDebugTitle()
    {
        if (!m_DebugWindow) return;
        const uint64_t now = GetTickCount64();
        if (now < m_NextDebugTitleMs) return;
        m_NextDebugTitleMs = now + 250;

        const wchar_t* view = L"Final";
        if (m_DebugSettings.enabled)
        {
            switch (m_DebugSettings.viewMode)
            {
            case DebugViewMode::CameraDepth: view = L"CameraDepth"; break;
            case DebugViewMode::WorldNormal: view = L"WorldNormal"; break;
            case DebugViewMode::UV: view = L"UV"; break;
            case DebugViewMode::MipLOD: view = L"MipLOD"; break;
            default: break;
            }
        }
        const RendererDebugStats stats = m_DX11->GetDebugStats();
        std::wostringstream title;
        title << m_DebugBaseTitle << L" | " << view
            << L" | Wire " << (m_DebugSettings.enabled && m_DebugSettings.wireframe ? L"ON" : L"OFF")
            << L" | Draws " << stats.drawCalls << L" | Tris " << stats.triangleCount;
        if (m_DebugSettings.enabled && m_DebugSettings.viewMode == DebugViewMode::CameraDepth)
            title << L" | Depth [" << m_DebugSettings.depthMin << L", " << m_DebugSettings.depthMax << L"]";
        if (!m_DebugSettings.captureGpuTimings)
            title << L" | GPU OFF";
        else if (!stats.gpuTimingValid)
            title << L" | GPU pending/unavailable";
        else
            title << std::fixed << std::setprecision(3)
                << L" | GPU " << stats.gpuFrameMs << L" ms"
                << L" (Scene " << stats.drawSceneMs << L", Output " << stats.finalOutputMs << L")"
                << L" #" << stats.gpuFrameId;
        title << L" | F1 View F2 Wire F3 GPU F4 Depth F10 Reset";
        SetWindowTextW(static_cast<HWND>(m_DebugWindow), title.str().c_str());
    }
#endif

    void Renderer::Render(Scene* scene, const AssetManager& assets)
    {
        if (!m_DX11->BeginFrame())
        {
            return;
        }

        CameraComponent* camera = scene ? scene->GetMainCamera() : nullptr;

        if (!camera || !camera->IsEnabled())
        {
            if (!m_WarnedNoCamera)
            {
                CN_LOG_RENDERER("Renderer", "메인 카메라 없음");
                m_WarnedNoCamera = true;
            }
        }

        else
        {
            m_WarnedNoCamera = false;

            const float aspectRatio = static_cast<float>(m_DX11->GetWidth()) / static_cast<float>(m_DX11->GetHeight());
            RenderView view;
            view.view = camera->GetViewMatrix();
            view.projection = camera->GetProjectionMatrix(aspectRatio);
            view.cameraPosition = camera->GetTransform()->GetPosition();
            m_DX11->SetRenderView(view);

            scene->BuildRenderScene(assets, *this);
        }

        m_DX11->DrawScene();
        m_DX11->EndFrame();
#if CANAPE_RENDER_DEBUG
        UpdateDebugTitle();
#endif
    }

    void Renderer::AddRenderItem(MeshAssetId mesh, uint32_t submeshIndex, MaterialAssetId material, const Matrix4& world)
    {
        const MeshResourceId meshResource = m_Registry->GetResourceId(mesh);
        if (!meshResource.IsValid())
        {
            return;
        }

        RenderItem item;
        item.mesh = meshResource;
        item.submeshIndex = submeshIndex;
        item.material = m_Registry->GetResourceId(material);
        item.world = world;
        m_DX11->AddRenderItem(item);
    }

    uint32_t Renderer::GetWidth() const
    {
        return m_DX11->GetWidth();
    }

    uint32_t Renderer::GetHeight() const
    {
        return m_DX11->GetHeight();
    }

    const std::string& Renderer::GetLastError() const
    {
        return m_DX11->GetLastError();
    }
}
