#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace AssetPaths
{
    std::string Normalize(std::string_view path);
    std::string_view StripSubAsset(std::string_view path);
    bool IsContentPath(std::string_view value);
    std::string ToGameRelative(const std::filesystem::path& root, const std::filesystem::path& file);
    std::filesystem::path ToAbsolute(const std::filesystem::path& root, std::string_view gameRelative);
    bool ReadText(const std::filesystem::path& file, std::string& text);
    bool WriteTextAtomic(const std::filesystem::path& file, std::string_view text);
    void ForEachJsonString(std::string_view text, const std::function<void(size_t begin, size_t end)>& visit);
}

class ReferenceIndex
{
public:
    struct MissingReference
    {
        std::string assetPath;
        std::vector<std::string> files;
    };

    void SetRoot(std::filesystem::path gameDirectory);
    const std::filesystem::path& GetRoot() const { return m_Root; }

    void Rebuild();
    void Refresh();
    bool IsBuilt() const { return m_Built; }

    std::vector<std::string> FindReferencingFiles(const std::string& assetPath) const;
    std::vector<std::string> FindReferencingFilesUnder(const std::string& folderPath) const;
    std::vector<MissingReference> FindMissing() const;
    size_t GetFileCount() const { return m_Files.size(); }

    static bool IsIndexedFile(const std::filesystem::path& file);
    static bool IsMeshFile(const std::filesystem::path& file);

private:
    struct FileEntry
    {
        std::filesystem::file_time_type time{};
        uintmax_t size = 0;
        std::vector<std::string> references;
    };

    void ScanFile(const std::string& relative, const std::filesystem::path& absolute, std::filesystem::file_time_type time, uintmax_t size);
    void RemoveFile(const std::string& relative);

    std::filesystem::path m_Root;
    std::map<std::string, FileEntry> m_Files;
    std::map<std::string, std::set<std::string>> m_Reverse;
    std::map<std::string, std::string> m_DisplayPaths;
    bool m_Built = false;
};
