#pragma once

#include <filesystem>
#include <string>

class GltfImportWindow
{
public:
    static constexpr const char* kName = "Import glTF";

    struct Options
    {
        bool skeleton = false;
        bool skeletonModel = false;
        bool staticModel = false;
        bool animationClip = false;
    };

    void Open(const std::filesystem::path& targetDirectory, const std::filesystem::path& sourceFile = {});
    void Draw();
    bool IsOpen() const { return m_Open; }

    // 창과 명령줄 임포트(--import-gltf)가 같이 쓰는 실제 임포트. 결과 메시지를 돌려준다.
    static std::string Import(const std::string& sourcePath, const std::filesystem::path& targetDirectory, const Options& options);

private:
    bool m_Open = false;
    std::filesystem::path m_TargetDirectory;
    std::string m_SourcePath;
    bool m_ImportSkeleton = true;
    bool m_ImportSkeletonModel = false;
    bool m_ImportStaticModel = false;
    bool m_ImportAnimationClip = true;
    std::string m_Message;
};
