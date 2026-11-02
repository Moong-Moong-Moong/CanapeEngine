#pragma once

#include "EditorContext.h"
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
    void SaveScene();
    void SaveSceneAs();
    void ReplaceActiveScene(Canape::Scene* scene);

    void StartPlay();
    void StopPlay();

    EditorContext m_Context;
    HierarchyPanel m_Hierarchy;
    InspectorPanel m_Inspector;
    SceneViewPanel m_SceneView;
    FileBrowserPanel m_FileBrowser;
    DebugConsolePanel m_DebugConsole;

    Canape::Scene* m_LastScene = nullptr;
    std::string m_PlaySnapshotPath;
    uint64_t m_RestoreSelectionId = 0;
    bool m_Playing = false;
    bool m_RestoringFromPlay = false;
    bool m_ResetLayout = false;
    bool m_ShowDemoWindow = false;
};
