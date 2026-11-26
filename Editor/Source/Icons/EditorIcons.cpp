#include "Icons/EditorIcons.h"
#include "EditorSettings.h"
#include "EditorUtils.h"

#include "Asset/Texture/TextureImporter.h"

#include <d3d11.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <system_error>

namespace fs = std::filesystem;
using namespace Canape;

namespace
{
    constexpr const char* kGameObjectKey = "gameobject";
    constexpr const char* kComponentTypePrefix = "componentType.";
    constexpr const char* kDefaultComponentKey = "component.default";
    constexpr const char* kDefaultFileKey = "file.default";

    std::string Lower(std::string text)
    {
        std::transform(text.begin(), text.end(), text.begin(),
            [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
        return text;
    }

    ImTextureID ToTextureId(ID3D11ShaderResourceView* view)
    {
        return static_cast<ImTextureID>(reinterpret_cast<intptr_t>(view));
    }

    // Engine component types, most specific first, mapped to an icon kind. Derived types inherit the kind.
    struct KindRule
    {
        const char* typeName;
        const char* kind;
    };
    constexpr KindRule kKindRules[] = {
        { "TransformComponent", "transform" },
        { "SkinnedMeshRendererComponent", "skinned_mesh" },
        { "MeshRendererComponent", "mesh" },
        { "CameraComponent", "camera" },
        { "LightComponent", "light" },
        { "AudioSourceComponent", "audio" },
        { "AnimatorComponent", "animator" },
        { "RigidBodyComponent", "rigidbody" },
        { "ColliderComponent", "collider" },
        { "HingeComponent", "joint" },
        { "ParticleEffectComponent", "particle" },
        { "TerrainGeneratorComponent", "terrain" },
        { "WaterVolumeComponent", "water" },
        { "WaveSurfaceComponent", "wave" },
        { "FogComponent", "fog" },
        { "SkyboxComponent", "skybox" },
        { "WindComponent", "wind" },
        { "StateMachine", "state_machine" },
        { "FsmState", "state_machine" },
        { "RenderableComponent", "mesh" },
        { "ScriptComponent", "script" },
    };

    struct FileCategory
    {
        const char* category;
        std::vector<const char*> extensions;
    };
    const std::vector<FileCategory>& FileCategories()
    {
        static const std::vector<FileCategory> categories = {
            { "prefab", { ".prefab" } },
            { "material", { ".mat" } },
            { "mesh", { ".cnmesh", ".cnskmesh" } },
            { "model", { ".gltf", ".glb", ".fbx", ".obj" } },
            { "texture", { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".hdr", ".exr" } },
            { "audio", { ".wav", ".mp3", ".ogg", ".flac" } },
            { "effect", { ".effect" } },
            { "animation", { ".aclip", ".anim" } },
            { "shader", { ".hlsl", ".hlsli" } },
            { "code", { ".h", ".hpp", ".cpp", ".inl", ".c" } },
            { "json", { ".json" } },
            { "text", { ".txt", ".md", ".ini", ".log" } },
            { "font", { ".ttf", ".otf" } },
            { "archive", { ".zip", ".7z", ".rar" } },
            { "binary", { ".bin" } },
        };
        return categories;
    }

    std::string JoinExtensions(const std::vector<const char*>& extensions)
    {
        std::string text;
        for (const char* extension : extensions)
        {
            text += (text.empty() ? "" : " ") + std::string(extension);
        }
        return text;
    }
}

EditorIcons& EditorIcons::Get()
{
    static EditorIcons icons;
    return icons;
}

void EditorIcons::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    m_Device = device;
    m_Context = context;
}

void EditorIcons::Shutdown()
{
    for (auto& [path, view] : m_Textures)
    {
        if (view)
        {
            view->Release();
        }
    }
    m_Textures.clear();
    m_Device = nullptr;
    m_Context = nullptr;
}

fs::path EditorIcons::GetIconDirectory()
{
    return EditorSettings::GetRepositoryDirectory() / "Editor" / "Resources" / "Icons";
}

fs::path EditorIcons::ResolvePath(const std::string& stored)
{
    const fs::path path = EditorUtils::FromUtf8(stored);
    if (path.is_absolute())
    {
        return path;
    }
    std::error_code error;
    const fs::path inGame = (EditorUtils::GetGameDirectory() / path).lexically_normal();
    if (fs::exists(inGame, error))
    {
        return inGame;
    }
    return (EditorSettings::GetRepositoryDirectory() / path).lexically_normal();
}

std::string EditorIcons::MakeStoredPath(const fs::path& file)
{
    const fs::path normal = file.lexically_normal();
    for (const fs::path& base : { EditorUtils::GetGameDirectory(), EditorSettings::GetRepositoryDirectory() })
    {
        const fs::path relative = normal.lexically_relative(base);
        if (!relative.empty() && *relative.begin() != "..")
        {
            return EditorUtils::ToUtf8(relative.generic_wstring());
        }
    }
    return EditorUtils::ToUtf8(normal.generic_wstring());
}

ImTextureID EditorIcons::Load(const fs::path& file)
{
    const std::string key = EditorUtils::ToUtf8(file.lexically_normal());
    if (const auto found = m_Textures.find(key); found != m_Textures.end())
    {
        return ToTextureId(found->second);
    }

    ID3D11ShaderResourceView* view = nullptr;
    std::error_code error;
    if (m_Device && m_Context && fs::is_regular_file(file, error))
    {
        if (const std::unique_ptr<TextureAsset> image = TextureImporter::LoadTexture(file, nullptr, TextureFormat::RGBA8_UNORM))
        {
            // Full mip chain so the 128px icons stay smooth at text size.
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = image->GetWidth();
            desc.Height = image->GetHeight();
            desc.MipLevels = 0;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

            ID3D11Texture2D* texture = nullptr;
            if (SUCCEEDED(m_Device->CreateTexture2D(&desc, nullptr, &texture)))
            {
                m_Context->UpdateSubresource(texture, 0, nullptr, image->GetPixels().data(), image->GetWidth() * 4, 0);
                if (SUCCEEDED(m_Device->CreateShaderResourceView(texture, nullptr, &view)))
                {
                    m_Context->GenerateMips(view);
                }
                texture->Release();
            }
        }
    }
    m_Textures[key] = view;
    return ToTextureId(view);
}

const std::vector<EditorIcons::Entry>& EditorIcons::GetGeneralEntries()
{
    static const std::vector<Entry> entries = {
        { kGameObjectKey, "GameObject", "Hierarchy rows", "gameobject.png" },
    };
    return entries;
}

const std::vector<EditorIcons::Entry>& EditorIcons::GetComponentKindEntries()
{
    static const std::vector<Entry> entries = [] {
        const std::pair<const char*, const char*> kinds[] = {
            { "transform", "Transform" }, { "mesh", "Mesh Renderer" }, { "skinned_mesh", "Skinned Mesh Renderer" },
            { "camera", "Camera" }, { "light", "Light" }, { "audio", "Audio Source" }, { "animator", "Animator" },
            { "rigidbody", "Rigid Body" }, { "collider", "Collider" }, { "joint", "Joint" }, { "particle", "Particle Effect" },
            { "terrain", "Terrain" }, { "water", "Water Volume" }, { "wave", "Wave Surface" }, { "fog", "Fog" },
            { "skybox", "Skybox" }, { "wind", "Wind" }, { "state_machine", "State Machine / FSM State" },
            { "script", "Script (game scripts)" }, { "default", "Other components" },
        };
        std::vector<Entry> result;
        for (const auto& [kind, label] : kinds)
        {
            result.push_back({ std::string("component.") + kind, label, "", std::string("component_") + kind + ".png" });
        }
        return result;
    }();
    return entries;
}

const std::vector<EditorIcons::Entry>& EditorIcons::GetFileEntries()
{
    static const std::vector<Entry> entries = [] {
        std::vector<Entry> result;
        result.push_back({ "file.folder", "Folder", "", "file_folder.png" });
        result.push_back({ "file.scene", "Scene", ".json in Content/Scenes", "file_scene.png" });
        for (const FileCategory& category : FileCategories())
        {
            std::string label = category.category;
            label[0] = static_cast<char>(label[0] - 'a' + 'A');
            result.push_back({ std::string("file.") + category.category, label, JoinExtensions(category.extensions),
                std::string("file_") + category.category + ".png" });
        }
        result.push_back({ kDefaultFileKey, "Other files", "", "file_default.png" });
        return result;
    }();
    return entries;
}

std::string EditorIcons::GetComponentKindKey(const rttr::type& type)
{
    for (const KindRule& rule : kKindRules)
    {
        const rttr::type ruleType = rttr::type::get_by_name(rule.typeName);
        if (ruleType.is_valid() && (type == ruleType || type.is_derived_from(ruleType)))
        {
            return std::string("component.") + rule.kind;
        }
    }
    return kDefaultComponentKey;
}

std::vector<EditorIcons::Entry> EditorIcons::GetComponentTypeEntries()
{
    std::vector<Entry> result;
    const rttr::type component = rttr::type::get_by_name("Component");
    for (const rttr::type& type : rttr::type::get_types())
    {
        if (!type.is_class() || type.is_wrapper() || type.is_pointer() || !component.is_valid()
            || type == component || !type.is_derived_from(component))
        {
            continue;
        }
        const std::string name = type.get_name().to_string();
        const std::string kindKey = GetComponentKindKey(type);
        result.push_back({ kComponentTypePrefix + name, name, "Default: " + kindKey.substr(std::string("component.").size()), "" });
    }
    std::sort(result.begin(), result.end(), [](const Entry& lhs, const Entry& rhs) { return lhs.label < rhs.label; });
    return result;
}

const EditorIcons::Entry* EditorIcons::FindEntry(const std::string& key) const
{
    for (const std::vector<Entry>* list : { &GetGeneralEntries(), &GetComponentKindEntries(), &GetFileEntries() })
    {
        for (const Entry& entry : *list)
        {
            if (entry.key == key)
            {
                return &entry;
            }
        }
    }
    return nullptr;
}

std::string EditorIcons::DefaultFileFor(const std::string& key) const
{
    if (const Entry* entry = FindEntry(key))
    {
        return entry->defaultFile;
    }
    if (key.starts_with(kComponentTypePrefix))
    {
        const rttr::type type = rttr::type::get_by_name(key.substr(std::string(kComponentTypePrefix).size()));
        if (const Entry* kind = FindEntry(type.is_valid() ? GetComponentKindKey(type) : kDefaultComponentKey))
        {
            return kind->defaultFile;
        }
    }
    return key.starts_with("file.") ? "file_default.png" : "component_default.png";
}

ImTextureID EditorIcons::GetDefault(const std::string& key)
{
    if (key.starts_with(kComponentTypePrefix))
    {
        // A type without its own override shows its kind's icon, which may itself be overridden.
        const rttr::type type = rttr::type::get_by_name(key.substr(std::string(kComponentTypePrefix).size()));
        return GetByKey(type.is_valid() ? GetComponentKindKey(type) : kDefaultComponentKey);
    }
    if (const ImTextureID icon = Load(GetIconDirectory() / DefaultFileFor(key)))
    {
        return icon;
    }
    return Load(GetIconDirectory() / (key.starts_with("file.") ? "file_default.png" : "component_default.png"));
}

ImTextureID EditorIcons::GetByKey(const std::string& key)
{
    if (const std::string* stored = EditorSettings::Get().FindIconOverride(key))
    {
        if (const ImTextureID icon = Load(ResolvePath(*stored)))
        {
            return icon;
        }
    }
    return GetDefault(key);
}

bool EditorIcons::IsOverrideBroken(const std::string& key)
{
    const std::string* stored = EditorSettings::Get().FindIconOverride(key);
    return stored && !Load(ResolvePath(*stored));
}

ImTextureID EditorIcons::GetGameObject()
{
    return GetByKey(kGameObjectKey);
}

ImTextureID EditorIcons::GetComponent(const rttr::type& type)
{
    return GetByKey(kComponentTypePrefix + type.get_name().to_string());
}

ImTextureID EditorIcons::GetFile(const fs::path& path, bool directory)
{
    if (directory)
    {
        return GetByKey("file.folder");
    }
    const std::string extension = Lower(EditorUtils::ToUtf8(path.extension()));
    if (extension == ".json" && EditorUtils::ToGameRelativePath(path).starts_with("Content/Scenes/"))
    {
        return GetByKey("file.scene");
    }
    for (const FileCategory& category : FileCategories())
    {
        if (std::find(category.extensions.begin(), category.extensions.end(), extension) != category.extensions.end())
        {
            return GetByKey(std::string("file.") + category.category);
        }
    }
    return GetByKey(kDefaultFileKey);
}

float EditorIcons::GetInlineSize()
{
    return ImGui::GetFontSize();
}

std::string EditorIcons::GetLabelPadding()
{
    const float space = std::max(ImGui::CalcTextSize(" ").x, 1.0f);
    const int count = static_cast<int>(std::ceil((GetInlineSize() + ImGui::GetStyle().ItemInnerSpacing.x) / space));
    return std::string(static_cast<size_t>(count), ' ');
}

void EditorIcons::DrawAtLabelStart(ImTextureID icon, const ImVec2& start)
{
    if (!icon)
    {
        return;
    }
    const float size = GetInlineSize();
    const float centerY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
    const ImVec2 min(start.x, std::floor(centerY - size * 0.5f));
    ImGui::GetWindowDrawList()->AddImage(icon, min, ImVec2(min.x + size, min.y + size));
}

void EditorIcons::DrawOnTreeNodeLabel(ImTextureID icon, bool framed)
{
    // Matches ImGui's TreeNodeBehavior: the label starts after the arrow and frame padding.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float offset = ImGui::GetFontSize() + style.FramePadding.x * (framed ? 3.0f : 2.0f);
    DrawAtLabelStart(icon, ImVec2(ImGui::GetItemRectMin().x + offset, 0.0f));
}
