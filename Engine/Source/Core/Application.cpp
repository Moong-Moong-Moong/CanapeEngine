#include "Core/Application.h"
#include "Core/Console/EngineCommands.h"
#include "Core/Reflection/Reflection.h"
#include "Core/Time.h"
#include "CNDebug/DebugLog.h"
#include "Renderer/GpuResourceRegistry.h"

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
            if (input.IsKeyPressed(Key::Backquote)) Debug::BeginCommandInput();

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
        m_PhysicsRuntime(std::make_unique<JoltRuntime>()),
        m_SceneManager(std::make_unique<SceneManager>(m_PhysicsRuntime.get())),
        m_Commands(std::make_unique<CommandRegistry>()),
        m_UISystem(std::make_unique<UISystem>()),
        m_Headless(windowDesc.Headless)
    {
        if (!m_PhysicsRuntime->IsValid())
        {
            throw std::runtime_error(m_PhysicsRuntime->GetError());
        }

        RegisterEngineTypes();

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

        RegisterEngineCommands(*this);

#if defined(_DEBUG)
        Debug::SetCommandHandler([this](std::string_view line) { m_Commands->Submit(line); });
#endif
    }

    Application::~Application()
    {
#if defined(_DEBUG)
        Debug::SetCommandHandler(nullptr);
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
            // 스크립트보다 먼저: UI가 쓴 마우스 입력은 게임 쪽 조회에서 빠진다.
            m_UISystem->ProcessInput(m_SceneManager->GetActiveScene(), *m_InputManager);

#if defined(_DEBUG)
            UpdateDebugConsole(*m_InputManager);
#endif

            m_Commands->ExecutePending();

            m_SceneManager->Update(m_RunSceneScripts);

            if (!m_Headless)
            {
                Scene* activeScene = m_SceneManager->GetActiveScene();
                const GpuResourceRegistry& registry = m_Renderer->GetRegistry();
                const UITextureResolver resolver = [&registry](TextureAssetId texture) { return registry.GetResourceId(texture); };
                Renderer& renderer = *m_Renderer;
                UITextureDevice device;
                device.Create = [&renderer](uint32_t width, uint32_t height, std::span<const uint8_t> rgba)
                {
                    return renderer.CreateTexture(width, height, TextureFormat::RGBA8_UNORM, rgba);
                };
                device.Update = [&renderer](TextureResourceId texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                    std::span<const uint8_t> rgba)
                {
                    return renderer.UpdateTexture(texture, x, y, width, height, rgba);
                };
                m_UISystem->BuildDrawList(activeScene, m_Renderer->GetWidth(), m_Renderer->GetHeight(), resolver, device);
                m_Renderer->SetUIDrawList(&m_UISystem->GetDrawList());
                m_Renderer->Render(activeScene, *m_AssetManager);
            }

            m_InputManager->EndFrame();
        }

        OnShutdown();
    }

    void Application::Quit()
    {
        m_Running = false;
    }
}
