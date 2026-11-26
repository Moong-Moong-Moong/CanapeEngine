#pragma once

#include "Assets/ReferenceIndex.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct PathMove
{
    std::string from;
    std::string to;
    bool directory = false;
};

struct ReferenceEdit
{
    std::string file;
    std::string before;
    std::string after;
};

struct RefactorPlan
{
    enum class Kind { Rename, Move, Redirect, Delete };

    Kind kind = Kind::Rename;
    PathMove move;
    std::vector<PathMove> companions;
    std::vector<std::string> files;
    std::vector<ReferenceEdit> edits;
    std::string startupSceneBefore;
    std::string startupSceneAfter;
    std::vector<std::string> warnings;
    std::string error;

    bool IsValid() const { return error.empty(); }
    bool MovesOnDisk() const { return kind == Kind::Rename || kind == Kind::Move; }
    bool DeletesOnDisk() const { return kind == Kind::Delete; }
};

using AssetDeleter = std::function<bool(const std::filesystem::path& path, std::string& error)>;

namespace AssetRefactor
{
    inline constexpr std::string_view kGameConfig = "Content/Game.json";
    inline constexpr std::string_view kSceneFolder = "Content/Scenes/";

    bool IsUnderContent(std::string_view gameRelative);
    std::string ValidateName(const std::string& name);
    std::string ValidateRename(const std::filesystem::path& root, const std::string& assetPath, const std::string& newName);
    std::string ValidateMove(const std::filesystem::path& root, const std::string& assetPath, const std::string& targetFolder);

    std::optional<std::string> Remap(std::string_view value, const PathMove& move);
    std::string RewriteJsonText(std::string_view text, const PathMove& move, std::vector<std::pair<std::string, std::string>>* changes);

    RefactorPlan PlanRename(ReferenceIndex& index, const std::string& assetPath, const std::string& newName);
    RefactorPlan PlanMove(ReferenceIndex& index, const std::string& assetPath, const std::string& targetFolder);
    RefactorPlan PlanRedirect(ReferenceIndex& index, const std::string& missingPath, const std::string& newPath);
    RefactorPlan PlanDelete(ReferenceIndex& index, const std::string& assetPath);
    bool Apply(const RefactorPlan& plan, ReferenceIndex& index, std::string& error, const AssetDeleter& deleter = {});
    bool MoveToRecycleBin(const std::filesystem::path& path, std::string& error);

    std::string RemapGameRelative(const std::string& path, const RefactorPlan& plan);
}
