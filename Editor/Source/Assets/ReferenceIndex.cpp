#include "Assets/ReferenceIndex.h"

#include "Asset/Mesh/MeshSerializer.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace AssetPaths
{
    namespace
    {
        char LowerAscii(char c)
        {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
        }
    }

    std::string Normalize(std::string_view path)
    {
        std::string result(path);
        for (char& c : result)
        {
            c = c == '\\' ? '/' : LowerAscii(c);
        }
        return result;
    }

    std::string_view StripSubAsset(std::string_view path)
    {
        const size_t hash = path.find('#');
        return hash == std::string_view::npos ? path : path.substr(0, hash);
    }

    bool IsContentPath(std::string_view value)
    {
        constexpr std::string_view prefix = "content/";
        if (value.size() <= prefix.size())
        {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i)
        {
            const char c = value[i] == '\\' ? '/' : LowerAscii(value[i]);
            if (c != prefix[i])
            {
                return false;
            }
        }
        return true;
    }

    std::string ToGameRelative(const fs::path& root, const fs::path& file)
    {
        const std::u8string text = file.lexically_normal().lexically_relative(root.lexically_normal()).generic_u8string();
        return std::string(text.begin(), text.end());
    }

    fs::path ToAbsolute(const fs::path& root, std::string_view gameRelative)
    {
        return (root / fs::path(std::u8string(gameRelative.begin(), gameRelative.end()))).lexically_normal();
    }

    bool ReadText(const fs::path& file, std::string& text)
    {
        std::ifstream stream(file, std::ios::binary);
        if (!stream)
        {
            return false;
        }
        text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        return !stream.bad();
    }

    bool WriteTextAtomic(const fs::path& file, std::string_view text)
    {
        fs::path temp = file;
        temp += ".tmp";
        std::error_code error;
        {
            std::ofstream stream(temp, std::ios::binary | std::ios::trunc);
            if (!stream || !stream.write(text.data(), static_cast<std::streamsize>(text.size())))
            {
                fs::remove(temp, error);
                return false;
            }
        }
        fs::rename(temp, file, error);
        if (error)
        {
            fs::remove(temp, error);
            return false;
        }
        return true;
    }

    void ForEachJsonString(std::string_view text, const std::function<void(size_t begin, size_t end)>& visit)
    {
        size_t i = 0;
        while (i < text.size())
        {
            if (text[i] != '"')
            {
                ++i;
                continue;
            }
            const size_t begin = ++i;
            while (i < text.size() && text[i] != '"')
            {
                i += text[i] == '\\' ? 2 : 1;
            }
            if (i >= text.size())
            {
                return;
            }
            visit(begin, i);
            ++i;
        }
    }
}

void ReferenceIndex::SetRoot(fs::path gameDirectory)
{
    m_Root = gameDirectory.lexically_normal();
    m_Files.clear();
    m_Reverse.clear();
    m_DisplayPaths.clear();
    m_Built = false;
}

bool ReferenceIndex::IsIndexedFile(const fs::path& file)
{
    const std::u8string text = file.extension().u8string();
    const std::string extension = AssetPaths::Normalize(std::string(text.begin(), text.end()));
    return extension == ".json" || extension == ".prefab" || extension == ".mat" || IsMeshFile(file);
}

bool ReferenceIndex::IsMeshFile(const fs::path& file)
{
    const std::u8string text = file.u8string();
    const std::string path(text.begin(), text.end());
    return Canape::MeshSerializer::IsStaticMeshPath(path) || Canape::MeshSerializer::IsSkinnedMeshPath(path);
}

void ReferenceIndex::Rebuild()
{
    m_Files.clear();
    m_Reverse.clear();
    m_DisplayPaths.clear();
    m_Built = true;
    Refresh();
}

void ReferenceIndex::Refresh()
{
    m_Built = true;
    std::set<std::string> seen;
    std::error_code error;
    const fs::path content = m_Root / "Content";
    for (fs::recursive_directory_iterator it(content, fs::directory_options::skip_permission_denied, error), end;
        !error && it != end; it.increment(error))
    {
        std::error_code entryError;
        if (!it->is_regular_file(entryError) || !IsIndexedFile(it->path()))
        {
            continue;
        }
        const fs::file_time_type time = it->last_write_time(entryError);
        const uintmax_t size = it->file_size(entryError);
        if (entryError)
        {
            continue;
        }

        const std::string relative = AssetPaths::ToGameRelative(m_Root, it->path());
        seen.insert(relative);
        const auto existing = m_Files.find(relative);
        if (existing != m_Files.end() && existing->second.time == time && existing->second.size == size)
        {
            continue;
        }
        ScanFile(relative, it->path(), time, size);
    }

    std::vector<std::string> removed;
    for (const auto& [relative, entry] : m_Files)
    {
        if (!seen.contains(relative))
        {
            removed.push_back(relative);
        }
    }
    for (const std::string& relative : removed)
    {
        RemoveFile(relative);
    }
}

void ReferenceIndex::ScanFile(const std::string& relative, const fs::path& absolute, fs::file_time_type time, uintmax_t size)
{
    RemoveFile(relative);

    std::vector<std::string> values;
    if (IsMeshFile(absolute))
    {
        const std::u8string text = absolute.u8string();
        Canape::MeshSerializer::ReadMaterialSlots(std::string(text.begin(), text.end()), values);
    }
    else
    {
        std::string text;
        if (AssetPaths::ReadText(absolute, text))
        {
            AssetPaths::ForEachJsonString(text, [&](size_t begin, size_t end)
            {
                values.emplace_back(text.substr(begin, end - begin));
            });
        }
    }

    FileEntry entry{ time, size, {} };
    for (const std::string& value : values)
    {
        if (!AssetPaths::IsContentPath(value))
        {
            continue;
        }
        const std::string_view asset = AssetPaths::StripSubAsset(value);
        std::string key = AssetPaths::Normalize(asset);
        if (std::find(entry.references.begin(), entry.references.end(), key) != entry.references.end())
        {
            continue;
        }
        m_Reverse[key].insert(relative);
        m_DisplayPaths.try_emplace(key, std::string(asset));
        entry.references.push_back(std::move(key));
    }
    m_Files[relative] = std::move(entry);
}

void ReferenceIndex::RemoveFile(const std::string& relative)
{
    const auto it = m_Files.find(relative);
    if (it == m_Files.end())
    {
        return;
    }
    for (const std::string& key : it->second.references)
    {
        const auto reverse = m_Reverse.find(key);
        if (reverse == m_Reverse.end())
        {
            continue;
        }
        reverse->second.erase(relative);
        if (reverse->second.empty())
        {
            m_Reverse.erase(reverse);
            m_DisplayPaths.erase(key);
        }
    }
    m_Files.erase(it);
}

std::vector<std::string> ReferenceIndex::FindReferencingFiles(const std::string& assetPath) const
{
    const auto it = m_Reverse.find(AssetPaths::Normalize(AssetPaths::StripSubAsset(assetPath)));
    return it == m_Reverse.end() ? std::vector<std::string>{} : std::vector<std::string>(it->second.begin(), it->second.end());
}

std::vector<std::string> ReferenceIndex::FindReferencingFilesUnder(const std::string& folderPath) const
{
    std::string prefix = AssetPaths::Normalize(folderPath);
    if (prefix.empty() || prefix.back() != '/')
    {
        prefix += '/';
    }

    std::set<std::string> files;
    for (auto it = m_Reverse.lower_bound(prefix); it != m_Reverse.end() && it->first.starts_with(prefix); ++it)
    {
        files.insert(it->second.begin(), it->second.end());
    }
    return std::vector<std::string>(files.begin(), files.end());
}

std::vector<ReferenceIndex::MissingReference> ReferenceIndex::FindMissing() const
{
    std::vector<MissingReference> missing;
    for (const auto& [key, files] : m_Reverse)
    {
        const auto display = m_DisplayPaths.find(key);
        const std::string& path = display != m_DisplayPaths.end() ? display->second : key;
        std::error_code error;
        if (!fs::exists(AssetPaths::ToAbsolute(m_Root, path), error))
        {
            missing.push_back({ path, std::vector<std::string>(files.begin(), files.end()) });
        }
    }
    return missing;
}
