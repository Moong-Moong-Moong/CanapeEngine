#include "EditorApp.h"
#include "EditorSettings.h"
#include "EditorUtils.h"
#include "Icons/EditorIcons.h"
#include "CNDebug/DebugLog.h"
#include "Scene/Component/LightComponent.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <ImGuizmo.h>

#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <d3d11.h>

#include <filesystem>
#include <system_error>

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

    // Light/camera icons exist only in the editor and only while editing; the game never registers this.
    GetRenderer().SetEditorSubmit([this](Scene& scene, Renderer& renderer)
    {
        if (!m_Playing)
        {
            m_ProxyMeshes.Submit(scene, renderer);
            if (m_SceneView.IsEditingViewVisible())
            {
                for (const LightComponent* light : scene.GetLights())
                {
                    if (!light || !Scene::IsRunnable(light) || light->GetType() != LightType::Point
                        || !m_Context.IsSelected(light->GetOwnerObject()->GetId())) continue;
                    renderer.AddEditorWireSphere({ light->GetTransform()->GetWorldPosition(), light->GetRange(),
                        { 1.0f, 0.7f, 0.1f, 1.0f } });
                }
            }
        }
    });

    GetSceneManager().SetSceneDirectory(EditorUtils::GetSceneDirectory().string() + "/");

    m_FileBrowser.SetCallbacks({
        [this]
        {
            if (m_Playing)
            {
                m_FileBrowser.SetMessage("Stop play mode before renaming or moving assets.");
                return false;
            }
            return ConfirmDiscardChanges();
        },
        [this](const RefactorPlan& plan, bool movedOnDisk, bool sceneChecked) { OnAssetsRefactored(plan, movedOnDisk, sceneChecked); } });

    GetCommands().Register("asset.rename", "asset.rename <asset path> <new file name>",
        "Rename an asset under Content and update every reference to it", 2,
        [this](const CommandArgs& args)
        {
            m_FileBrowser.RunRefactorCommand(m_Context, false, args.GetString(0), args.GetString(1));
        });
    GetCommands().Register("asset.move", "asset.move <asset path> <target folder>",
        "Move an asset or folder under Content and update every reference to it", 2,
        [this](const CommandArgs& args)
        {
            m_FileBrowser.RunRefactorCommand(m_Context, true, args.GetString(0), args.GetString(1));
        });
    GetCommands().Register("asset.delete", "asset.delete <asset path>",
        "Move an asset or folder under Content to the Recycle Bin and clear every reference to it", 1,
        [this](const CommandArgs& args) { m_FileBrowser.RunDeleteCommand(m_Context, args.GetString(0)); });
    GetCommands().Register("editor.open", "editor.open <scene path>", "Open a scene the way the editor does", 1,
        [this](const CommandArgs& args)
        {
            const std::filesystem::path path = EditorUtils::GetGameDirectory() / EditorUtils::FromUtf8(args.GetString(0));
            OpenScene(path.lexically_normal().string());
        });
    GetCommands().Register("editor.dirty", "editor.dirty", "Report whether the open scene differs from its saved state", 0,
        [this](const CommandArgs&) { ReportUnsavedChanges(); });
    GetCommands().Register("editor.pick", "editor.pick <u 0..1> <v 0..1>", "Pick in the scene view at a normalized position", 2,
        [this](const CommandArgs& args)
        {
            m_SceneView.RequestPickAt(GetRenderer(), std::stof(args.GetString(0)), std::stof(args.GetString(1)));
        });
    GetCommands().Register("editor.selected", "editor.selected", "Log the selected object", 0,
        [this](const CommandArgs&)
        {
            const GameObject* selected = m_Context.GetSelected(GetSceneManager().GetActiveScene());
            CN_LOG_GAME("Editor", "selected: ", selected ? selected->GetName() : std::string("(none)"));
        });
    GetCommands().Register("editor.select", "editor.select <object name>", "Select the first GameObject with this name", 1,
        [this](const CommandArgs& args)
        {
            std::string name = args.GetString(0);
            for (size_t i = 1; i < args.Count(); ++i) name += " " + args.GetString(i);
            if (Scene* scene = GetSceneManager().GetActiveScene())
            {
                for (GameObject* gameObject : scene->GetAllGameObjects())
                {
                    if (!gameObject->IsPendingDestroy() && gameObject->GetName() == name)
                    {
                        m_Context.Select(gameObject);
                        return;
                    }
                }
            }
            CN_LOG_GAME("Editor", "editor.select: no object named ", name);
        });
    GetCommands().Register("editor.focus", "editor.focus <window name>", "Bring an editor window or tab to the front", 1,
        [this](const CommandArgs& args)
        {
            m_PendingFocusWindow = args.GetString(0);
            for (size_t i = 1; i < args.Count(); ++i) m_PendingFocusWindow += " " + args.GetString(i);
        });
    GetCommands().Register("editor.settings", "editor.settings", "Open Tools > Editor Settings", 0,
        [this](const CommandArgs&) { m_SettingsWindow.Open(); });
    GetCommands().Register("editor.undo", "editor.undo", "Undo the last scene edit (Ctrl+Z)", 0,
        [this](const CommandArgs&) { PerformUndo(); });
    GetCommands().Register("editor.redo", "editor.redo", "Redo the last undone scene edit (Ctrl+Y)", 0,
        [this](const CommandArgs&) { PerformRedo(); });
    GetCommands().Register("editor.history", "editor.history", "Log the undo history", 0,
        [this](const CommandArgs&)
        {
            const UndoHistory::Entry* undo = m_Undo.PeekUndo();
            const UndoHistory::Entry* redo = m_Undo.PeekRedo();
            CN_LOG_GAME("Editor", "undo history: ", m_Undo.GetUndoCount(), " undo / ", m_Undo.GetRedoCount(), " redo (max ",
                m_Undo.GetLimit(), ", ", m_Undo.GetMemoryBytes() / 1024, " KB) next undo: ", undo ? undo->label : std::string("-"),
                ", next redo: ", redo ? redo->label : std::string("-"));
        });
    GetCommands().Register("editor.rename", "editor.rename <new name>", "Rename the selected object as one undo step", 1,
        [this](const CommandArgs& args)
        {
            Scene* scene = GetSceneManager().GetActiveScene();
            GameObject* selected = scene ? m_Context.GetSelected(scene) : nullptr;
            if (!selected || m_Playing || scene != m_UndoScene)
            {
                CN_LOG_GAME("Editor", "editor.rename: nothing selected or the scene is not ready");
                return;
            }
            std::string name = args.GetString(0);
            for (size_t i = 1; i < args.Count(); ++i) name += " " + args.GetString(i);
            m_FrameStartSelection = selected->GetId();
            m_Context.SetUndoLabel("Rename " + selected->GetName());
            selected->SetName(name);
            CommitUndoIfChanged(*scene);
        });
    GetCommands().Register("editor.play", "editor.play", "Enter play mode", 0, [this](const CommandArgs&) { StartPlay(); });
    GetCommands().Register("editor.stop", "editor.stop", "Leave play mode", 0, [this](const CommandArgs&) { StopPlay(); });

    if (RunCommandLineImports())
    {
        m_QuitAfterCommandLine = true; // Run()이 시작된 뒤 첫 프레임에 종료한다
        return;
    }

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

bool EditorApp::RunCommandLineImports()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        return false;
    }

    std::string& log = m_CommandLog;
    bool any = false;
    const std::deque<Debug::LogEntry>& history = Debug::GetLogHistory();
    m_CommandLogFirstSequence = history.empty() ? 0 : history.back().sequence + 1;
    for (int i = 1; i < argc; ++i)
    {
        const std::wstring arg = argv[i];
        if (arg == L"--import-log" && i + 1 < argc)
        {
            m_CommandLogFile = argv[++i];
            continue;
        }
        if (arg == L"--exec" && i + 1 < argc)
        {
            const std::string line = EditorUtils::ToUtf8(std::filesystem::path(argv[++i]));
            any = true;
            GetCommands().Execute(line);
            log += "exec: " + line + "\n";
            continue;
        }
        if (arg == L"--exec-frame" && i + 2 < argc)
        {
            const int frame = std::max(1, _wtoi(argv[++i]));
            m_DeferredCommands.emplace_back(frame, EditorUtils::ToUtf8(std::filesystem::path(argv[++i])));
            any = true;
            continue;
        }
        if (arg != L"--import-gltf" || i + 2 >= argc)
        {
            continue;
        }

        const std::filesystem::path source = argv[++i];
        std::filesystem::path target = argv[++i];
        if (target.is_relative())
        {
            target = EditorUtils::GetGameDirectory() / target;
        }
        GltfImportWindow::Options options;
        while (i + 1 < argc && std::wstring(argv[i + 1]).rfind(L"--", 0) == 0 && std::wstring(argv[i + 1]) != L"--import-gltf"
            && std::wstring(argv[i + 1]) != L"--import-log" && std::wstring(argv[i + 1]) != L"--exec"
            && std::wstring(argv[i + 1]) != L"--exec-frame")
        {
            const std::wstring flag = argv[++i];
            if (flag == L"--static") options.staticModel = true;
            else if (flag == L"--skeleton-model") options.skeletonModel = true;
            else if (flag == L"--skeleton") options.skeleton = true;
            else if (flag == L"--anim") options.animationClip = true;
        }

        any = true;
        const std::string result = GltfImportWindow::Import(EditorUtils::ToUtf8(source), target, options);
        log += EditorUtils::ToUtf8(source) + " -> " + EditorUtils::ToUtf8(target) + "\n" + result;
        CN_LOG_RUNTIME("Import", EditorUtils::ToUtf8(source), ": ", result);
    }
    LocalFree(argv);

    if (any && m_DeferredCommands.empty())
    {
        WriteCommandLog();
    }
    return any;
}

void EditorApp::WriteCommandLog()
{
    if (m_CommandLogFile.empty())
    {
        return;
    }
    for (const Debug::LogEntry& entry : Debug::GetLogHistory())
    {
        if (entry.sequence >= m_CommandLogFirstSequence)
        {
            m_CommandLog += std::string("log [") + Debug::GetTagName(entry.tag) + "/" + entry.subTag + "] " + entry.message + "\n";
        }
    }
    FILE* file = nullptr;
    if (_wfopen_s(&file, m_CommandLogFile.c_str(), L"wb") == 0 && file)
    {
        fwrite(m_CommandLog.data(), 1, m_CommandLog.size(), file);
        fclose(file);
    }
}

void EditorApp::ReportUnsavedChanges()
{
    Scene* scene = GetSceneManager().GetActiveScene();
    if (!scene)
    {
        CN_LOG_GAME("Editor", "dirty: no active scene");
        return;
    }
    const std::string current = SceneSerializer::SaveToString(*scene);
    if (current == m_SavedSnapshot)
    {
        CN_LOG_GAME("Editor", "dirty: no (", current.size(), " bytes)");
        return;
    }

    size_t at = 0;
    while (at < current.size() && at < m_SavedSnapshot.size() && current[at] == m_SavedSnapshot[at])
    {
        ++at;
    }
    const auto lineAround = [at](const std::string& text)
    {
        const size_t begin = text.rfind('\n', std::min(at, text.size()) > 0 ? std::min(at, text.size()) - 1 : 0);
        const size_t start = begin == std::string::npos ? 0 : begin + 1;
        const size_t end = text.find('\n', at);
        return text.substr(start, (end == std::string::npos ? text.size() : end) - start);
    };
    const size_t line = static_cast<size_t>(std::count(m_SavedSnapshot.begin(), m_SavedSnapshot.begin()
        + static_cast<std::ptrdiff_t>(std::min(at, m_SavedSnapshot.size())), '\n')) + 1;
    CN_LOG_GAME("Editor", "dirty: yes, first difference at line ", line, " | saved: ", lineAround(m_SavedSnapshot),
        " | now: ", lineAround(current));
}

void EditorApp::OnUpdate(float deltaTime)
{
    if (m_QuitAfterCommandLine)
    {
        ++m_CommandLineFrame;
        for (auto it = m_DeferredCommands.begin(); it != m_DeferredCommands.end();)
        {
            if (it->first <= m_CommandLineFrame)
            {
                m_CommandLog += "exec@" + std::to_string(it->first) + ": " + it->second + "\n";
                GetCommands().Execute(it->second);
                it = m_DeferredCommands.erase(it);
            }
            else
            {
                ++it;
            }
        }
        if (m_DeferredCommands.empty())
        {
            if (m_CommandLineFrame > 1)
            {
                WriteCommandLog();
            }
            Quit(); // 이번 프레임은 평소처럼 마치고 루프를 빠져나간다
        }
    }

    const std::string pendingScene = m_Context.TakePendingOpenScene();
    if (!pendingScene.empty() && ConfirmDiscardChanges())
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
        else if (m_RestoringFromRefactor)
        {
            if (scene)
            {
                m_Context.Select(scene->GetGameObject(scene->FindGameObjectById(m_RestoreSelectionId)));
            }
            m_RestoringFromRefactor = false;
        }
        else if (!m_Playing)
        {
            m_SceneView.FrameMainCamera(scene);
        }
    }

    m_FrameStartSelection = GetSelectedId(scene);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    HandleShortcuts();
    DrawMainMenu();
    DrawDockSpace();
    if (!m_PendingFocusWindow.empty())
    {
        ImGui::SetWindowFocus(m_PendingFocusWindow.c_str());
        m_PendingFocusWindow.clear();
    }

    m_Hierarchy.Draw(m_Context, scene);
    const SceneViewPanel::PlayRequest playRequest = m_SceneView.Draw(m_Context, scene, GetRenderer(), GetUISystem(), deltaTime, m_Playing);
    m_Inspector.Draw(m_Context, scene);
    m_FileBrowser.Draw(m_Context);
    m_DebugConsole.Draw(GetCommands());
    m_SettingsWindow.Draw();

    if (m_ShowDemoWindow)
    {
        ImGui::ShowDemoWindow(&m_ShowDemoWindow);
    }

    UpdateUndo();
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
    GetRenderer().SetEditorSubmit(nullptr);
    m_ProxyMeshes.Release();
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
    DragAcceptFiles(static_cast<HWND>(GetWindow().GetNativeHandle()), TRUE);
    ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(renderer.GetNativeDevice()),
        static_cast<ID3D11DeviceContext*>(renderer.GetNativeContext()));
    EditorSettings::Get().Load();
    EditorIcons::Get().Initialize(static_cast<ID3D11Device*>(renderer.GetNativeDevice()),
        static_cast<ID3D11DeviceContext*>(renderer.GetNativeContext()));

    GetWindow().SetNativeMessageHandler([this](void* hwnd, uint32_t message, uint64_t wParam, int64_t lParam)
    {
        if (message == WM_CLOSE && !ConfirmDiscardChanges())
        {
            return true;
        }
        if (message == WM_DROPFILES)
        {
            const HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::filesystem::path> files;
            for (UINT index = 0; index < count; ++index)
            {
                const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                std::wstring file(length + 1, L'\0');
                DragQueryFileW(drop, index, file.data(), length + 1);
                file.resize(length);
                files.emplace_back(std::move(file));
            }
            DragFinish(drop);
            m_FileBrowser.QueueExternalDrop(std::move(files));
            return true;
        }
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

    EditorIcons::Get().Shutdown();
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
        if (ImGui::MenuItem("New Scene", "Ctrl+N", false, !m_Playing) && ConfirmDiscardChanges()) NewScene();
        if (ImGui::MenuItem("Open Scene...", "Ctrl+O", false, !m_Playing)) OpenSceneDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, !m_Playing)) SaveScene();
        if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S", false, !m_Playing)) SaveSceneAs();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit") && ConfirmDiscardChanges()) Quit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit"))
    {
        const UndoHistory::Entry* undo = m_Undo.PeekUndo();
        const UndoHistory::Entry* redo = m_Undo.PeekRedo();
        const std::string undoLabel = undo ? "Undo " + undo->label : std::string("Undo");
        const std::string redoLabel = redo ? "Redo " + redo->label : std::string("Redo");
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, undo && CanUseUndo())) PerformUndo();
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, redo && CanUseUndo())) PerformRedo();
        ImGui::Separator();
        ImGui::TextDisabled("History: %zu undo / %zu redo (max %zu)", m_Undo.GetUndoCount(), m_Undo.GetRedoCount(), m_Undo.GetLimit());
        if (m_Playing)
        {
            ImGui::TextDisabled("Undo is off while playing.");
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Reset Layout")) m_ResetLayout = true;
        if (ImGui::MenuItem("Frame Main Camera")) m_SceneView.FrameMainCamera(GetSceneManager().GetActiveScene());
        ImGui::MenuItem("ImGui Demo", nullptr, &m_ShowDemoWindow);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        if (ImGui::MenuItem("Editor Settings...")) m_SettingsWindow.Open();
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
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_N, ImGuiInputFlags_RouteGlobal) && ConfirmDiscardChanges()) NewScene();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) OpenSceneDialog();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) SaveScene();
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) SaveSceneAs();
    // A focused text field keeps Ctrl+Z for its own text undo.
    if (!ImGui::GetIO().WantTextInput)
    {
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) PerformUndo();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)
            || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) PerformRedo();
    }
}

uint64_t EditorApp::GetSelectedId(Scene* scene) const
{
    const GameObject* selected = scene ? m_Context.GetSelected(scene) : nullptr;
    return selected ? selected->GetId() : 0;
}

void EditorApp::UpdateUndo()
{
    m_Undo.SetLimit(static_cast<size_t>(EditorSettings::Get().GetUndoLimit()));

    const bool itemActive = ImGui::IsAnyItemActive() || ImGuizmo::IsUsing();
    const bool deactivated = m_UndoWasItemActive && !itemActive;
    m_UndoWasItemActive = itemActive;

    if (m_Playing)
    {
        // Play-mode changes are thrown away when play stops, so they are never recorded.
        m_Context.TakeUndoLabel();
        return;
    }

    Scene* scene = GetSceneManager().GetActiveScene();
    if (scene != m_UndoScene)
    {
        // New / opened / reloaded scene: start over. Undo, redo and play restore keep the history.
        if (!m_KeepUndoHistory)
        {
            m_Undo.Clear();
        }
        m_KeepUndoHistory = false;
        m_UndoScene = scene;
        m_UndoBaseline = scene ? SceneSerializer::SaveToString(*scene) : std::string();
        m_Context.TakeUndoLabel();
        return;
    }
    if (!scene || m_KeepUndoHistory || itemActive)
    {
        return;
    }

    // An interaction ended this frame: a mouse button came up, a widget lost focus, or a key went down
    // outside a text field (Delete, Enter in a dialog, Ctrl+V, ...).
    bool boundary = deactivated || ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (!boundary && !ImGui::GetIO().WantTextInput)
    {
        for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END && !boundary; ++key)
        {
            boundary = ImGui::IsKeyPressed(static_cast<ImGuiKey>(key), false);
        }
    }
    if (boundary)
    {
        CommitUndoIfChanged(*scene);
    }
}

void EditorApp::CommitUndoIfChanged(Scene& scene)
{
    std::string current = SceneSerializer::SaveToString(scene);
    std::string label = m_Context.TakeUndoLabel();
    if (current == m_UndoBaseline)
    {
        return;
    }
    UndoHistory::Entry entry;
    entry.label = label.empty() ? std::string("Edit Scene") : std::move(label);
    entry.before = std::move(m_UndoBaseline);
    entry.after = current;
    entry.selectionBefore = m_FrameStartSelection;
    entry.selectionAfter = GetSelectedId(&scene);
    m_Undo.Push(std::move(entry));
    m_UndoBaseline = std::move(current);
}

bool EditorApp::CanUseUndo()
{
    return !m_Playing && !m_KeepUndoHistory && m_UndoScene && m_UndoScene == GetSceneManager().GetActiveScene()
        && !ImGui::IsAnyItemActive() && !ImGuizmo::IsUsing();
}

void EditorApp::PerformUndo()
{
    if (!CanUseUndo())
    {
        return;
    }
    // Anything not yet recorded becomes its own step first, so undo never skips over it.
    CommitUndoIfChanged(*m_UndoScene);
    if (const UndoHistory::Entry* entry = m_Undo.Undo())
    {
        CN_LOG_GAME("Editor", "Undo: ", entry->label);
        ApplyUndoSnapshot(entry->before, entry->selectionBefore);
    }
}

void EditorApp::PerformRedo()
{
    if (!CanUseUndo())
    {
        return;
    }
    CommitUndoIfChanged(*m_UndoScene);
    if (const UndoHistory::Entry* entry = m_Undo.Redo())
    {
        CN_LOG_GAME("Editor", "Redo: ", entry->label);
        ApplyUndoSnapshot(entry->after, entry->selectionAfter);
    }
}

void EditorApp::ApplyUndoSnapshot(const std::string& text, uint64_t selectionId)
{
    SceneManager& sceneManager = GetSceneManager();
    Scene* previous = sceneManager.GetActiveScene();
    Scene* loaded = sceneManager.LoadSceneFromString(text);
    if (!loaded)
    {
        CN_LOG_GAME("Editor", "Undo failed: could not rebuild the scene");
        return;
    }
    if (previous && previous != loaded)
    {
        sceneManager.UnloadScene(previous);
    }
    // Same scene file, same object ids: keep the path, camera and selection.
    m_KeepUndoHistory = true;
    m_RestoreSelectionId = selectionId;
    m_RestoringFromRefactor = true;
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
    cameraObject->GetTransform()->SetPosition({ 0.0f, 3.0f, -10.0f });
    cameraObject->GetTransform()->SetRotation(EditorUtils::EulerToQuaternion({ 15.0f, 0.0f, 0.0f }));
    scene->SetMainCamera(cameraObject->AddComponent<CameraComponent>());

    GameObject* lightObject = scene->GetGameObject(scene->AddGameObject("Directional Light"));
    lightObject->GetTransform()->SetRotation(EditorUtils::EulerToQuaternion({ 50.0f, -30.0f, 0.0f }));
    LightComponent* light = lightObject->AddComponent<LightComponent>();
    light->SetType(LightType::Directional);
    light->SetCastShadow(true);

    ReplaceActiveScene(scene);
    m_Context.SetScenePath({});
    MarkSceneSaved(scene);
}

void EditorApp::OpenSceneDialog()
{
    if (!ConfirmDiscardChanges())
    {
        return;
    }

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
    MarkSceneSaved(loaded);
}

bool EditorApp::SaveScene()
{
    if (m_Context.GetScenePath().empty())
    {
        return SaveSceneAs();
    }

    Scene* scene = GetSceneManager().GetActiveScene();
    if (!scene || !GetSceneManager().SaveSceneToFile(*scene, m_Context.GetScenePath()))
    {
        return false;
    }
    MarkSceneSaved();
    return true;
}

bool EditorApp::SaveSceneAs()
{
    Scene* scene = GetSceneManager().GetActiveScene();
    if (!scene)
    {
        return false;
    }

    const std::string path = ShowSceneFileDialog(GetWindow().GetNativeHandle(), true, m_Context.GetScenePath());
    if (path.empty())
    {
        return false;
    }

    const std::string previousName = scene->GetName();
    scene->SetName(std::filesystem::path(path).stem().string());
    if (!GetSceneManager().SaveSceneToFile(*scene, path))
    {
        scene->SetName(previousName);
        return false;
    }
    m_Context.SetScenePath(path);
    MarkSceneSaved();
    return true;
}

void EditorApp::OnAssetsRefactored(const RefactorPlan& plan, bool movedOnDisk, bool sceneChecked)
{
    if (movedOnDisk && plan.DeletesOnDisk())
    {
        GetAssetManager().ForgetAssetPaths(plan.move.from, plan.move.directory);
        for (const PathMove& companion : plan.companions)
        {
            GetAssetManager().ForgetAssetPaths(companion.from, false);
        }
    }
    else if (movedOnDisk)
    {
        GetAssetManager().RemapAssetPaths(plan.move.from, plan.move.to, plan.move.directory);
        for (const PathMove& companion : plan.companions)
        {
            GetAssetManager().RemapAssetPaths(companion.from, companion.to, false);
        }
    }

    const std::string scenePath = m_Context.GetScenePath();
    if (!scenePath.empty())
    {
        const std::filesystem::path& root = EditorUtils::GetGameDirectory();
        const std::string relative = AssetPaths::ToGameRelative(root, std::filesystem::path(scenePath));
        const std::string moved = movedOnDisk ? AssetRefactor::RemapGameRelative(relative, plan) : relative;
        const std::string normalized = AssetPaths::Normalize(relative);
        const bool edited = std::any_of(plan.files.begin(), plan.files.end(),
            [&](const std::string& file) { return AssetPaths::Normalize(file) == normalized; });
        if (moved != relative || edited || (sceneChecked && HasUnsavedChanges()))
        {
            ReloadSceneKeepingView(AssetPaths::ToAbsolute(root, moved).string());
        }
    }
    GetAssetManager().ReleaseUnusedAssets();
}

void EditorApp::ReloadSceneKeepingView(const std::string& path)
{
    Scene* previous = GetSceneManager().GetActiveScene();
    const GameObject* selected = m_Context.GetSelected(previous);
    const uint64_t selectedId = selected ? selected->GetId() : 0;

    OpenScene(path);
    if (GetSceneManager().GetActiveScene() != previous)
    {
        m_RestoreSelectionId = selectedId;
        m_RestoringFromRefactor = true;
    }
}

void EditorApp::MarkSceneSaved(Scene* scene)
{
    if (!scene)
    {
        scene = GetSceneManager().GetActiveScene();
    }
    m_SavedSnapshot = scene ? SceneSerializer::SaveToString(*scene) : std::string();
}

bool EditorApp::HasUnsavedChanges()
{
    if (m_Playing)
    {
        return m_PlaySnapshot != m_SavedSnapshot;
    }
    Scene* scene = GetSceneManager().GetActiveScene();
    return scene && SceneSerializer::SaveToString(*scene) != m_SavedSnapshot;
}

bool EditorApp::SavePlaySnapshot()
{
    std::string path = m_Context.GetScenePath();
    if (path.empty())
    {
        path = ShowSceneFileDialog(GetWindow().GetNativeHandle(), true, path);
        if (path.empty())
        {
            return false;
        }
    }

    std::error_code error;
    std::filesystem::copy_file(m_PlaySnapshotPath, path, std::filesystem::copy_options::overwrite_existing, error);
    if (error)
    {
        CN_LOG_GAME("Editor", "Failed to save scene: ", path);
        return false;
    }
    m_Context.SetScenePath(path);
    m_SavedSnapshot = m_PlaySnapshot;
    return true;
}

bool EditorApp::ConfirmDiscardChanges()
{
    if (!HasUnsavedChanges())
    {
        return true;
    }

    const std::string& scenePath = m_Context.GetScenePath();
    const std::wstring sceneName = scenePath.empty() ? L"Untitled" : std::filesystem::path(scenePath).stem().wstring();
    const std::wstring message = L"Do you want to save the changes to " + sceneName + L"?";
    const int answer = MessageBoxW(static_cast<HWND>(GetWindow().GetNativeHandle()), message.c_str(),
        L"Canape Editor", MB_YESNOCANCEL | MB_ICONWARNING);

    if (answer == IDYES)
    {
        return m_Playing ? SavePlaySnapshot() : SaveScene();
    }
    return answer == IDNO;
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
    m_PlaySnapshot = SceneSerializer::SaveToString(*scene);

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
    const bool wasSaved = m_PlaySnapshot == m_SavedSnapshot;

    SceneManager& sceneManager = GetSceneManager();
    for (const std::unique_ptr<Scene>& scene : sceneManager.GetScenes())
    {
        sceneManager.UnloadScene(scene.get());
    }

    if (Scene* restored = sceneManager.LoadSceneFromFile(m_PlaySnapshotPath))
    {
        m_RestoringFromPlay = true;
        m_KeepUndoHistory = true; // the restored scene is the one the edit history was recorded on
        if (wasSaved)
        {
            MarkSceneSaved(restored);
        }
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
