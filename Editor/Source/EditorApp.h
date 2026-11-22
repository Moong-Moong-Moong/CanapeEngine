#pragma once

#include "EditorContext.h"
#include "EditorProxyMeshes.h"
#include "Panels/DebugConsolePanel.h"
#include "Panels/FileBrowserPanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/SceneViewPanel.h"

#include <string>

class EditorApp final : public Canape::Application
{
public:
    EditorApp();

protected:
    void OnInit() override;
    void OnUpdate(float deltaTime) override;
    void OnShutdown() override;

private:
    void InitImGui();
    void ShutdownImGui();

    void DrawMainMenu();
    void DrawDockSpace();
    void BuildDefaultLayout(unsigned int dockspaceId);
    void HandleShortcuts();

    void NewScene();
    void OpenSceneDialog();
    void OpenScene(const std::string& path);
    bool SaveScene();
    bool SaveSceneAs();
    void ReplaceActiveScene(Canape::Scene* scene);

    void MarkSceneSaved();
    bool HasUnsavedChanges();
    bool SavePlaySnapshot();
    bool ConfirmDiscardChanges();

    void StartPlay();
    void StopPlay();

    // 명령줄 임포트: Editor.exe --import-gltf <원본.gltf> <대상 폴더(Game 기준)> [--static] [--skeleton-model]
    //   [--skeleton] [--anim] (여러 번 반복 가능), [--import-log <파일>]. 임포트만 하고 종료한다.
    // 임포트가 하나라도 있으면 true.
    bool RunCommandLineImports();

    EditorContext m_Context;
    HierarchyPanel m_Hierarchy;
    InspectorPanel m_Inspector;
    SceneViewPanel m_SceneView;
    FileBrowserPanel m_FileBrowser;
    DebugConsolePanel m_DebugConsole;
    EditorProxyMeshes m_ProxyMeshes;

    Canape::Scene* m_LastScene = nullptr;
    std::string m_PlaySnapshotPath;
    std::string m_PlaySnapshot;
    std::string m_SavedSnapshot;
    uint64_t m_RestoreSelectionId = 0;
    bool m_Playing = false;
    bool m_RestoringFromPlay = false;
    bool m_ResetLayout = false;
    bool m_ShowDemoWindow = false;
    bool m_QuitAfterCommandLine = false;
};
