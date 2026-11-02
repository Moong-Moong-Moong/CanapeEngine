#include "EditorApp.h"
#include "EditorUtils.h"
#include "CNDebug/DebugLog.h"
#include "Scene/Component/LightComponent.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <commdlg.h>
#include <d3d11.h>

#include <filesystem>
#include <system_error>

#pragma comment(lib, "comdlg32.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace Canape;

namespace
{
    constexpr char kKoreanFont[] = "C:\\Windows\\Fonts\\malgun.ttf";
    constexpr char kMonospaceFont[] = "C:\\Windows\\Fonts\\consola.ttf";
    constexpr float kFontSize = 17.0f;
    constexpr float kMonospaceFontSize = 15.0f;

    constexpr ImWchar kSymbolRanges[] = {
        0x2010, 0x205E,
        0x2190, 0x21FF,
        0x2500, 0x257F,
        0x25A0, 0x25FF,
        0 };

    constexpr ImWchar kMonospaceRanges[] = {
        0x0020, 0x00FF,
        0x2010, 0x205E,
        0x2190, 0x21FF,
        0x2500, 0x257F,
        0x25A0, 0x25FF,
        0 };

    std::string ShowSceneFileDialog(void* owner, bool save, const std::string& currentPath)
    {
        std::error_code error;
        const std::filesystem::path sceneDirectory = EditorUtils::GetSceneDirectory();
        std::filesystem::create_directories(sceneDirectory, error);

        std::wstring initialDirectory = sceneDirectory.wstring();
        wchar_t file[MAX_PATH] = L"";
        if (!currentPath.empty())
        {
            const std::filesystem::path current(currentPath);
            initialDirectory = current.parent_path().wstring();
            wcsncpy_s(file, current.filename().wstring().c_str(), _TRUNCATE);
        }
        for (wchar_t& c : initialDirectory)
        {
            if (c == L'/') c = L'\\';
        }

        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = static_cast<HWND>(owner);
        dialog.lpstrFilter = L"Scene (*.json)\0*.json\0All Files (*.*)\0*.*\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrInitialDir = initialDirectory.c_str();
        dialog.lpstrDefExt = L"json";
        dialog.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);

        const BOOL accepted = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
        return accepted ? std::filesystem::path(file).string() : std::string();
    }
}

EditorApp::EditorApp()
    : Canape::Application({ L"Canape Editor", 1600, 900 })
{
}

void EditorApp::OnInit()
{
    AssetManager::SetAssetRoot(EditorUtils::GetGameDirectory());
    SetRunSceneScripts(false);
    InitImGui();

    GetSceneManager().SetSceneDirectory(EditorUtils::GetSceneDirectory().string() + "/");

    const std::filesystem::path defaultScene = EditorUtils::GetSceneDirectory() / "SampleScene.json";
    std::error_code error;
    if (std::filesystem::exists(defaultScene, error))
    {
        OpenScene(defaultScene.string());
    }
    else
    {
        NewScene();
    }
}

void EditorApp::OnUpdate(float deltaTime)
{
    const std::string pendingScene = m_Context.TakePendingOpenScene();
    if (!pendingScene.empty())
    {
        OpenScene(pendingScene);
    }

    Scene* scene = GetSceneManager().GetActiveScene();
    if (scene != m_LastScene)
    {
        m_LastScene = scene;
        m_Context.ClearSelection();
        if (m_RestoringFromPlay)
        {
            if (scene)
            {
                m_Context.Select(scene->GetGameObject(scene->FindGameObjectById(m_RestoreSelectionId)));
            }
            m_RestoringFromPlay = false;
        }
        else if (!m_Playing)
        {
            m_SceneView.FrameMainCamera(scene);
        }
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    HandleShortcuts();
    DrawMainMenu();
    DrawDockSpace();

    m_Hierarchy.Draw(m_Context, scene);
    const SceneViewPanel::PlayRequest playRequest = m_SceneView.Draw(GetRenderer(), deltaTime, m_Playing);
    m_Inspector.Draw(m_Context, scene);
    m_FileBrowser.Draw(m_Context);
    m_DebugConsole.Draw(GetCommands());

    if (m_ShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    ImGui::Render();

    if (playRequest == SceneViewPanel::PlayRequest::Play)
    {
        StartPlay();
    }
    else if (playRequest == SceneViewPanel::PlayRequest::Stop)
    {
        StopPlay();
    }
}

void EditorApp::OnShutdown()
{
    if (m_Playing)
    {
        SetRunSceneScripts(false);
        m_Playing = false;
    }
    ShutdownImGui();
}

void EditorApp::InitImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;

    static ImVector<ImWchar> uiRanges;
    if (uiRanges.empty())
    {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
        builder.AddRanges(kSymbolRanges);
        builder.BuildRanges(&uiRanges);
    }

    std::error_code error;
    const bool hasKoreanFont = std::filesystem::exists(kKoreanFont, error);
    if (hasKoreanFont)
    {
        io.Fonts->AddFontFromFileTTF(kKoreanFont, kFontSize, nullptr, uiRanges.Data);
    }
    else
    {
        io.Fonts->AddFontDefault();
    }

    if (std::filesystem::exists(kMonospaceFont, error))
    {
        ImFont* monospace = io.Fonts->AddFontFromFileTTF(kMonospaceFont, kMonospaceFontSize, nullptr, kMonospaceRanges);
        if (monospace && hasKoreanFont)
        {
            ImFontConfig merge;
            merge.MergeMode = true;
            io.Fonts->AddFontFromFileTTF(kKoreanFont, kMonospaceFontSize, &merge, uiRanges.Data);
        }
        m_DebugConsole.SetLogFont(monospace);
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.TabRounding = 2.0f;

    Renderer& renderer = GetRenderer();
    ImGui_ImplWin32_Init(GetWindow().GetNativeHandle());
    ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(renderer.GetNativeDevice()),
        static_cast<ID3D11DeviceContext*>(renderer.GetNativeContext()));

    GetWindow().SetNativeMessageHandler([](void* hwnd, uint32_t message, uint64_t wParam, int64_t lParam)
    {
        return ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(hwnd), message,
            static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam)) != 0;
    });
    renderer.SetOverlay([] { ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); });
}

void EditorApp::ShutdownImGui()
{
    GetRenderer().SetOverlay(nullptr);
    GetRenderer().ClearViewOverride();
    GetRenderer().SetSceneSize(0, 0);
    GetWindow().SetNativeMessageHandler(nullptr);

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

void EditorApp::DrawMainMenu()
{
    if (!ImGui::BeginMainMenuBar())
    {
        return;
    }

    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !m_Playing)) NewScene();
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, !m_Playing)) OpenSceneDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !m_Playing)) SaveScene();
        if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, !m_Playing)) SaveSceneAs();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) Quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Reset Layout")) m_ResetLayout = true;
        if (ImGui::MenuItem("Frame Main Camera")) m_SceneView.FrameMainCamera(GetSceneManager().GetActiveScene());
        ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
        ImGui::EndMenu();
    }

    const Scene* scene = GetSceneManager().GetActiveScene();
    const std::string status = scene
        ? scene->GetName() + (m_Context.GetScenePath().empty() ? std::string("  (unsaved)") : "  " + m_Context.GetScenePath())
        : std::string("No scene");
    const float statusWidth = ImGui::CalcTextSize(status.c_str()).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - statusWidth - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", status.c_str());

    ImGui::EndMainMenuBar();
}

void EditorApp::DrawDockSpace()
{
    const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    if (m_ResetLayout || !ImGui::DockBuilderGetNode(dockspaceId))
    {
        BuildDefaultLayout(dockspaceId);
        m_ResetLayout = false;
    }
    ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport(), ImGuiDockNodeFlags_NoUndocking);
}

void EditorApp::BuildDefaultLayout(unsigned int dockspaceId)
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

    ImGuiID left = 0;
    ImGuiID inspector = 0;
    ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.24f, &inspector, &left);

    ImGuiID top = 0;
    ImGuiID fileBrowser = 0;
    ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.3f, &fileBrowser, &top);

    ImGuiID hierarchy = 0;
    ImGuiID sceneView = 0;
    ImGui::DockBuilderSplitNode(top, ImGuiDir_Left, 0.25f, &hierarchy, &sceneView);

    ImGui::DockBuilderDockWindow(HierarchyPanel::kName, hierarchy);
    ImGui::DockBuilderDockWindow(SceneViewPanel::kName, sceneView);
    ImGui::DockBuilderDockWindow(InspectorPanel::kName, inspector);
    ImGui::DockBuilderDockWindow(FileBrowserPanel::kName, fileBrowser);
    ImGui::DockBuilderDockWindow(DebugConsolePanel::kName, fileBrowser);
    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorApp::HandleShortcuts()
{
    if (m_Playing)
    {
        return;
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal)) NewScene();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) OpenSceneDialog();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) SaveScene();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) SaveSceneAs();
}

void EditorApp::NewScene()
{
    SceneManager& sceneManager = GetSceneManager();
    std::string name = "Untitled";
    for (int index = 1; sceneManager.GetScene(name); ++index)
    {
        name = "Untitled" + std::to_string(index);
    }

    Scene* scene = sceneManager.CreateScene(name);

    GameObject* cameraObject = scene->GetGameObject(scene->AddGameObject("Main Camera"));
    cameraObject->GetTransform()->SetLocalPosition({ 0.0f, 3.0f, -10.0f });
    cameraObject->GetTransform()->SetLocalRotation(EditorUtils::EulerToQuaternion({ 15.0f, 0.0f, 0.0f }));
    scene->SetMainCamera(cameraObject->AddComponent<CameraComponent>());

    GameObject* lightObject = scene->GetGameObject(scene->AddGameObject("Directional Light"));
    lightObject->GetTransform()->SetLocalRotation(EditorUtils::EulerToQuaternion({ 50.0f, -30.0f, 0.0f }));
    LightComponent* light = lightObject->AddComponent<LightComponent>();
    light->SetType(LightType::Directional);
    light->SetCastShadow(true);

    ReplaceActiveScene(scene);
    m_Context.SetScenePath({});
}

void EditorApp::OpenSceneDialog()
{
    const std::string path = ShowSceneFileDialog(GetWindow().GetNativeHandle(), false, m_Context.GetScenePath());
    if (!path.empty())
    {
        OpenScene(path);
    }
}

void EditorApp::OpenScene(const std::string& path)
{
    SceneManager& sceneManager = GetSceneManager();
    Scene* previous = sceneManager.GetActiveScene();
    Scene* loaded = sceneManager.LoadSceneFromFile(path);
    if (!loaded)
    {
        CN_LOG_GAME("Editor", "Failed to open scene: ", path);
        return;
    }

    if (previous && previous != loaded)
    {
        sceneManager.UnloadScene(previous);
    }
    m_Context.ClearSelection();
    m_Context.SetScenePath(path);
}

void EditorApp::SaveScene()
{
    if (m_Context.GetScenePath().empty())
    {
        SaveSceneAs();
        return;
    }

    if (Scene* scene = GetSceneManager().GetActiveScene())
    {
        GetSceneManager().SaveSceneToFile(*scene, m_Context.GetScenePath());
    }
}

void EditorApp::SaveSceneAs()
{
    Scene* scene = GetSceneManager().GetActiveScene();
    if (!scene)
    {
        return;
    }

    const std::string path = ShowSceneFileDialog(GetWindow().GetNativeHandle(), true, m_Context.GetScenePath());
    if (!path.empty() && GetSceneManager().SaveSceneToFile(*scene, path))
    {
        m_Context.SetScenePath(path);
    }
}

void EditorApp::StartPlay()
{
    Scene* scene = GetSceneManager().GetActiveScene();
    if (m_Playing || !scene)
    {
        return;
    }

    std::error_code error;
    m_PlaySnapshotPath = (std::filesystem::temp_directory_path(error) / "CanapeEditorPlaySnapshot.json").string();
    if (error || !SceneSerializer::Save(*scene, m_PlaySnapshotPath))
    {
        CN_LOG_GAME("Editor", "Failed to start play: could not save scene snapshot");
        return;
    }

    const GameObject* selected = m_Context.GetSelected(scene);
    m_RestoreSelectionId = selected ? selected->GetId() : 0;
    m_Playing = true;
    SetRunSceneScripts(true);
}

void EditorApp::StopPlay()
{
    if (!m_Playing)
    {
        return;
    }

    SetRunSceneScripts(false);
    m_Playing = false;

    SceneManager& sceneManager = GetSceneManager();
    for (const std::unique_ptr<Scene>& scene : sceneManager.GetScenes())
    {
        sceneManager.UnloadScene(scene.get());
    }

    if (sceneManager.LoadSceneFromFile(m_PlaySnapshotPath))
    {
        m_RestoringFromPlay = true;
    }
    else
    {
        CN_LOG_GAME("Editor", "Failed to restore scene snapshot: ", m_PlaySnapshotPath);
        NewScene();
    }

    std::error_code error;
    std::filesystem::remove(m_PlaySnapshotPath, error);
}

void EditorApp::ReplaceActiveScene(Scene* scene)
{
    SceneManager& sceneManager = GetSceneManager();
    Scene* previous = sceneManager.GetActiveScene();
    sceneManager.SetActiveScene(scene);
    if (previous && previous != scene)
    {
        sceneManager.UnloadScene(previous);
    }
    m_Context.ClearSelection();
}
