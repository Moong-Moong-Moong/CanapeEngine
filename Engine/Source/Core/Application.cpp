#include "Core/Application.h"
#include "Core/Time.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/MeshRendererComponent.h"
#include "Scene/GameObject/GameObject.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <stdexcept>
#include <vector>

namespace Canape
{
    Application* Application::s_Instance = nullptr;

    Application::Application(const WindowDesc& windowDesc) : 
        m_Window(std::make_unique<Window>(windowDesc)),
        m_InputManager(std::make_unique<InputManager>()),
        m_Renderer(std::make_unique<DX11Renderer>()),
        m_ResourceManager(std::make_unique<ResourceManager>()),
        m_SceneManager(std::make_unique<SceneManager>())
    {
        m_InputManager->InitInput(m_Window->GetNativeHandle());
        m_Window->SetMessageObserver([this](const MSG& msg) { m_InputManager->ObserveInput(msg); });

        if (!m_Renderer->Initialize(m_Window->GetNativeHandle(), m_Window->GetWidth(), m_Window->GetHeight()))
        {
            throw std::runtime_error(m_Renderer->GetLastError());
        }

        if (s_Instance)
        {
            throw std::runtime_error("Only one Application instance can exist");
        }
        s_Instance = this;
    }

    Application::~Application()
    {
        m_SceneManager.reset();

        if (s_Instance == this)
        {
            s_Instance = nullptr;
        }
    }

    void Application::Run()
    {
        OnInit();

        m_Running = true;
		Time::Reset();
        
        while (m_Running && m_Window->PumpMessages())
        {
            Time::Tick();
            OnUpdate(Time::DeltaTime());

            m_InputManager->Update();

            m_SceneManager->Update();

            RenderScene();

            m_InputManager->EndFrame();
        }

        OnShutdown();
    }

    void Application::RenderScene()
    {
        if (!m_Renderer->BeginFrame())
        {
            return;
        }

        Scene* scene = m_SceneManager->GetActiveScene();
        CameraComponent* camera = scene ? scene->GetMainCamera() : nullptr;

        if (!camera || !camera->IsActiveAndEnabled())
        {
            if (!m_WarnedNoCamera)
            {
                OutputDebugStringA("[Engine] No main camera is rendering\n");
                m_WarnedNoCamera = true;
            }
        }
        else
        {
            m_WarnedNoCamera = false;

            const float aspectRatio = static_cast<float>(m_Renderer->GetWidth()) / static_cast<float>(m_Renderer->GetHeight());
            const Matrix4 view = camera->GetViewMatrix();
            const Matrix4 projection = camera->GetProjectionMatrix(aspectRatio);
            m_Renderer->SetCamera(view.Data(), projection.Data());

            scene->ForEachGameObject([this](GameObject* gameObject)
            {
                if (gameObject->IsPendingDestroy() || !gameObject->IsActiveInHierarchy())
                {
                    return;
                }

                for (MeshRendererComponent* meshRenderer : gameObject->GetComponents<MeshRendererComponent>())
                {
                    Mesh* mesh = meshRenderer->GetMesh().get();
                    if (!meshRenderer->IsEnabled() || !mesh)
                    {
                        continue;
                    }

                    if (mesh->GetGpuHandle() == DX11Renderer::InvalidMesh)
                    {
                        std::vector<DX11Renderer::Vertex> vertices;
                        vertices.reserve(mesh->GetVertexCount());
                        for (const Vector3& position : mesh->GetPositions())
                        {
                            vertices.push_back({ { position.X, position.Y, position.Z } });
                        }
                        mesh->SetGpuHandle(m_Renderer->CreateMesh(vertices, mesh->GetIndices()));
                    }

                    const Matrix4 world = gameObject->GetTransform()->GetLocalToWorldMatrix();
                    m_Renderer->DrawMesh(mesh->GetGpuHandle(), world.Data());
                }
            });
        }

        m_Renderer->EndFrame();
    }

    void Application::Quit()
    {
        m_Running = false;
    }
}
