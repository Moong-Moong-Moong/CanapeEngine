#pragma once

#include "Asset/AssetManager.h"
#include "Core/Window.h"
#include "Input/InputManager.h"
#include "Renderer/Renderer.h"
#include "Scene/SceneManager.h"

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
        Renderer& GetRenderer() { return *m_Renderer; }
        AssetManager& GetAssetManager() { return *m_AssetManager; }

    protected:
        virtual void OnInit() {}
        virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
        virtual void OnShutdown() {}

    private:
        static Application* s_Instance;

        std::unique_ptr<Window> m_Window;
        std::unique_ptr<InputManager> m_InputManager;
        std::unique_ptr<Renderer> m_Renderer;
        std::unique_ptr<AssetManager> m_AssetManager;
        std::unique_ptr<SceneManager> m_SceneManager;

        bool m_Running = false;
        uint32_t m_PrintAssetsCommand = 0;
    };
}
