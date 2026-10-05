#pragma once

#include "Asset/ResourceManager.h"
#include "Core/Window.h"
#include "Input/InputManager.h"
#include "Scene/SceneManager.h"

#include "DX11Renderer.h"

#include <memory>

namespace Canape
{
    class Application
    {
    public:
        explicit Application(const WindowDesc& windowDesc = {});
        virtual ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;

        static Application& Get() { return *s_Instance; }

        void Run();
        void Quit();

        Window& GetWindow() { return *m_Window; }
        SceneManager& GetSceneManager() { return *m_SceneManager; }
        InputManager& GetInputManager() { return *m_InputManager; }
        DX11Renderer& GetRenderer() { return *m_Renderer; }
        ResourceManager& GetResourceManager() { return *m_ResourceManager; }

    protected:
        virtual void OnInit() {}
        virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
        virtual void OnShutdown() {}

    private:
        void RenderScene();

        static Application* s_Instance;

        std::unique_ptr<Window> m_Window;
        std::unique_ptr<InputManager> m_InputManager;
        std::unique_ptr<DX11Renderer> m_Renderer;
        std::unique_ptr<ResourceManager> m_ResourceManager;
        std::unique_ptr<SceneManager> m_SceneManager;

        bool m_Running = false;
        bool m_WarnedNoCamera = false;
    };
}
