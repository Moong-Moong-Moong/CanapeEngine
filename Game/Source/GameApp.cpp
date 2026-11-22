#include "GameApp.h"
#include "CNDebug/DebugLog.h"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace
{
    constexpr char kGameConfigFile[] = "Game.json";
    constexpr char kDefaultStartupScene[] = "SampleScene";

    std::filesystem::path GetContentDirectory()
    {
#if defined(CANAPE_GAME_CONTENT_DIR)
        return std::filesystem::path(CANAPE_GAME_CONTENT_DIR);
#else
        std::vector<wchar_t> buffer(32768);
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size())
        {
            return std::filesystem::path("Content");
        }
        return std::filesystem::path(buffer.data()).parent_path() / "Content";
#endif
    }

    std::string ToNarrow(const wchar_t* text)
    {
        const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (length <= 1)
        {
            return {};
        }
        std::string result(static_cast<size_t>(length - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
        return result;
    }

    std::string FindSceneArgument()
    {
        int count = 0;
        LPWSTR* arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        if (!arguments)
        {
            return {};
        }

        std::string scene;
        for (int index = 1; index + 1 < count; ++index)
        {
            if (lstrcmpiW(arguments[index], L"-scene") == 0)
            {
                scene = ToNarrow(arguments[index + 1]);
                break;
            }
        }
        LocalFree(arguments);
        return scene;
    }

    std::string ReadConfiguredScene(const std::filesystem::path& contentDirectory)
    {
        std::ifstream file(contentDirectory / kGameConfigFile);
        if (!file)
        {
            return {};
        }

        const nlohmann::json config = nlohmann::json::parse(file, nullptr, false);
        if (!config.is_object())
        {
            CN_LOG_GAME("init", "Game.json 파싱 실패");
            return {};
        }
        return config.value("startupScene", std::string());
    }

    std::string ResolveStartupScene(const std::filesystem::path& contentDirectory)
    {
        if (std::string scene = FindSceneArgument(); !scene.empty())
        {
            return scene;
        }
        if (std::string scene = ReadConfiguredScene(contentDirectory); !scene.empty())
        {
            return scene;
        }
        return kDefaultStartupScene;
    }
}

GameApp::GameApp()
    : Canape::Application({ L"Canape Game", 1280, 720 })
{
}

void GameApp::OnInit()
{
    const std::filesystem::path contentDirectory = GetContentDirectory();
    Canape::SceneManager& sceneManager = GetSceneManager();
    sceneManager.SetSceneDirectory((contentDirectory / "Scenes").string() + "/");

    const std::string startupScene = ResolveStartupScene(contentDirectory);
    if (!sceneManager.OpenScene(startupScene))
    {
        CN_LOG_GAME("init", "시작 씬을 열 수 없음 : ", startupScene);
    }

    GetCommands().Register("asset.release", "asset.release", "사용하지 않는 에셋 해제", 0,
        [this](const Canape::CommandArgs&)
        {
            const size_t released = GetAssetManager().ReleaseUnusedAssets();
            CN_LOG_GAME("Console", "미사용 에셋 해제: ", released, "개");
        });
}

void GameApp::OnUpdate(float deltaTime)
{
    (void)deltaTime;

    const Canape::InputManager& input = GetInputManager();
    if (input.IsKeyPressed(Canape::Key::Escape) || input.IsGamepadPressed(Canape::GamepadButton::Back))
    {
        // 잠겨 있으면 첫 Esc는 포인터만 풀고, 풀린 상태에서 한 번 더 누르면 종료한다.
        if (m_CursorLocked)
        {
            SetCursorLocked(false);
        }
        else
        {
            Quit();
        }
    }
    UpdateCursorLock();
}

void GameApp::UpdateCursorLock()
{
    const HWND window = static_cast<HWND>(GetWindow().GetNativeHandle());
    const bool focused = window && GetForegroundWindow() == window && !IsIconic(window);

    if (!focused)
    {
        if (m_CursorLocked)
        {
            SetCursorLocked(false);
        }
        m_WasFocused = false;
        return;
    }

    // 창이 막 활성화됐거나(시작 포함), 풀린 상태에서 창 안을 클릭하면 잠근다.
    const bool justFocused = !m_WasFocused;
    m_WasFocused = true;
    if (!m_CursorLocked && (justFocused || GetInputManager().IsMousePressed(Canape::MouseButton::Left)))
    {
        SetCursorLocked(true);
    }

    // 창이 움직이거나 크기가 바뀔 수 있으니 잠겨 있는 동안 매 프레임 가두는 영역을 갱신한다.
    if (m_CursorLocked)
    {
        RECT client{};
        GetClientRect(window, &client);
        POINT topLeft{ client.left, client.top };
        POINT bottomRight{ client.right, client.bottom };
        ClientToScreen(window, &topLeft);
        ClientToScreen(window, &bottomRight);
        const RECT clip{ topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
        ClipCursor(&clip);
    }
}

void GameApp::SetCursorLocked(bool locked)
{
    if (locked == m_CursorLocked)
    {
        return;
    }
    m_CursorLocked = locked;
    // ShowCursor는 카운터라서 원하는 쪽이 될 때까지 반복한다(보임: >= 0, 숨김: < 0).
    if (locked)
    {
        while (ShowCursor(FALSE) >= 0) {}
    }
    else
    {
        ClipCursor(nullptr);
        while (ShowCursor(TRUE) < 0) {}
    }
}

void GameApp::OnShutdown()
{
    SetCursorLocked(false);
}
