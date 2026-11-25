#pragma once

#include <Engine.h>
#include <imgui.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11ShaderResourceView;

// Icons for the hierarchy, inspector and content browser.
// Every icon has a key; its image is the user's override from EditorSettings, or the default PNG in Editor/Resources/Icons.
//   gameobject               hierarchy rows
//   component.<kind>         component kinds (transform, mesh, light, script, ...)
//   componentType.<Type>     one specific component type, falls back to its kind
//   file.<category>          content browser file categories (folder, scene, material, ...)
class EditorIcons
{
public:
    struct Entry
    {
        std::string key;
        std::string label;
        std::string detail;      // extra text in the settings window (extensions, kind)
        std::string defaultFile; // file name in Editor/Resources/Icons
    };

    static EditorIcons& Get();

    void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();

    ImTextureID GetGameObject();
    ImTextureID GetComponent(const rttr::type& type);
    ImTextureID GetFile(const std::filesystem::path& path, bool directory);
    ImTextureID GetByKey(const std::string& key);

    // Settings window support.
    static const std::vector<Entry>& GetGeneralEntries();
    static const std::vector<Entry>& GetComponentKindEntries();
    static const std::vector<Entry>& GetFileEntries();
    static std::vector<Entry> GetComponentTypeEntries();
    static std::string GetComponentKindKey(const rttr::type& type);
    ImTextureID GetDefault(const std::string& key);
    // True when the key has an override that could not be loaded (the default is shown instead).
    bool IsOverrideBroken(const std::string& key);
    static std::filesystem::path GetIconDirectory();
    static std::filesystem::path ResolvePath(const std::string& stored);
    static std::string MakeStoredPath(const std::filesystem::path& file);

    // Image helpers. Labels drawn after an icon start with GetLabelPadding() so the icon has room.
    static float GetInlineSize();
    static std::string GetLabelPadding();
    // Draws `icon` at the start of the label of the last tree node / collapsing header.
    static void DrawOnTreeNodeLabel(ImTextureID icon, bool framed);
    // Draws `icon` at `start` (screen position of the label start), centred on the last item's height.
    static void DrawAtLabelStart(ImTextureID icon, const ImVec2& start);

private:
    ImTextureID Load(const std::filesystem::path& file);
    const Entry* FindEntry(const std::string& key) const;
    std::string DefaultFileFor(const std::string& key) const;

    ID3D11Device* m_Device = nullptr;
    ID3D11DeviceContext* m_Context = nullptr;
    // Keyed by absolute path; nullptr entries remember files that failed to load.
    std::unordered_map<std::string, ID3D11ShaderResourceView*> m_Textures;
};
