#include "GameApp.h"
#include "CNDebug/DebugLog.h"
#include "ThirdParty/nlohmann/json.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#pragma comment(lib, "shell32.lib")

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
        Quit();
    }
}

void GameApp::OnShutdown()
{
}
