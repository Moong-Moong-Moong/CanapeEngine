#include "Assets/AssetRefactor.h"

#include "Asset/Mesh/MeshSerializer.h"

#include <nlohmann/json.hpp>

#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>

namespace fs = std::filesystem;

namespace AssetRefactor
{
    namespace
    {
        std::string ToUtf8(const fs::path& path)
        {
            const std::u8string text = path.u8string();
            return std::string(text.begin(), text.end());
        }

        std::string FileName(std::string_view gameRelative)
        {
            const size_t slash = gameRelative.find_last_of('/');
            return std::string(slash == std::string_view::npos ? gameRelative : gameRelative.substr(slash + 1));
        }

        std::string Parent(std::string_view gameRelative)
        {
            const size_t slash = gameRelative.find_last_of('/');
            return slash == std::string_view::npos ? std::string() : std::string(gameRelative.substr(0, slash));
        }

        std::string Join(const std::string& folder, const std::string& name)
        {
            return folder.empty() ? name : folder + "/" + name;
        }

        std::string LowerExtension(std::string_view name)
        {
            const size_t dot = name.find_last_of('.');
            return dot == std::string_view::npos ? std::string() : AssetPaths::Normalize(name.substr(dot));
        }

        bool IsDirectory(const fs::path& root, const std::string& gameRelative)
        {
            std::error_code error;
            return fs::is_directory(AssetPaths::ToAbsolute(root, gameRelative), error);
        }

        std::string StartupSceneFor(const std::string& sceneValue)
        {
            return std::string(kSceneFolder) + sceneValue + ".json";
        }

        bool FindStartupScene(std::string_view text, size_t& begin, size_t& end)
        {
            bool nextIsValue = false;
            bool found = false;
            AssetPaths::ForEachJsonString(text, [&](size_t tokenBegin, size_t tokenEnd)
            {
                if (found)
                {
                    return;
                }
                if (nextIsValue)
                {
                    begin = tokenBegin;
                    end = tokenEnd;
                    found = true;
                    return;
                }
                nextIsValue = text.substr(tokenBegin, tokenEnd - tokenBegin) == "startupScene";
            });
            return found;
        }

        void CollectStartupScene(const fs::path& root, RefactorPlan& plan)
        {
            std::string text;
            size_t begin = 0;
            size_t end = 0;
            if (!AssetPaths::ReadText(AssetPaths::ToAbsolute(root, kGameConfig), text) || !FindStartupScene(text, begin, end))
            {
                return;
            }

            const std::string before = text.substr(begin, end - begin);
            const std::optional<std::string> moved = Remap(StartupSceneFor(before), plan.move);
            if (!moved)
            {
                return;
            }
            if (moved->empty())
            {
                plan.warnings.push_back(std::string(kGameConfig) + ": startupScene \"" + before
                    + "\" is the scene being deleted. Choose another startup scene by hand.");
                return;
            }

            const std::string normalized = AssetPaths::Normalize(*moved);
            if (!normalized.starts_with(AssetPaths::Normalize(kSceneFolder)) || !normalized.ends_with(".json"))
            {
                plan.warnings.push_back(std::string(kGameConfig) + ": startupScene \"" + before
                    + "\" would no longer be under " + std::string(kSceneFolder) + ". Update it by hand.");
                return;
            }
            plan.startupSceneBefore = before;
            plan.startupSceneAfter = moved->substr(kSceneFolder.size(), moved->size() - kSceneFolder.size() - 5);
        }

        bool IsSourceFile(const fs::path& file)
        {
            const std::string extension = LowerExtension(ToUtf8(file.filename()));
            return extension == ".h" || extension == ".hpp" || extension == ".cpp" || extension == ".inl";
        }

        void CollectCodeWarnings(const fs::path& root, RefactorPlan& plan)
        {
            const std::string needle = AssetPaths::Normalize(plan.move.from) + (plan.move.directory ? "/" : "");
            std::error_code error;
            for (fs::recursive_directory_iterator it(root / "Source", fs::directory_options::skip_permission_denied, error), end;
                !error && it != end; it.increment(error))
            {
                std::error_code entryError;
                if (!it->is_regular_file(entryError) || !IsSourceFile(it->path()))
                {
                    continue;
                }
                std::string text;
                if (!AssetPaths::ReadText(it->path(), text))
                {
                    continue;
                }

                const std::string lower = AssetPaths::Normalize(text);
                size_t line = 1;
                size_t lineStart = 0;
                size_t searchFrom = 0;
                for (size_t found = lower.find(needle); found != std::string::npos; found = lower.find(needle, searchFrom))
                {
                    searchFrom = found + needle.size();
                    const char next = searchFrom < lower.size() ? lower[searchFrom] : '\0';
                    const bool boundary = plan.move.directory || next == '"' || next == '#';
                    const bool quoted = found > 0 && (lower[found - 1] == '"' || plan.move.directory);
                    if (!boundary || !quoted)
                    {
                        continue;
                    }
                    for (; lineStart < found; ++lineStart)
                    {
                        if (text[lineStart] == '\n')
                        {
                            ++line;
                        }
                    }
                    plan.warnings.push_back(AssetPaths::ToGameRelative(root, it->path()) + ":" + std::to_string(line)
                        + " references this path in code. Update it by hand.");
                }
            }
        }

        void CollectCompanions(const fs::path& root, RefactorPlan& plan)
        {
            const bool movesOrDeletes = plan.kind == RefactorPlan::Kind::Move || plan.kind == RefactorPlan::Kind::Delete;
            if (!movesOrDeletes || plan.move.directory || LowerExtension(plan.move.from) != ".gltf")
            {
                return;
            }
            std::string text;
            if (!AssetPaths::ReadText(AssetPaths::ToAbsolute(root, plan.move.from), text))
            {
                return;
            }
            const nlohmann::json gltf = nlohmann::json::parse(text, nullptr, false);
            if (gltf.is_discarded() || !gltf.contains("buffers") || !gltf["buffers"].is_array())
            {
                return;
            }

            const std::string fromFolder = Parent(plan.move.from);
            const std::string toFolder = Parent(plan.move.to);
            for (const nlohmann::json& buffer : gltf["buffers"])
            {
                const std::string uri = buffer.value("uri", std::string());
                if (uri.empty() || uri.starts_with("data:") || uri.find_first_of("/\\:") != std::string::npos)
                {
                    continue;
                }
                const bool deleting = plan.DeletesOnDisk();
                PathMove companion{ Join(fromFolder, uri), deleting ? std::string() : Join(toFolder, uri), false };
                std::error_code error;
                if (!fs::exists(AssetPaths::ToAbsolute(root, companion.from), error))
                {
                    continue;
                }
                if (!deleting && fs::exists(AssetPaths::ToAbsolute(root, companion.to), error))
                {
                    plan.warnings.push_back(companion.to + " already exists, so " + uri + " is not moved with the glTF.");
                    continue;
                }
                plan.companions.push_back(std::move(companion));
            }
        }

        void CollectEdits(ReferenceIndex& index, RefactorPlan& plan)
        {
            const fs::path& root = index.GetRoot();
            index.Refresh();
            const std::vector<std::string> files = plan.move.directory
                ? index.FindReferencingFilesUnder(plan.move.from)
                : index.FindReferencingFiles(plan.move.from);

            for (const std::string& file : files)
            {
                if (plan.DeletesOnDisk() && Remap(file, plan.move))
                {
                    continue;
                }
                const fs::path absolute = AssetPaths::ToAbsolute(root, file);
                size_t editsBefore = plan.edits.size();
                if (ReferenceIndex::IsMeshFile(absolute))
                {
                    std::vector<std::string> slots;
                    if (!Canape::MeshSerializer::ReadMaterialSlots(ToUtf8(absolute), slots))
                    {
                        plan.warnings.push_back(file + " could not be read and is not updated.");
                        continue;
                    }
                    for (const std::string& slot : slots)
                    {
                        if (const std::optional<std::string> moved = Remap(slot, plan.move))
                        {
                            plan.edits.push_back({ file, slot, *moved });
                        }
                    }
                }
                else
                {
                    std::string text;
                    if (!AssetPaths::ReadText(absolute, text))
                    {
                        plan.warnings.push_back(file + " could not be read and is not updated.");
                        continue;
                    }
                    std::vector<std::pair<std::string, std::string>> changes;
                    RewriteJsonText(text, plan.move, &changes);
                    for (auto& [before, after] : changes)
                    {
                        plan.edits.push_back({ file, std::move(before), std::move(after) });
                    }
                }
                if (plan.edits.size() > editsBefore)
                {
                    plan.files.push_back(file);
                }
            }
        }

        RefactorPlan Finish(ReferenceIndex& index, RefactorPlan plan)
        {
            if (!plan.IsValid())
            {
                return plan;
            }
            CollectEdits(index, plan);
            CollectStartupScene(index.GetRoot(), plan);
            CollectCompanions(index.GetRoot(), plan);
            CollectCodeWarnings(index.GetRoot(), plan);
            return plan;
        }

        bool RewriteFile(const fs::path& root, const std::string& file, const PathMove& move, std::string& error)
        {
            const fs::path absolute = AssetPaths::ToAbsolute(root, file);
            if (ReferenceIndex::IsMeshFile(absolute))
            {
                std::vector<std::string> slots;
                if (!Canape::MeshSerializer::ReadMaterialSlots(ToUtf8(absolute), slots))
                {
                    error += "Failed to read " + file + "\n";
                    return false;
                }
                bool changed = false;
                for (std::string& slot : slots)
                {
                    if (const std::optional<std::string> moved = Remap(slot, move))
                    {
                        slot = *moved;
                        changed = true;
                    }
                }
                if (changed && !Canape::MeshSerializer::WriteMaterialSlots(ToUtf8(absolute), slots))
                {
                    error += "Failed to write " + file + "\n";
                    return false;
                }
                return true;
            }

            std::string text;
            if (!AssetPaths::ReadText(absolute, text))
            {
                error += "Failed to read " + file + "\n";
                return false;
            }
            std::vector<std::pair<std::string, std::string>> changes;
            const std::string rewritten = RewriteJsonText(text, move, &changes);
            if (!changes.empty() && !AssetPaths::WriteTextAtomic(absolute, rewritten))
            {
                error += "Failed to write " + file + "\n";
                return false;
            }
            return true;
        }

        bool RewriteStartupScene(const fs::path& root, const RefactorPlan& plan, std::string& error)
        {
            if (plan.startupSceneBefore.empty())
            {
                return true;
            }
            const fs::path file = AssetPaths::ToAbsolute(root, kGameConfig);
            std::string text;
            size_t begin = 0;
            size_t end = 0;
            if (!AssetPaths::ReadText(file, text) || !FindStartupScene(text, begin, end)
                || text.substr(begin, end - begin) != plan.startupSceneBefore)
            {
                error += "Failed to update startupScene in " + std::string(kGameConfig) + "\n";
                return false;
            }
            text.replace(begin, end - begin, plan.startupSceneAfter);
            if (!AssetPaths::WriteTextAtomic(file, text))
            {
                error += "Failed to write " + std::string(kGameConfig) + "\n";
                return false;
            }
            return true;
        }
    }

    bool IsUnderContent(std::string_view gameRelative)
    {
        return AssetPaths::IsContentPath(gameRelative);
    }

    std::string ValidateName(const std::string& name)
    {
        if (name.empty())
        {
            return "Name is empty.";
        }
        if (name == "." || name == "..")
        {
            return "Name is not allowed.";
        }
        for (const char c : name)
        {
            if (static_cast<unsigned char>(c) < 0x20 || std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos)
            {
                return "Name cannot contain < > : \" / \\ | ? *";
            }
        }
        if (name.back() == ' ' || name.back() == '.')
        {
            return "Name cannot end with a space or a dot.";
        }

        std::string stem = AssetPaths::Normalize(name.substr(0, name.find('.')));
        static constexpr std::array<std::string_view, 4> reserved = { "con", "prn", "aux", "nul" };
        const bool numbered = stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9';
        if (numbered || std::find(reserved.begin(), reserved.end(), stem) != reserved.end())
        {
            return "Name is reserved by Windows.";
        }
        return {};
    }

    std::string ValidateRename(const fs::path& root, const std::string& assetPath, const std::string& newName)
    {
        if (!IsUnderContent(assetPath))
        {
            return "Only assets under Content can be renamed.";
        }
        if (std::string error = ValidateName(newName); !error.empty())
        {
            return error;
        }

        std::error_code error;
        const fs::path source = AssetPaths::ToAbsolute(root, assetPath);
        if (!fs::exists(source, error))
        {
            return "The asset no longer exists.";
        }
        const std::string oldName = FileName(assetPath);
        if (newName == oldName)
        {
            return "Name is unchanged.";
        }
        if (!fs::is_directory(source, error) && LowerExtension(newName) != LowerExtension(oldName))
        {
            return "The extension cannot be changed.";
        }

        const fs::path target = AssetPaths::ToAbsolute(root, Join(Parent(assetPath), newName));
        const bool caseOnly = AssetPaths::Normalize(newName) == AssetPaths::Normalize(oldName);
        if (!caseOnly && fs::exists(target, error))
        {
            return newName + " already exists in this folder.";
        }
        return {};
    }

    std::string ValidateMove(const fs::path& root, const std::string& assetPath, const std::string& targetFolder)
    {
        if (!IsUnderContent(assetPath))
        {
            return "Only assets under Content can be moved.";
        }
        const std::string folder = AssetPaths::Normalize(targetFolder);
        if (folder != "content" && !IsUnderContent(targetFolder))
        {
            return "Assets can only be moved into Content.";
        }

        std::error_code error;
        if (!fs::exists(AssetPaths::ToAbsolute(root, assetPath), error))
        {
            return "The asset no longer exists.";
        }
        if (!IsDirectory(root, targetFolder))
        {
            return "The target is not a folder.";
        }
        if (AssetPaths::Normalize(Parent(assetPath)) == folder)
        {
            return "The asset is already in this folder.";
        }
        const std::string source = AssetPaths::Normalize(assetPath);
        if (IsDirectory(root, assetPath) && (folder == source || folder.starts_with(source + "/")))
        {
            return "A folder cannot be moved into itself.";
        }
        if (fs::exists(AssetPaths::ToAbsolute(root, Join(targetFolder, FileName(assetPath))), error))
        {
            return FileName(assetPath) + " already exists in the target folder.";
        }
        return {};
    }

    std::optional<std::string> Remap(std::string_view value, const PathMove& move)
    {
        const std::string_view base = AssetPaths::StripSubAsset(value);
        const std::string_view suffix = value.substr(base.size());
        const std::string normalizedBase = AssetPaths::Normalize(base);
        const std::string normalizedFrom = AssetPaths::Normalize(move.from);

        if (!move.directory)
        {
            if (normalizedBase != normalizedFrom)
            {
                return std::nullopt;
            }
            return move.to.empty() ? std::string() : move.to + std::string(suffix);
        }

        if (!normalizedBase.starts_with(normalizedFrom + "/"))
        {
            return std::nullopt;
        }
        return move.to.empty() ? std::string() : move.to + std::string(base.substr(move.from.size())) + std::string(suffix);
    }

    std::string RewriteJsonText(std::string_view text, const PathMove& move, std::vector<std::pair<std::string, std::string>>* changes)
    {
        std::string result;
        result.reserve(text.size());
        size_t copied = 0;
        AssetPaths::ForEachJsonString(text, [&](size_t begin, size_t end)
        {
            const std::string_view value = text.substr(begin, end - begin);
            if (!AssetPaths::IsContentPath(value))
            {
                return;
            }
            const std::optional<std::string> moved = Remap(value, move);
            if (!moved)
            {
                return;
            }
            result.append(text.substr(copied, begin - copied));
            result.append(*moved);
            copied = end;
            if (changes)
            {
                changes->emplace_back(std::string(value), *moved);
            }
        });
        result.append(text.substr(copied));
        return result;
    }

    RefactorPlan PlanRename(ReferenceIndex& index, const std::string& assetPath, const std::string& newName)
    {
        RefactorPlan plan;
        plan.kind = RefactorPlan::Kind::Rename;
        plan.error = ValidateRename(index.GetRoot(), assetPath, newName);
        plan.move = { assetPath, Join(Parent(assetPath), newName), IsDirectory(index.GetRoot(), assetPath) };
        return Finish(index, std::move(plan));
    }

    RefactorPlan PlanMove(ReferenceIndex& index, const std::string& assetPath, const std::string& targetFolder)
    {
        RefactorPlan plan;
        plan.kind = RefactorPlan::Kind::Move;
        plan.error = ValidateMove(index.GetRoot(), assetPath, targetFolder);
        plan.move = { assetPath, Join(targetFolder, FileName(assetPath)), IsDirectory(index.GetRoot(), assetPath) };
        return Finish(index, std::move(plan));
    }

    RefactorPlan PlanRedirect(ReferenceIndex& index, const std::string& missingPath, const std::string& newPath)
    {
        RefactorPlan plan;
        plan.kind = RefactorPlan::Kind::Redirect;
        plan.move = { missingPath, newPath, false };

        std::error_code error;
        if (!IsUnderContent(newPath))
        {
            plan.error = "The new path must be under Content.";
        }
        else if (!fs::is_regular_file(AssetPaths::ToAbsolute(index.GetRoot(), newPath), error))
        {
            plan.error = newPath + " does not exist.";
        }
        else if (LowerExtension(newPath) != LowerExtension(AssetPaths::StripSubAsset(missingPath)))
        {
            plan.error = "The new path must have the same extension.";
        }
        else if (AssetPaths::Normalize(newPath) == AssetPaths::Normalize(missingPath))
        {
            plan.error = "The path is unchanged.";
        }
        return Finish(index, std::move(plan));
    }

    RefactorPlan PlanDelete(ReferenceIndex& index, const std::string& assetPath)
    {
        RefactorPlan plan;
        plan.kind = RefactorPlan::Kind::Delete;
        plan.move = { assetPath, std::string(), IsDirectory(index.GetRoot(), assetPath) };

        std::error_code error;
        if (!IsUnderContent(assetPath))
        {
            plan.error = "Only assets under Content can be deleted.";
        }
        else if (!fs::exists(AssetPaths::ToAbsolute(index.GetRoot(), assetPath), error))
        {
            plan.error = "The asset no longer exists.";
        }
        return Finish(index, std::move(plan));
    }

    bool MoveToRecycleBin(const fs::path& path, std::string& error)
    {
        std::wstring from = fs::path(path).lexically_normal().make_preferred().wstring();
        from.push_back(L'\0');
        SHFILEOPSTRUCTW operation{};
        operation.wFunc = FO_DELETE;
        operation.pFrom = from.c_str();
        operation.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI;
        const int result = SHFileOperationW(&operation);
        if (result != 0 || operation.fAnyOperationsAborted)
        {
            error = "Failed to move to the Recycle Bin (code " + std::to_string(result) + ")";
            return false;
        }
        return true;
    }

    bool Apply(const RefactorPlan& plan, ReferenceIndex& index, std::string& error, const AssetDeleter& deleter)
    {
        error.clear();
        if (!plan.IsValid())
        {
            error = plan.error;
            return false;
        }

        const fs::path& root = index.GetRoot();
        if (plan.DeletesOnDisk())
        {
            const AssetDeleter remove = deleter ? deleter : AssetDeleter(&MoveToRecycleBin);
            std::string deleteError;
            if (!remove(AssetPaths::ToAbsolute(root, plan.move.from), deleteError))
            {
                error = "Failed to delete " + plan.move.from + ": " + deleteError;
                return false;
            }
            for (const PathMove& companion : plan.companions)
            {
                if (!remove(AssetPaths::ToAbsolute(root, companion.from), deleteError))
                {
                    error += "Failed to delete " + companion.from + ": " + deleteError + "\n";
                }
            }
        }
        if (plan.MovesOnDisk())
        {
            std::error_code moveError;
            fs::rename(AssetPaths::ToAbsolute(root, plan.move.from), AssetPaths::ToAbsolute(root, plan.move.to), moveError);
            if (moveError)
            {
                error = "Failed to move " + plan.move.from + ": " + moveError.message();
                return false;
            }
            for (const PathMove& companion : plan.companions)
            {
                fs::rename(AssetPaths::ToAbsolute(root, companion.from), AssetPaths::ToAbsolute(root, companion.to), moveError);
                if (moveError)
                {
                    error += "Failed to move " + companion.from + ": " + moveError.message() + "\n";
                }
            }
        }

        for (const std::string& file : plan.files)
        {
            RewriteFile(root, RemapGameRelative(file, plan), plan.move, error);
        }
        RewriteStartupScene(root, plan, error);

        index.Refresh();
        return error.empty();
    }

    std::string RemapGameRelative(const std::string& path, const RefactorPlan& plan)
    {
        if (!plan.MovesOnDisk())
        {
            return path;
        }
        return Remap(path, plan.move).value_or(path);
    }
}
