#include "Assets/AssetRefactor.h"
#include "Assets/ReferenceIndex.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Mesh/MeshSerializer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace Canape;

namespace
{
    int g_Failures = 0;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::printf("FAIL: %s\n", message);
            ++g_Failures;
        }
    }

    std::string Utf8(const fs::path& path)
    {
        const std::u8string text = path.generic_u8string();
        return std::string(text.begin(), text.end());
    }

    void Write(const fs::path& root, const std::string& relative, const std::string& text)
    {
        const fs::path file = AssetPaths::ToAbsolute(root, relative);
        fs::create_directories(file.parent_path());
        Require(AssetPaths::WriteTextAtomic(file, text), "fixture write");
    }

    std::string Read(const fs::path& root, const std::string& relative)
    {
        std::string text;
        AssetPaths::ReadText(AssetPaths::ToAbsolute(root, relative), text);
        return text;
    }

    bool Exists(const fs::path& root, const std::string& relative)
    {
        std::error_code error;
        return fs::exists(AssetPaths::ToAbsolute(root, relative), error);
    }

    bool Contains(const std::vector<std::string>& values, const std::string& value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }

    bool AnyStartsWith(const std::vector<std::string>& values, const std::string& prefix)
    {
        return std::any_of(values.begin(), values.end(), [&](const std::string& v) { return v.starts_with(prefix); });
    }

    void WriteMesh(const fs::path& root, const std::string& relative, const std::string& slot)
    {
        std::vector<StaticVertex> vertices(3);
        MaterialData material;
        material.name = slot;
        const MeshAsset mesh(std::move(vertices), std::vector<uint32_t>{ 0, 1, 2 }, std::vector<SubMesh>{},
            std::vector<MaterialData>{ material });
        Require(MeshSerializer::Save(mesh, Utf8(AssetPaths::ToAbsolute(root, relative))), "fixture mesh save");
    }

    std::vector<std::string> MeshSlots(const fs::path& root, const std::string& relative)
    {
        std::vector<std::string> slots;
        MeshSerializer::ReadMaterialSlots(Utf8(AssetPaths::ToAbsolute(root, relative)), slots);
        return slots;
    }

    const char* kScene = R"({
    "name": "Main",
    "gameObjects": [
        {
            "mesh": "Content/Models/Tree/Tree.cnmesh",
            "materials": [
                "Content/Models/Tree/Bark.mat",
                ""
            ],
            "legacy": "Content/Models/Tree/Tree.gltf#node4",
            "hitSound": "content/audio/hit.wav",
            "decoy": "Content/Models/Tree/Bark.mat.bak",
            "other": "Content/Models/TreeOld/x.mat",
            "note": "Content/Models/Tree/Bark.mat is mentioned in a sentence"
        }
    ]
}
)";

    void BuildFixture(const fs::path& root)
    {
        std::error_code error;
        fs::remove_all(root, error);
        Write(root, "Content/Game.json", "{\n    \"startupScene\": \"Main\"\n}\n");
        Write(root, "Content/Scenes/Main.json", kScene);
        Write(root, "Content/Models/Tree/Bark.mat", "{\n    \"baseColorTexture\": \"Content/Models/Tree/bark.png\"\n}\n");
        Write(root, "Content/Models/Tree/bark.png", "png");
        Write(root, "Content/Models/Tree/Tree.gltf", "{ \"buffers\": [ { \"uri\": \"Tree.bin\" } ] }");
        Write(root, "Content/Models/Tree/Tree.bin", "bin");
        Write(root, "Content/Models/TreeOld/x.txt", "keeps the folder");
        Write(root, "Content/Audio/hit.wav", "wav");
        Write(root, "Source/Foo.cpp", "#include <string>\nconst char* kHit = \"Content/Audio/hit.wav\";\nconst char* kNot = \"Content/Audio/hit.wav.old\";\n");
        WriteMesh(root, "Content/Models/Tree/Tree.cnmesh", "Content/Models/Tree/Bark.mat");
    }

    void TestPathHelpers()
    {
        Require(AssetPaths::Normalize("Content\\Models\\A.MAT") == "content/models/a.mat", "normalize");
        Require(AssetPaths::StripSubAsset("Content/A.gltf#skin0") == "Content/A.gltf", "strip sub asset");
        Require(AssetPaths::IsContentPath("content/a.mat") && !AssetPaths::IsContentPath("Content/") && !AssetPaths::IsContentPath("Builtin/Cube"), "content path");

        const PathMove file{ "Content/Models/Tree/Bark.mat", "Content/Models/Tree/Wood.mat", false };
        Require(AssetRefactor::Remap("content/models/tree/bark.mat", file).value_or("") == "Content/Models/Tree/Wood.mat", "remap file case-insensitive");
        Require(!AssetRefactor::Remap("Content/Models/Tree/Bark.mat.bak", file), "remap ignores longer name");
        const PathMove gltf{ "Content/Models/Tree/Tree.gltf", "Content/Props/Tree.gltf", false };
        Require(AssetRefactor::Remap("Content/Models/Tree/Tree.gltf#node4", gltf).value_or("") == "Content/Props/Tree.gltf#node4", "remap keeps sub asset");
        const PathMove folder{ "Content/Models/Tree", "Content/Props/Tree", true };
        Require(AssetRefactor::Remap("Content/Models/Tree/A/b.mat", folder).value_or("") == "Content/Props/Tree/A/b.mat", "remap folder");
        Require(!AssetRefactor::Remap("Content/Models/TreeOld/x.mat", folder), "remap folder needs separator");
        Require(!AssetRefactor::Remap("Content/Models/Tree", folder), "remap folder itself is not a reference");

        std::vector<std::pair<std::string, std::string>> changes;
        const std::string rewritten = AssetRefactor::RewriteJsonText(kScene, file, &changes);
        std::string expected = kScene;
        const std::string from = "\"Content/Models/Tree/Bark.mat\"";
        expected.replace(expected.find(from), from.size(), "\"Content/Models/Tree/Wood.mat\"");
        Require(rewritten == expected, "rewrite changes only exact string tokens");
        Require(changes.size() == 1, "rewrite reports one change");
    }

    void TestIndex(const fs::path& root)
    {
        ReferenceIndex index;
        index.SetRoot(root);
        index.Rebuild();

        const auto bark = index.FindReferencingFiles("content/models/tree/BARK.mat");
        Require(bark.size() == 2 && Contains(bark, "Content/Scenes/Main.json") && Contains(bark, "Content/Models/Tree/Tree.cnmesh"), "bark.mat referenced by scene and mesh");
        Require(Contains(index.FindReferencingFiles("Content/Models/Tree/Tree.gltf"), "Content/Scenes/Main.json"), "sub asset reference indexed by file");
        const auto under = index.FindReferencingFilesUnder("Content/Models/Tree");
        Require(Contains(under, "Content/Models/Tree/Bark.mat") && Contains(under, "Content/Scenes/Main.json") && Contains(under, "Content/Models/Tree/Tree.cnmesh"), "folder query");

        const auto missing = index.FindMissing();
        const auto hasMissing = [&](const std::string& path)
        {
            return std::any_of(missing.begin(), missing.end(), [&](const ReferenceIndex::MissingReference& m) { return m.assetPath == path; });
        };
        Require(hasMissing("Content/Models/Tree/Bark.mat.bak") && hasMissing("Content/Models/TreeOld/x.mat"), "missing references found");
        Require(!hasMissing("Content/Models/Tree/Bark.mat"), "existing reference not missing");

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Write(root, "Content/Prefabs/New.prefab", "{ \"mesh\": \"Content/Models/Tree/Tree.cnmesh\" }");
        index.Refresh();
        Require(Contains(index.FindReferencingFiles("Content/Models/Tree/Tree.cnmesh"), "Content/Prefabs/New.prefab"), "refresh picks up new file");

        Write(root, "Content/Prefabs/New.prefab", "{ \"mesh\": \"Content/Audio/hit.wav\" }");
        index.Refresh();
        Require(!Contains(index.FindReferencingFiles("Content/Models/Tree/Tree.cnmesh"), "Content/Prefabs/New.prefab"), "refresh picks up changed file");

        fs::remove(AssetPaths::ToAbsolute(root, "Content/Prefabs/New.prefab"));
        index.Refresh();
        Require(!Contains(index.FindReferencingFiles("Content/Audio/hit.wav"), "Content/Prefabs/New.prefab"), "refresh drops removed file");
    }

    void TestValidation(const fs::path& root)
    {
        using namespace AssetRefactor;
        Require(!ValidateName("a<b").empty() && !ValidateName("").empty() && !ValidateName("x.").empty() && !ValidateName("con.mat").empty(), "invalid names rejected");
        Require(ValidateName("Bark 2.mat").empty(), "valid name accepted");
        Require(!ValidateRename(root, "Content/Models/Tree/Bark.mat", "Bark.png").empty(), "extension change rejected");
        Require(!ValidateRename(root, "Content/Models/Tree/Bark.mat", "bark.png").empty(), "existing file rejected");
        Require(!ValidateRename(root, "Source/Foo.cpp", "Bar.cpp").empty(), "outside content rejected");
        Require(ValidateRename(root, "Content/Models/Tree/Bark.mat", "BARK.mat").empty(), "case only rename allowed");
        Require(!ValidateMove(root, "Content/Models/Tree", "Content/Models/Tree").empty(), "move into itself rejected");
        Require(!ValidateMove(root, "Content/Models", "Content/Models/Tree").empty(), "move into child rejected");
        Require(!ValidateMove(root, "Content/Models/Tree/Bark.mat", "Content/Models/Tree").empty(), "move to same folder rejected");
        Require(!ValidateMove(root, "Content/Models/Tree/Bark.mat", "Source").empty(), "move outside content rejected");
        Require(ValidateMove(root, "Content/Models/Tree/Bark.mat", "Content").empty(), "move to content root allowed");
    }

    void TestRenameFile(const fs::path& root, ReferenceIndex& index)
    {
        const RefactorPlan plan = AssetRefactor::PlanRename(index, "Content/Models/Tree/Bark.mat", "Wood.mat");
        Require(plan.IsValid(), "rename plan valid");
        Require(plan.edits.size() == 2 && plan.files.size() == 2, "rename plan has scene and mesh edits");

        std::string error;
        Require(AssetRefactor::Apply(plan, index, error), "rename applied");
        Require(!Exists(root, "Content/Models/Tree/Bark.mat") && Exists(root, "Content/Models/Tree/Wood.mat"), "file renamed");
        const std::string scene = Read(root, "Content/Scenes/Main.json");
        Require(scene.find("\"Content/Models/Tree/Wood.mat\"") != std::string::npos, "scene reference updated");
        Require(scene.find("\"Content/Models/Tree/Bark.mat.bak\"") != std::string::npos, "decoy untouched");
        Require(scene.find("\"Content/Models/Tree/Bark.mat is mentioned in a sentence\"") != std::string::npos, "sentence untouched");
        Require(MeshSlots(root, "Content/Models/Tree/Tree.cnmesh") == std::vector<std::string>{ "Content/Models/Tree/Wood.mat" }, "mesh slot updated");
        Require(MeshSerializer::Load(Utf8(AssetPaths::ToAbsolute(root, "Content/Models/Tree/Tree.cnmesh"))) != nullptr, "mesh still loads");
        Require(Contains(index.FindReferencingFiles("Content/Models/Tree/Wood.mat"), "Content/Scenes/Main.json"), "index updated after apply");
    }

    void TestCodeWarningAndCase(const fs::path& root, ReferenceIndex& index)
    {
        const RefactorPlan plan = AssetRefactor::PlanRename(index, "Content/Audio/hit.wav", "Hit2.wav");
        Require(plan.IsValid() && plan.edits.size() == 1, "lowercase reference found");
        Require(plan.warnings.size() == 1 && plan.warnings[0].starts_with("Source/Foo.cpp:2"), "code literal reported once with line");
        std::string error;
        Require(AssetRefactor::Apply(plan, index, error), "audio rename applied");
        Require(Read(root, "Content/Scenes/Main.json").find("\"Content/Audio/Hit2.wav\"") != std::string::npos, "lowercase reference rewritten");
        Require(Read(root, "Source/Foo.cpp").find("Content/Audio/hit.wav") != std::string::npos, "code not modified");
    }

    void TestMoveFolder(const fs::path& root, ReferenceIndex& index)
    {
        fs::create_directories(AssetPaths::ToAbsolute(root, "Content/Props"));
        const RefactorPlan plan = AssetRefactor::PlanMove(index, "Content/Models/Tree", "Content/Props");
        Require(plan.IsValid() && plan.move.directory, "folder move plan valid");
        std::string error;
        Require(AssetRefactor::Apply(plan, index, error), "folder move applied");
        Require(Exists(root, "Content/Props/Tree/Tree.cnmesh") && !Exists(root, "Content/Models/Tree"), "folder moved");
        const std::string scene = Read(root, "Content/Scenes/Main.json");
        Require(scene.find("\"Content/Props/Tree/Tree.cnmesh\"") != std::string::npos, "mesh reference moved");
        Require(scene.find("\"Content/Props/Tree/Tree.gltf#node4\"") != std::string::npos, "sub asset reference moved");
        Require(scene.find("\"Content/Models/TreeOld/x.mat\"") != std::string::npos, "sibling folder untouched");
        Require(Read(root, "Content/Props/Tree/Wood.mat").find("\"Content/Props/Tree/bark.png\"") != std::string::npos, "file inside moved folder rewritten");
        Require(MeshSlots(root, "Content/Props/Tree/Tree.cnmesh") == std::vector<std::string>{ "Content/Props/Tree/Wood.mat" }, "moved mesh slot rewritten");
    }

    void TestMoveGltfWithBuffer(const fs::path& root, ReferenceIndex& index)
    {
        const RefactorPlan plan = AssetRefactor::PlanMove(index, "Content/Props/Tree/Tree.gltf", "Content/Models");
        Require(plan.IsValid() && plan.companions.size() == 1, "gltf move carries its buffer");
        std::string error;
        Require(AssetRefactor::Apply(plan, index, error), "gltf move applied");
        Require(Exists(root, "Content/Models/Tree.gltf") && Exists(root, "Content/Models/Tree.bin") && !Exists(root, "Content/Props/Tree/Tree.bin"), "gltf and bin moved");
        Require(Read(root, "Content/Scenes/Main.json").find("\"Content/Models/Tree.gltf#node4\"") != std::string::npos, "gltf reference moved");
    }

    void TestSceneRename(const fs::path& root, ReferenceIndex& index)
    {
        const RefactorPlan plan = AssetRefactor::PlanRename(index, "Content/Scenes/Main.json", "Start.json");
        Require(plan.startupSceneBefore == "Main" && plan.startupSceneAfter == "Start", "startup scene planned");
        std::string error;
        Require(AssetRefactor::Apply(plan, index, error), "scene rename applied");
        Require(Read(root, "Content/Game.json").find("\"startupScene\": \"Start\"") != std::string::npos, "Game.json updated");
        Require(AssetRefactor::RemapGameRelative("Content/Scenes/Main.json", plan) == "Content/Scenes/Start.json", "scene path remapped for the editor");
    }

    void TestRedirect(const fs::path& root, ReferenceIndex& index)
    {
        const RefactorPlan bad = AssetRefactor::PlanRedirect(index, "Content/Models/TreeOld/x.mat", "Content/Nope.mat");
        Require(!bad.IsValid(), "redirect to missing file rejected");
        const RefactorPlan plan = AssetRefactor::PlanRedirect(index, "Content/Models/TreeOld/x.mat", "Content/Props/Tree/Wood.mat");
        Require(plan.IsValid() && plan.edits.size() == 1, "redirect plan valid");
        std::string error;
        Require(AssetRefactor::Apply(plan, index, error), "redirect applied");
        Require(Read(root, "Content/Scenes/Start.json").find("\"Content/Models/TreeOld/x.mat\"") == std::string::npos, "missing reference redirected");
        Require(Exists(root, "Content/Props/Tree/Wood.mat"), "redirect does not move files");
    }

    bool RemoveForTest(const fs::path& path, std::string& error)
    {
        std::error_code code;
        fs::remove_all(path, code);
        error = code.message();
        return !code;
    }

    void TestDelete(const fs::path& root, ReferenceIndex& index)
    {
        Require(!AssetRefactor::PlanDelete(index, "Source/Foo.cpp").IsValid(), "delete outside content rejected");
        Require(!AssetRefactor::PlanDelete(index, "Content/Nope.mat").IsValid(), "delete of missing asset rejected");

        const RefactorPlan scene = AssetRefactor::PlanDelete(index, "Content/Scenes/Start.json");
        Require(scene.IsValid() && AnyStartsWith(scene.warnings, "Content/Game.json"), "deleting the startup scene warns");

        const RefactorPlan material = AssetRefactor::PlanDelete(index, "Content/Props/Tree/Wood.mat");
        Require(material.IsValid() && material.edits.size() == 3 && material.files.size() == 2, "material delete finds scene and mesh references");
        Require(std::all_of(material.edits.begin(), material.edits.end(), [](const ReferenceEdit& e) { return e.after.empty(); }), "references are cleared");
        std::string error;
        Require(AssetRefactor::Apply(material, index, error, RemoveForTest), "material delete applied");
        Require(!Exists(root, "Content/Props/Tree/Wood.mat"), "material file deleted");
        const std::string sceneText = Read(root, "Content/Scenes/Start.json");
        Require(sceneText.find("Content/Props/Tree/Wood.mat") == std::string::npos, "scene no longer references the material");
        Require(sceneText.find("\"Content/Props/Tree/Bark.mat.bak\"") != std::string::npos, "similar name untouched");
        Require(MeshSlots(root, "Content/Props/Tree/Tree.cnmesh") == std::vector<std::string>{ "" }, "mesh slot cleared");
        Require(MeshSerializer::Load(Utf8(AssetPaths::ToAbsolute(root, "Content/Props/Tree/Tree.cnmesh"))) != nullptr, "mesh still loads");

        const RefactorPlan gltf = AssetRefactor::PlanDelete(index, "Content/Models/Tree.gltf");
        Require(gltf.IsValid() && gltf.companions.size() == 1, "glTF delete includes its buffer");
        Require(AssetRefactor::Apply(gltf, index, error, RemoveForTest), "glTF delete applied");
        Require(!Exists(root, "Content/Models/Tree.gltf") && !Exists(root, "Content/Models/Tree.bin"), "glTF and bin deleted");
        Require(Read(root, "Content/Scenes/Start.json").find("\"legacy\": \"\"") != std::string::npos, "sub asset reference cleared");

        const RefactorPlan folder = AssetRefactor::PlanDelete(index, "Content/Props");
        Require(folder.IsValid() && folder.move.directory, "folder delete plan valid");
        Require(std::none_of(folder.files.begin(), folder.files.end(), [](const std::string& f) { return f.starts_with("Content/Props/"); }),
            "files inside the deleted folder are not rewritten");
        Require(AssetRefactor::Apply(folder, index, error, RemoveForTest), "folder delete applied");
        Require(!Exists(root, "Content/Props"), "folder deleted");
        Require(Read(root, "Content/Scenes/Start.json").find("Content/Props/") == std::string::npos, "references under the folder cleared");
        Require(index.FindReferencingFilesUnder("Content/Props").empty(), "index has no references to the deleted folder");
    }
}

int RunProbe(const fs::path& root, const std::string& operation, const std::string& asset, const std::string& argument)
{
    ReferenceIndex index;
    index.SetRoot(root);
    const auto start = std::chrono::steady_clock::now();
    index.Rebuild();
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("index: %zu files in %.2f ms, missing paths: %zu\n", index.GetFileCount(), elapsed, index.FindMissing().size());

    const auto planStart = std::chrono::steady_clock::now();
    const RefactorPlan plan = operation == "move"
        ? AssetRefactor::PlanMove(index, asset, argument)
        : AssetRefactor::PlanRename(index, asset, argument);
    const double planElapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - planStart).count();
    if (!plan.IsValid())
    {
        std::printf("plan error: %s\n", plan.error.c_str());
        return 1;
    }
    std::printf("plan: %s -> %s, %zu edits in %zu files, %zu warnings, %.2f ms\n", plan.move.from.c_str(), plan.move.to.c_str(),
        plan.edits.size(), plan.files.size(), plan.warnings.size(), planElapsed);
    for (const std::string& warning : plan.warnings)
    {
        std::printf("  warning: %s\n", warning.c_str());
    }

    std::string error;
    if (!AssetRefactor::Apply(plan, index, error))
    {
        std::printf("apply error: %s\n", error.c_str());
        return 1;
    }
    const size_t remaining = plan.move.directory
        ? index.FindReferencingFilesUnder(plan.move.from).size()
        : index.FindReferencingFiles(plan.move.from).size();
    std::printf("applied, files still referencing the old path: %zu, files referencing the new path: %zu\n", remaining,
        plan.move.directory ? index.FindReferencingFilesUnder(plan.move.to).size() : index.FindReferencingFiles(plan.move.to).size());
    return remaining == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    if (argc == 6 && std::string(argv[1]) == "--probe")
    {
        return RunProbe(fs::path(argv[2]), argv[3], argv[4], argv[5]);
    }

    const fs::path root = (argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / "AssetRefactorValidation") / "Game";
    BuildFixture(root);

    TestPathHelpers();
    TestIndex(root);
    TestValidation(root);

    ReferenceIndex index;
    index.SetRoot(root);
    const auto start = std::chrono::steady_clock::now();
    index.Rebuild();
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("index rebuilt: %zu files in %.2f ms\n", index.GetFileCount(), elapsed);

    TestRenameFile(root, index);
    TestCodeWarningAndCase(root, index);
    TestMoveFolder(root, index);
    TestMoveGltfWithBuffer(root, index);
    TestSceneRename(root, index);
    TestRedirect(root, index);
    TestDelete(root, index);

    if (g_Failures == 0)
    {
        std::printf("AssetRefactor validation passed\n");
        return 0;
    }
    std::printf("AssetRefactor validation failed: %d\n", g_Failures);
    return 1;
}
