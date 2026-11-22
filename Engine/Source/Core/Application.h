#pragma once

#include "Asset/AssetManager.h"
#include "Core/Console/CommandRegistry.h"
#include "Core/Window.h"
#include "Input/InputManager.h"
#include "JoltPhysics/JoltRuntime.h"
#include "Renderer/Renderer.h"
#include "Scene/SceneManager.h"
#include "UI/UISystem.h"

#include <memory>

namespace Canape
{
    class Session;
    class Simulator;

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
        JoltRuntime& GetPhysicsRuntime() { return *m_PhysicsRuntime; }
        CommandRegistry& GetCommands() { return *m_Commands; }
        UISystem& GetUISystem() { return *m_UISystem; }

    protected:
        virtual void OnInit() {}
        virtual void OnUpdate(float deltaTime) { (void)deltaTime; }
        virtual void OnShutdown() {}

        void SetRunSceneScripts(bool run) { m_RunSceneScripts = run; }

    private:
        static Application* s_Instance;

        std::unique_ptr<Window> m_Window;
        std::unique_ptr<InputManager> m_InputManager;
        std::unique_ptr<Renderer> m_Renderer;
        std::unique_ptr<AssetManager> m_AssetManager;
        std::unique_ptr<JoltRuntime> m_PhysicsRuntime;
        std::unique_ptr<Session> m_SimulationSession;
        std::unique_ptr<Simulator> m_Simulator;
        std::unique_ptr<SceneManager> m_SceneManager;
        std::unique_ptr<CommandRegistry> m_Commands;
        std::unique_ptr<UISystem> m_UISystem;

        bool m_Running = false;
        bool m_RunSceneScripts = true;

        bool m_Headless = false;
    };
}
