#pragma once

#include <filesystem>
#include <string>

class GltfImportWindow
{
public:
    static constexpr const char* kName = "Import glTF";

    void Open(const std::filesystem::path& targetDirectory);
    void Draw();

private:
    bool m_Open = false;
    std::filesystem::path m_TargetDirectory;
    std::string m_SourcePath;
    bool m_ImportSkeleton = true;
    bool m_ImportSkeletonModel = false;
    bool m_ImportAnimationClip = true;
    std::string m_Message;
};
