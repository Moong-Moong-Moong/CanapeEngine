#include "Core/Application.h"
#include "Core/Time.h"
#include "CNDebug/DebugLog.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <stdexcept>

namespace Canape
{
#if defined(_DEBUG)
    namespace
    {
        void UpdateDebugConsole(const InputManager& input)
        {
            if (input.IsKeyPressed(Key::F5)) Debug::ShowAll();
            if (input.IsKeyPressed(Key::F6)) Debug::ShowOnly(Debug::MainTag::Runtime);
            if (input.IsKeyPressed(Key::F7)) Debug::ShowOnly(Debug::MainTag::DX11Renderer);
            if (input.IsKeyPressed(Key::F8)) Debug::ShowOnly(Debug::MainTag::Game);
            if (input.IsKeyPressed(Key::F11)) Debug::BeginSubTagInput();

            Debug::Update();
        }
    }
#endif

    Application* Application::s_Instance = nullptr;

    Application::Application(const WindowDesc& windowDesc) : 
        m_Window(std::make_unique<Window>(windowDesc)),
        m_InputManager(std::make_unique<InputManager>()),
        m_Renderer(std::make_unique<Renderer>()),
        m_AssetManager(std::make_unique<AssetManager>(m_Renderer->GetRegistry())),
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

#if defined(_DEBUG)
        m_PrintAssetsCommand = Debug::RegisterKeyCommand(VK_F9, "AssetTracker", [this] { m_AssetManager->PrintAssetRecords(); });
#endif
    }

    Application::~Application()
    {
#if defined(_DEBUG)
        Debug::UnregisterKeyCommand(m_PrintAssetsCommand);
#endif

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

#if defined(_DEBUG)
            UpdateDebugConsole(*m_InputManager);
#endif

            m_SceneManager->Update();

            m_Renderer->Render(m_SceneManager->GetActiveScene(), *m_AssetManager);

            m_InputManager->EndFrame();
        }

        OnShutdown();
    }

    void Application::Quit()
    {
        m_Running = false;
    }
}
