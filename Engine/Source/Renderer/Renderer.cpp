#include "Renderer/Renderer.h"
#include "Renderer/GpuResourceRegistry.h"
#include "DX11Renderer/DX11Renderer.h"
#include "Asset/AssetManager.h"
#include "CNDebug/DebugLog.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Scene.h"

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
        return m_DX11->Initialize(nativeWindow, width, height);
    }

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
                CN_LOG_RUNTIME("Renderer", "메인 카메라 없음");
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
