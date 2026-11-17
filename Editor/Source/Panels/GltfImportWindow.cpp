#include "Panels/GltfImportWindow.h"
#include "EditorUtils.h"

#include "Asset/Animation/AnimationClipAsset.h"
#include "Asset/Animation/AnimationClipSerializer.h"
#include "Asset/Importer/glTF/CNglTFImporter.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <Windows.h>
#include <commdlg.h>

using namespace Canape;

void GltfImportWindow::Open(const std::filesystem::path& targetDirectory)
{
    m_TargetDirectory = targetDirectory;
    m_Message.clear();
    m_Open = true;
}

void GltfImportWindow::Draw()
{
    if (!m_Open)
    {
        return;
    }

    if (!ImGui::Begin(kName, &m_Open, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::End();
        return;
    }

    const std::string target = EditorUtils::ToGameRelativePath(m_TargetDirectory);
    ImGui::Text("Target : Game/%s", target == "." ? "" : target.c_str());
    ImGui::Separator();

    ImGui::SetNextItemWidth(400.0f);
    ImGui::InputText("##Source", &m_SourcePath, ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button("..."))
    {
        wchar_t desktop[MAX_PATH] = L"";
        const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", desktop, MAX_PATH);
        std::wstring initialDirectory = length > 0 ? std::wstring(desktop) + L"\\Desktop" : std::wstring();

        wchar_t file[MAX_PATH] = L"";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.lpstrFilter = L"glTF (*.gltf)\0*.gltf\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrInitialDir = initialDirectory.c_str();
        dialog.Flags = OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&dialog))
        {
            m_SourcePath = EditorUtils::ToUtf8(std::filesystem::path(file));
        }
    }

    ImGui::Checkbox("Import Skeleton", &m_ImportSkeleton);
    ImGui::Checkbox("Import Skeleton Model", &m_ImportSkeletonModel);
    ImGui::Checkbox("Import Animation Clip", &m_ImportAnimationClip);

    ImGui::BeginDisabled(m_SourcePath.empty() || (!m_ImportSkeleton && !m_ImportSkeletonModel && !m_ImportAnimationClip));
    if (ImGui::Button("Import"))
    {
        m_Message.clear();
        const std::string targetDirectory = EditorUtils::ToUtf8(m_TargetDirectory);
        std::string clipPath;

        if (m_ImportAnimationClip)
        {
            CNglTF cNglTF;
            CNglTFImporter importer;
            size_t saved = 0;
            if (importer.ReadglTF(m_SourcePath, cNglTF))
            {
                for (const std::unique_ptr<AnimationClipAsset>& clip : importer.LoadAnimationClipAsset(cNglTF))
                {
                    std::string fileName = clip->GetName();
                    for (char& c : fileName)
                    {
                        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                        {
                            c = '_';
                        }
                    }
                    const std::filesystem::path file = m_TargetDirectory / EditorUtils::FromUtf8(fileName + ".aclip");
                    if (AnimationClipSerializer::Save(*clip, EditorUtils::ToUtf8(file)))
                    {
                        if (clipPath.empty())
                        {
                            clipPath = EditorUtils::ToGameRelativePath(file);
                        }
                        ++saved;
                    }
                }
            }
            m_Message += std::to_string(saved) + " Animation Clip(s) imported.\n";
        }

        if (m_ImportSkeleton)
        {
            CNglTFImporter importer;
            m_Message += importer.ImportSkeleton(m_SourcePath, targetDirectory, clipPath)
                ? "Skeleton imported.\n" : "Skeleton import failed.\n";
        }

        if (m_ImportSkeletonModel)
        {
            CNglTFImporter importer;
            m_Message += importer.ImportSkeletonModel(m_SourcePath, targetDirectory, clipPath)
                ? "Skeleton Model imported.\n" : "Skeleton Model import failed.\n";
        }
    }
    ImGui::EndDisabled();

    if (!m_Message.empty())
    {
        ImGui::TextWrapped("%s", m_Message.c_str());
    }

    ImGui::End();
}
