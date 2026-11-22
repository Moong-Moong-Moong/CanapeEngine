#include "Scripts/RendererSample.h"
#include "Scripts/FlyCamera.h"

#include "Asset/Material/MaterialFile.h"
#include "Scene/Scene.h"
#include "Scene/Component/AnimatorComponent.h"
#include "Scene/Component/FogComponent.h"
#include "Scene/Component/LightComponent.h"
#include "Scene/Component/MeshRendererComponent.h"
#include "Scene/Component/SkinnedMeshRendererComponent.h"
#include "Scene/Component/SkyboxComponent.h"
#include "Scene/GameObject/GameObject.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <string>

RTTR_REGISTRATION
{
    rttr::registration::class_<RendererSample>("RendererSample")
        .constructor<>()(rttr::policy::ctor::as_raw_ptr);
}

using namespace Canape;

namespace
{
    constexpr std::array<const char*, 6> kEnvironmentMaps{
        "Content/HDRI/galileo_cross.dds", "Content/HDRI/grace_cross.dds",
        "Content/HDRI/rnl_cross.dds", "Content/HDRI/stpeters_cross.dds",
        "Content/HDRI/uffizi_cross.dds", "Content/HDRI/ludwikowice_farmland_cube.dds"
    };
    constexpr std::array<const char*, 6> kEnvironmentNames{
        "Galileo", "Grace", "RNL", "St. Peters", "Uffizi", "Farmland"
    };
    constexpr UIColor kPanel{ 17, 22, 32, 218 };
    constexpr UIColor kButton{ 49, 70, 97, 240 };
    constexpr float kRow = 43.0f;

    float Clamp01(float value) { return std::clamp(value, 0.0f, 1.0f); }
    void Shift(float& value, float delta) { value = Clamp01(std::round((value + delta) * 20.0f) / 20.0f); }
    bool SameMaterial(const MaterialData& a, const MaterialData& b)
    {
        return a.desc.baseColor.X == b.desc.baseColor.X && a.desc.baseColor.Y == b.desc.baseColor.Y &&
            a.desc.baseColor.Z == b.desc.baseColor.Z && a.desc.baseColor.W == b.desc.baseColor.W &&
            a.desc.metallic == b.desc.metallic && a.desc.roughness == b.desc.roughness &&
            a.desc.alphaMode == b.desc.alphaMode && a.desc.alphaCutoff == b.desc.alphaCutoff &&
            a.baseColorTexture == b.baseColorTexture &&
            a.metallicRoughnessTexture == b.metallicRoughnessTexture && a.normalTexture == b.normalTexture &&
            a.occlusionTexture == b.occlusionTexture && a.emissiveTexture == b.emissiveTexture;
    }
}

void RendererSample::Start()
{
    Scene* scene = GetOwnerObject()->GetScene();
    m_camera = scene->GetMainCamera() ? scene->GetMainCamera()->GetOwnerObject() : nullptr;
    if (auto* object = scene->FindGameObjectByName("Sample Sun")) m_light = object->GetComponent<LightComponent>();
    if (auto* object = scene->FindGameObjectByName("Sample Sky")) m_sky = object->GetComponent<SkyboxComponent>();
    if (auto* object = scene->FindGameObjectByName("Sample Fog")) m_fog = object->GetComponent<FogComponent>();
    if (m_sky && !m_sky->GetCubemap().IsValid()) m_sky->SetEnabled(false);
    if (m_light) m_sunIntensity = m_light->GetIntensity();

    // Register assets first; create each model only when selected. Reuse its instance afterward.
    AddStaticModel("Alfa Romeo", "Content/Models/AlfaRomeo_1933/alfa_romeo_spider_1933_v2.gltf",
        { 0.58f, 0.02f, 0.18f }, Rotator{}, { 0.85f, 0.85f, 0.85f }, 0.56f);
    AddSkinnedModel("Master", "Content/Models/Master/MasterChief_v5.7_walk_scl_SkeletonModel.prefab",
        { "Content/Models/Master/Armature_mixamo.com_Layer0.aclip" },
        { 0.0f, 0.0f, 0.0f }, Rotator{}, { 1.0f, 1.0f, 1.0f }, 1.0f);
    AddSkinnedModel("Tank", "Content/Models/Tank/SiegeTank_v2.xxx_SkeletonModel.prefab",
        { "Content/Models/Tank/Armature_Stand_full.aclip", "Content/Models/Tank/Armature_Attack_full.aclip",
          "Content/Models/Tank/Armature_Stand 01_full.aclip", "Content/Models/Tank/Armature_Stand Work Start_full.aclip",
          "Content/Models/Tank/Armature_Stand Work_full.aclip", "Content/Models/Tank/Armature_Stand Work End_full.aclip" },
        { 0.0f, 0.0f, 0.0f }, Rotator{}, { 1.0f, 1.0f, 1.0f }, 1.0f);
    AddStaticModel("Aleksandr", "Content/Models/Aleksandr/aleksandr_class_archipelago_frigate.gltf",
        { 0.0f, 0.275f, -0.164f }, Rotator{}, { 0.065f, 0.065f, 0.065f }, 1.37f);
    AddStaticModel("Cannon", "Content/Models/Cannon_N/Cannon_N_v2.gltf",
        { 0.37f, 0.006f, -0.49f }, Rotator{}, { 0.62f, 0.62f, 0.62f }, 0.55f);
    AddStaticModel("Cerberus", "Content/Models/Cerberus+/cerberusffvii_gun_v2_4k.gltf",
        { -0.077f, 1.03f, -0.91f }, Rotator{}, { 2.2f, 2.2f, 2.2f }, 0.72f);
    AddStaticModel("Shield", "Content/Models/SnW/SnW_Shield_v2.gltf",
        { -0.17f, 1.2f, 0.0f }, Rotator(50.0f, 0.0f, 0.0f), { 1.8f, 1.8f, 1.8f }, 1.2f);
    AddStaticModel("USN Hornet", "Content/Models/USN-Hornet/USN-Nornet_v2.gltf",
        { 0.12f, 0.128f, -0.36f }, Rotator{}, { 0.022f, 0.022f, 0.022f }, 0.4f);

    // Untextured, neutral material on identical meshes for fog comparison.
    MaterialData markerMaterial;
    markerMaterial.desc.baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    markerMaterial.desc.metallic = 0.0f;
    markerMaterial.desc.roughness = 1.0f;
    AssetOwnerInfo markerOwner;
    markerOwner.sceneName = "RendererSample";
    markerOwner.objectName = "Fog Markers";
    markerOwner.componentName = "RendererSample";
    Application::Get().GetAssetManager().CreateMaterial("RendererSample/FogMarkerPlain", markerMaterial, markerOwner);
    constexpr std::array<float, 4> markerX{ 3.0f, 8.0f, 14.0f, 21.0f };
    for (int i = 0; i < 4; ++i)
    {
        GameObject* object = GetGameObject(CreateGameObject(std::format("Fog Marker {}", i + 1)));
        object->GetTransform()->SetPosition({ markerX[i], 1.0f, 16.0f + i * 19.0f });
        object->GetTransform()->SetScale({ 2.0f, 2.0f, 2.0f });
        auto* renderer = object->AddComponent<MeshRendererComponent>();
        renderer->SetMeshPath("Builtin/Sphere");
        renderer->SetMaterial(0, "RendererSample/FogMarkerPlain");
        renderer->SetVisible(false);
        m_fogRenderers.push_back(renderer);
    }

    SelectModel(0);
    BuildHud();
    RefreshHud();
}

void RendererSample::AddStaticModel(const char* name, const char* mesh, const Vector3& position,
    const Rotator& rotation, const Vector3& scale, float orbitHeight)
{
    if (!std::filesystem::exists(AssetManager::GetAssetFile(mesh))) return;
    Model model;
    model.name = name;
    model.path = mesh;
    model.position = position;
    model.rotation = rotation;
    model.scale = scale;
    model.orbitHeight = orbitHeight;
    m_models.push_back(std::move(model));
}

void RendererSample::CollectModel(TransformComponent* node, Model& model)
{
    if (!node) return;
    GameObject* object = node->GetOwnerObject();
    for (auto* renderer : object->GetComponents<MeshRendererComponent>()) model.renderers.push_back(renderer);
    for (auto* animator : object->GetComponents<AnimatorComponent>()) model.animators.push_back(animator);
    for (auto* child : node->GetChildren()) CollectModel(child, model);
}

void RendererSample::AddSkinnedModel(const char* name, const char* prefab, std::vector<std::string> clips,
    const Vector3& position, const Rotator& rotation, const Vector3& scale, float orbitHeight)
{
    if (!std::filesystem::exists(AssetManager::GetAssetFile(prefab))) return;
    Model model;
    model.name = name;
    model.path = prefab;
    model.position = position;
    model.rotation = rotation;
    model.scale = scale;
    model.orbitHeight = orbitHeight;
    model.skinned = true;
    for (const std::string& clip : clips)
    {
        if (std::filesystem::exists(AssetManager::GetAssetFile(clip))) model.clips.push_back(clip);
    }
    m_models.push_back(std::move(model));
}

bool RendererSample::EnsureModelLoaded(size_t index)
{
    Model& model = m_models[index];
    if (model.root) return true;

    GameObject* root = model.skinned ? GetGameObject(Instantiate(model.path)) :
        GetGameObject(CreateGameObject(std::format("Sample {}", model.name)));
    if (!root) return false;
    root->GetTransform()->SetPosition(model.position);
    root->GetTransform()->SetRotation(model.rotation.ToQuaternion());
    root->GetTransform()->SetScale(model.scale);
    if (model.skinned)
    {
        // Keep the prefab skeleton, skin renderer and Animator together.
        CollectModel(root->GetTransform(), model);
        if (model.renderers.empty() || model.animators.empty())
        {
            Destroy(root);
            model.renderers.clear();
            model.animators.clear();
            return false;
        }
        if (!model.clips.empty())
            for (auto* animator : model.animators) animator->SetClip(model.clips.front());
    }
    else
    {
        auto* renderer = root->AddComponent<MeshRendererComponent>();
        renderer->SetMeshPath(model.path);
        if (!Application::Get().GetAssetManager().GetMesh(renderer->GetMesh()))
        {
            Destroy(root);
            return false;
        }
        model.renderers.push_back(renderer);
    }
    model.root = root;
    PrepareMaterials(model, index);
    return true;
}

void RendererSample::PrepareMaterials(Model& model, size_t modelIndex)
{
    AssetManager& assets = Application::Get().GetAssetManager();
    for (size_t rendererIndex = 0; rendererIndex < model.renderers.size(); ++rendererIndex)
    {
        MeshRendererComponent* renderer = model.renderers[rendererIndex];
        const MeshAsset* mesh = assets.GetMesh(renderer->GetMesh());
        if (!mesh) continue;
        uint32_t count = 1;
        for (const auto& submesh : mesh->GetSubMeshes()) count = std::max(count, submesh.materialSlot + 1);
        for (uint32_t index = 0; index < count; ++index)
        {
            MaterialData data;
            if (index < mesh->GetMaterialSlots().size())
            {
                data = mesh->GetMaterialSlots()[index];
                if (MaterialFile::IsMaterialPath(data.name))
                    MaterialFile::Load(AssetManager::GetAssetFile(data.name), data);
            }
            Slot slot;
            slot.name = std::format("RendererSample/model{}/renderer{}/slot{}", modelIndex, rendererIndex, index);
            slot.renderer = renderer;
            slot.index = index;
            slot.original = data;
            slot.current = data;
            AssetOwnerInfo owner;
            owner.sceneName = "RendererSample";
            owner.objectName = model.name;
            owner.componentName = "RendererSample";
            if (assets.CreateMaterial(slot.name, data, owner).IsValid() && renderer->SetMaterial(index, slot.name))
                model.slots.push_back(std::move(slot));
        }
    }
}

void RendererSample::ResetMaterial(Model& model)
{
    AssetManager& assets = Application::Get().GetAssetManager();
    for (Slot& slot : model.slots)
    {
        if (!SameMaterial(slot.current, slot.original)) assets.UpdateMaterial(slot.name, slot.original);
        slot.current = slot.original;
        slot.mrEnabled = true;
        slot.normalEnabled = true;
    }
}

void RendererSample::EditSlots(const std::function<void(Slot&)>& edit)
{
    if (m_models.empty()) return;
    Model& model = m_models[m_selected];
    for (size_t i = 0; i < model.slots.size(); ++i)
    {
        if (m_slot >= 0 && i != static_cast<size_t>(m_slot)) continue;
        const MaterialData before = model.slots[i].current;
        edit(model.slots[i]);
        if (!SameMaterial(before, model.slots[i].current))
            Application::Get().GetAssetManager().UpdateMaterial(model.slots[i].name, model.slots[i].current);
    }
    RefreshHud();
}

void RendererSample::SelectModel(int index)
{
    if (m_models.empty()) return;
    const int count = static_cast<int>(m_models.size());
    const int direction = index < m_selected ? -1 : 1;
    index = (index % count + count) % count;
    bool loaded = false;
    for (int attempt = 0; attempt < count; ++attempt)
    {
        const int candidate = (index + direction * attempt + count) % count;
        if (!EnsureModelLoaded(static_cast<size_t>(candidate))) continue;
        index = candidate;
        loaded = true;
        break;
    }
    if (!loaded) return;
    if (m_selected >= 0 && m_selected < count && m_selected != index && m_models[m_selected].root)
        m_models[m_selected].root->GetTransform()->SetRotation(m_models[m_selected].rotation.ToQuaternion());
    if (m_selected >= 0 && m_selected < count && m_selected != index) ResetMaterial(m_models[m_selected]);
    m_selected = index;
    m_orbitAngle = 0.0f;
    m_slot = -1;
    for (size_t i = 0; i < m_models.size(); ++i)
    {
        Model& model = m_models[i];
        const bool active = static_cast<int>(i) == index;
        for (auto* renderer : model.renderers) renderer->SetVisible(active && !m_fogView);
        for (auto* animator : model.animators)
        {
            animator->SetEnabled(active);
            if (active)
            {
                animator->Stop();
                animator->SetSpeed(1.0f);
                animator->Play(true);
            }
        }
        if (active)
        {
            ResetMaterial(model);
            model.playing = true;
            model.loop = true;
            model.speed = 1.0f;
        }
    }
    if (m_rotationMode == RotationMode::Camera) ApplyOrbitPose();
    RefreshHud();
}

void RendererSample::ApplySun()
{
    if (!m_light) return;
    m_light->SetIntensity(m_sunIntensity);
    m_light->GetOwnerObject()->GetTransform()->SetRotation(Rotator(m_sunPitch, m_sunYaw, 0.0f).ToQuaternion());
    RefreshHud();
}

void RendererSample::SetEnvironmentMap(int index)
{
    if (!m_sky) return;
    const int count = static_cast<int>(kEnvironmentMaps.size());
    index = (index % count + count) % count;
    if (!std::filesystem::exists(AssetManager::GetAssetFile(kEnvironmentMaps[index]))) return;
    const std::string previous = m_sky->GetCubemapPath();
    if (!m_sky->SetCubemap(kEnvironmentMaps[index]))
    {
        if (!previous.empty()) m_sky->SetCubemap(previous);
        return;
    }
    m_environmentIndex = index;
    m_sky->SetEnabled(m_environment);
    RefreshHud();
}

void RendererSample::SetRotationMode(RotationMode mode)
{
    if (mode != RotationMode::Off && m_fogView) SetCameraPose(false);
    if (m_rotationMode == RotationMode::Model && !m_models.empty() && m_models[m_selected].root)
        m_models[m_selected].root->GetTransform()->SetRotation(m_models[m_selected].rotation.ToQuaternion());
    m_rotationMode = mode;
    m_orbitAngle = 0.0f;
    if (m_camera)
        if (auto* fly = m_camera->GetComponent<FlyCamera>()) fly->SetEnabled(mode != RotationMode::Camera);
    if (mode == RotationMode::Camera)
    {
        m_fogView = false;
        if (!m_models.empty())
            for (auto* renderer : m_models[m_selected].renderers) renderer->SetVisible(true);
        ApplyOrbitPose();
    }
    RefreshHud();
}

void RendererSample::ApplyOrbitPose()
{
    if (!m_camera || m_models.empty()) return;
    const float radians = Math::ToRadians(m_orbitAngle);
    const float focusHeight = m_models[m_selected].orbitHeight;
    auto* transform = m_camera->GetTransform();
    transform->SetPosition({ std::sin(radians) * 6.0f, focusHeight + 1.3f, -std::cos(radians) * 6.0f });
    transform->LookAt({ 0.0f, focusHeight, 0.0f });
}

void RendererSample::SetHudMinimized(bool minimized)
{
    m_uiMinimized = minimized;
    if (m_materialPanel) m_materialPanel->SetVisible(!minimized);
    if (m_scenePanel) m_scenePanel->SetVisible(!minimized);
    if (m_viewPanel) m_viewPanel->SetVisible(!minimized);
    if (m_uiToggleLabel) m_uiToggleLabel->SetText(minimized ? "Show UI (H)" : "Hide UI (H)");
}

void RendererSample::ResetLighting()
{
    m_environment = true;
    m_brightness = 1.0f;
    SetEnvironmentMap(0);
    if (m_sky)
    {
        m_sky->SetBrightness(m_brightness);
        m_sky->SetEnabled(m_sky->GetCubemap().IsValid());
    }
    m_sunPitch = 30.0f;
    m_sunYaw = 0.0f;
    m_sunIntensity = 2.0f;
    ApplySun();
    if (m_light) m_light->SetCastShadow(true);
    RefreshHud();
}

void RendererSample::SetFogMode(FogMode mode)
{
    m_fogMode = mode;
    if (m_fog) m_fog->SetMode(mode);
    RefreshHud();
}

void RendererSample::SetCameraPose(bool fogView)
{
    if (m_rotationMode == RotationMode::Camera) SetRotationMode(RotationMode::Off);
    m_fogView = fogView;
    if (!m_models.empty())
        for (auto* renderer : m_models[m_selected].renderers) renderer->SetVisible(!fogView);
    if (!m_camera) return;
    auto* transform = m_camera->GetTransform();
    transform->SetPosition(fogView ? Vector3{ 5.0f, 2.7f, -8.0f } : Vector3{ 0.0f, 2.3f, -6.0f });
    transform->SetRotation(Rotator(fogView ? 1.5f : 11.0f, 0.0f, 0.0f).ToQuaternion());
    RefreshHud();
}

void RendererSample::ResetAll()
{
    for (Model& model : m_models) ResetMaterial(model);
    SetRotationMode(RotationMode::Off);
    SelectModel(0);
    m_rotationSpeed = 20.0f;
    ResetLighting();
    m_fogStart = 18.0f;
    m_fogEnd = 75.0f;
    m_fogDensity = 0.035f;
    m_fogColor = { 0.62f, 0.70f, 0.78f };
    if (m_fog)
    {
        m_fog->SetColor(m_fogColor);
        m_fog->SetStartDistance(m_fogStart);
        m_fog->SetEndDistance(m_fogEnd);
        m_fog->SetDensity(m_fogDensity);
    }
    if (m_fog) m_fog->SetEnabled(false);
    SetFogMode(FogMode::Linear);
    m_fogObjects = false;
    for (auto* renderer : m_fogRenderers) renderer->SetVisible(false);
    SetCameraPose(false);
    RefreshHud();
}

void RendererSample::Update()
{
    const InputManager& input = Application::Get().GetInputManager();
    if (input.IsKeyPressed(Key::Left)) SelectModel(m_selected - 1);
    if (input.IsKeyPressed(Key::Right)) SelectModel(m_selected + 1);
    if (input.IsKeyPressed(Key::R)) ResetAll();
    if (input.IsKeyPressed(Key::V)) SetCameraPose(!m_fogView);
    if (input.IsKeyPressed(Key::F)) SetCameraPose(false);
    if (input.IsKeyPressed(Key::H)) SetHudMinimized(!m_uiMinimized);
    if (!m_fogView && m_rotationMode != RotationMode::Off && !m_models.empty())
    {
        m_orbitAngle = std::fmod(m_orbitAngle + m_rotationSpeed * Time::DeltaTime(), 360.0f);
        if (m_rotationMode == RotationMode::Camera) ApplyOrbitPose();
        else if (m_models[m_selected].root)
        {
            const Rotator& base = m_models[m_selected].rotation;
            m_models[m_selected].root->GetTransform()->SetRotation(
                Rotator(base.Pitch, base.Yaw + m_orbitAngle, base.Roll).ToQuaternion());
        }
    }
    if (m_selected < static_cast<int>(m_models.size()) && !m_models[m_selected].animators.empty() &&
        input.IsKeyPressed(Key::P))
    {
        Model& model = m_models[m_selected];
        model.playing = !model.playing;
        for (auto* animator : model.animators)
        {
            if (model.playing && !animator->IsPlaying()) { animator->Stop(); animator->Play(model.loop); }
            animator->SetSpeed(model.playing ? model.speed : 0.0f);
        }
        RefreshHud();
    }
    if (!m_models.empty())
    {
        Model& model = m_models[m_selected];
        if (model.playing && !model.animators.empty() && !model.animators.front()->IsPlaying())
        {
            model.playing = false;
            RefreshHud();
        }
    }
}

void RendererSample::OnDestroy()
{
    // AssetManager can outlive this scene; reset the sample-owned material resources.
    for (Model& model : m_models) ResetMaterial(model);
}

UIText* RendererSample::AddText(UIElement& parent, const std::string& name, const std::string& value,
    float x, float y, float width, float height, float size)
{
    auto* text = parent.AddChild<UIText>(name);
    text->SetPosition({ x, y });
    text->SetSize({ width, height });
    text->SetText(value);
    text->SetFontSize(size);
    text->SetVerticalAlign(UITextAlign::Center);
    return text;
}

UIButton* RendererSample::AddButton(UIElement& parent, const std::string& name, const std::string& caption,
    float x, float y, float width, const std::function<void()>& action)
{
    auto* button = parent.AddChild<UIButton>(name);
    button->SetPosition({ x, y });
    button->SetSize({ width, 37.0f });
    button->SetColor(kButton);
    auto* label = AddText(*button, name + "Label", caption, 0, 0, width, 37.0f, 20.0f);
    label->SetHorizontalAlign(UITextAlign::Center);
    button->OnClicked = action;
    return button;
}

UIText* RendererSample::AddControl(UIElement& parent, const std::string& name, float x, float y,
    const std::function<void()>& minus, const std::function<void()>& plus)
{
    auto* label = AddText(parent, name + "Value", "", x, y, 300.0f, 37.0f);
    AddButton(parent, name + "Minus", "−", x + 315.0f, y, 49.0f, minus);
    AddButton(parent, name + "Plus", "+", x + 370.0f, y, 49.0f, plus);
    m_labels.push_back(label);
    return label;
}

void RendererSample::BuildHud()
{
    auto* canvas = AddComponent<UICanvasComponent>(Vector2{ 1920.0f, 1080.0f });
    UIElement& root = canvas->GetRoot();
    auto* left = root.AddChild<UIImage>("MaterialPanel");
    left->SetPosition({ 20.0f, 20.0f });
    left->SetSize({ 440.0f, 695.0f });
    left->SetColor(kPanel);
    auto* right = root.AddChild<UIImage>("ScenePanel");
    right->SetAnchor({ 1.0f, 0.0f });
    right->SetPivot({ 1.0f, 0.0f });
    right->SetPosition({ -20.0f, 20.0f });
    right->SetSize({ 440.0f, 910.0f });
    right->SetColor(kPanel);
    auto* view = root.AddChild<UIImage>("ViewPanel");
    view->SetPosition({ 480.0f, 20.0f });
    view->SetSize({ 440.0f, 405.0f });
    view->SetColor(kPanel);
    m_materialPanel = left;
    m_scenePanel = right;
    m_viewPanel = view;
    auto* toggle = AddButton(root, "HudToggle", "Hide UI (H)", 230.0f, 20.0f, 140.0f,
        [this] { SetHudMinimized(!m_uiMinimized); });
    toggle->SetAnchor({ 0.5f, 0.0f });
    toggle->SetPivot({ 0.5f, 0.0f });
    m_uiToggleLabel = toggle->Find<UIText>("HudToggleLabel");

    AddText(*left, "Title", "RENDERER SAMPLE", 12, 4, 420, 42, 27);
    AddControl(*left, "Model", 12, 53,
        [this] { SelectModel(m_selected - 1); }, [this] { SelectModel(m_selected + 1); });
    AddControl(*left, "Slot", 12, 53 + kRow,
        [this] { const int n = static_cast<int>(m_models[m_selected].slots.size()); m_slot = m_slot <= -1 ? n - 1 : m_slot - 1; RefreshHud(); },
        [this] { const int n = static_cast<int>(m_models[m_selected].slots.size()); m_slot = m_slot + 1 >= n ? -1 : m_slot + 1; RefreshHud(); });

    auto colorEdit = [this](int channel, float delta)
    {
        EditSlots([=](Slot& slot)
        {
            float* channels[] = { &slot.current.desc.baseColor.X, &slot.current.desc.baseColor.Y, &slot.current.desc.baseColor.Z };
            Shift(*channels[channel], delta);
        });
    };
    const char* colors[] = { "Red", "Green", "Blue" };
    for (int i = 0; i < 3; ++i)
        AddControl(*left, colors[i], 12, 53 + (i + 2) * kRow,
            [=] { colorEdit(i, -0.05f); }, [=] { colorEdit(i, 0.05f); });
    AddControl(*left, "Metallic", 12, 53 + 5 * kRow,
        [this] { EditSlots([](Slot& s) { Shift(s.current.desc.metallic, -0.05f); }); },
        [this] { EditSlots([](Slot& s) { Shift(s.current.desc.metallic, 0.05f); }); });
    AddControl(*left, "Roughness", 12, 53 + 6 * kRow,
        [this] { EditSlots([](Slot& s) { Shift(s.current.desc.roughness, -0.05f); }); },
        [this] { EditSlots([](Slot& s) { Shift(s.current.desc.roughness, 0.05f); }); });
    AddControl(*left, "MR Map", 12, 53 + 7 * kRow,
        [this] { EditSlots([](Slot& s) { if (!s.original.metallicRoughnessTexture.empty()) { s.mrEnabled = false; s.current.metallicRoughnessTexture.clear(); } }); },
        [this] { EditSlots([](Slot& s) { s.mrEnabled = true; s.current.metallicRoughnessTexture = s.original.metallicRoughnessTexture; }); });
    AddControl(*left, "Normal Map", 12, 53 + 8 * kRow,
        [this] { EditSlots([](Slot& s) { if (!s.original.normalTexture.empty()) { s.normalEnabled = false; s.current.normalTexture.clear(); } }); },
        [this] { EditSlots([](Slot& s) { s.normalEnabled = true; s.current.normalTexture = s.original.normalTexture; }); });
    AddButton(*left, "ResetMaterial", "Reset selected material", 12, 53 + 9 * kRow, 408,
        [this] { ResetMaterial(m_models[m_selected]); RefreshHud(); });
    AddText(*left, "MaterialHelp", "MR factor × map channels. AO stays linked.\nLeft/Right: model  |  F: camera home\nWASD + right drag: fly  |  wheel: speed\nR: reset all  |  V: fog view  |  P: pause\nH: hide/show UI", 12, 53 + 10 * kRow, 408, 145, 19);

    AddText(*right, "SceneTitle", "LIGHT / ANIMATION / FOG", 12, 4, 420, 42, 26);
    AddControl(*right, "Environment", 12, 53,
        [this] { m_environment = false; if (m_sky) m_sky->SetEnabled(false); RefreshHud(); },
        [this] { m_environment = true; if (m_sky) m_sky->SetEnabled(m_sky->GetCubemap().IsValid()); RefreshHud(); });
    AddControl(*right, "Brightness", 12, 53 + kRow,
        [this] { m_brightness = std::max(0.0f, m_brightness - 0.1f); if (m_sky) m_sky->SetBrightness(m_brightness); RefreshHud(); },
        [this] { m_brightness = std::min(3.0f, m_brightness + 0.1f); if (m_sky) m_sky->SetBrightness(m_brightness); RefreshHud(); });
    AddControl(*right, "Shadows", 12, 53 + 2 * kRow,
        [this] { if (m_light) m_light->SetCastShadow(false); RefreshHud(); },
        [this] { if (m_light) m_light->SetCastShadow(true); RefreshHud(); });
    AddControl(*right, "Animation", 12, 53 + 3 * kRow,
        [this] { Model& m = m_models[m_selected]; if (m.animators.empty()) return; m.playing = false; for (auto* a : m.animators) a->SetSpeed(0); RefreshHud(); },
        [this] { Model& m = m_models[m_selected]; if (m.animators.empty()) return; m.playing = true; for (auto* a : m.animators) { if (!a->IsPlaying()) { a->Stop(); a->Play(m.loop); } a->SetSpeed(m.speed); } RefreshHud(); });
    AddControl(*right, "AnimSpeed", 12, 53 + 4 * kRow,
        [this] { Model& m = m_models[m_selected]; if (m.animators.empty()) return; m.speed = std::max(0.25f, m.speed - 0.25f); for (auto* a : m.animators) a->SetSpeed(m.playing ? m.speed : 0); RefreshHud(); },
        [this] { Model& m = m_models[m_selected]; if (m.animators.empty()) return; m.speed = std::min(2.0f, m.speed + 0.25f); for (auto* a : m.animators) a->SetSpeed(m.playing ? m.speed : 0); RefreshHud(); });
    AddControl(*right, "Loop", 12, 53 + 5 * kRow,
        [this] { Model& m = m_models[m_selected]; if (m.animators.empty()) return; m.loop = false; for (auto* a : m.animators) { a->Play(false); a->SetSpeed(m.playing ? m.speed : 0); } RefreshHud(); },
        [this] { Model& m = m_models[m_selected]; if (m.animators.empty()) return; m.loop = true; for (auto* a : m.animators) { a->Play(true); a->SetSpeed(m.playing ? m.speed : 0); } RefreshHud(); });
    AddButton(*right, "Restart", "Restart animation", 12, 53 + 6 * kRow, 200,
        [this] { Model& m = m_models[m_selected]; for (auto* a : m.animators) { a->Stop(); a->Play(m.loop); a->SetSpeed(m.playing ? m.speed : 0); } RefreshHud(); });
    AddButton(*right, "Clip", "Next clip", 220, 53 + 6 * kRow, 200,
        [this] { Model& m = m_models[m_selected]; if (m.clips.empty()) return; m.clipIndex = (m.clipIndex + 1) % m.clips.size(); for (auto* a : m.animators) { a->SetClip(m.clips[m.clipIndex]); a->Stop(); a->Play(m.loop); a->SetSpeed(m.playing ? m.speed : 0); } RefreshHud(); });
    AddControl(*right, "FogObjects", 12, 53 + 7 * kRow,
        [this] { m_fogObjects = false; for (auto* r : m_fogRenderers) r->SetVisible(false); RefreshHud(); },
        [this] { m_fogObjects = true; for (auto* r : m_fogRenderers) r->SetVisible(true); RefreshHud(); });
    AddControl(*right, "FogView", 12, 53 + 8 * kRow,
        [this] { SetCameraPose(false); }, [this] { SetCameraPose(true); });
    AddControl(*right, "Fog", 12, 53 + 9 * kRow,
        [this] { if (m_fog) m_fog->SetEnabled(false); RefreshHud(); },
        [this] { if (m_fog) m_fog->SetEnabled(true); RefreshHud(); });
    AddControl(*right, "FogMode", 12, 53 + 10 * kRow,
        [this] { SetFogMode(m_fogMode == FogMode::Linear ? FogMode::Exp2 : m_fogMode == FogMode::Exp ? FogMode::Linear : FogMode::Exp); },
        [this] { SetFogMode(m_fogMode == FogMode::Linear ? FogMode::Exp : m_fogMode == FogMode::Exp ? FogMode::Exp2 : FogMode::Linear); });
    for (int i = 0; i < 3; ++i)
    {
        const std::string name = std::format("FogColor{}", i);
        AddControl(*right, name, 12, 53 + (11 + i) * kRow,
            [this, i] { float* p[] = { &m_fogColor.X, &m_fogColor.Y, &m_fogColor.Z }; Shift(*p[i], -0.05f); if (m_fog) m_fog->SetColor(m_fogColor); RefreshHud(); },
            [this, i] { float* p[] = { &m_fogColor.X, &m_fogColor.Y, &m_fogColor.Z }; Shift(*p[i], 0.05f); if (m_fog) m_fog->SetColor(m_fogColor); RefreshHud(); });
    }
    AddControl(*right, "FogStart", 12, 53 + 14 * kRow,
        [this] { if (m_fogMode != FogMode::Linear) return; m_fogStart = std::max(0.0f, m_fogStart - 2); if (m_fog) m_fog->SetStartDistance(m_fogStart); RefreshHud(); },
        [this] { if (m_fogMode != FogMode::Linear) return; m_fogStart = std::min(m_fogEnd - 2, m_fogStart + 2); if (m_fog) m_fog->SetStartDistance(m_fogStart); RefreshHud(); });
    AddControl(*right, "FogEnd", 12, 53 + 15 * kRow,
        [this] { if (m_fogMode != FogMode::Linear) return; m_fogEnd = std::max(m_fogStart + 2, m_fogEnd - 2); if (m_fog) m_fog->SetEndDistance(m_fogEnd); RefreshHud(); },
        [this] { if (m_fogMode != FogMode::Linear) return; m_fogEnd = std::min(140.0f, m_fogEnd + 2); if (m_fog) m_fog->SetEndDistance(m_fogEnd); RefreshHud(); });
    AddControl(*right, "FogDensity", 12, 53 + 16 * kRow,
        [this] { if (m_fogMode == FogMode::Linear) return; m_fogDensity = std::max(0.0f, m_fogDensity - 0.005f); if (m_fog) m_fog->SetDensity(m_fogDensity); RefreshHud(); },
        [this] { if (m_fogMode == FogMode::Linear) return; m_fogDensity = std::min(0.2f, m_fogDensity + 0.005f); if (m_fog) m_fog->SetDensity(m_fogDensity); RefreshHud(); });
    AddButton(*right, "ResetScene", "Reset env + sun", 12, 53 + 17 * kRow, 200,
        [this] { ResetLighting(); });
    AddButton(*right, "ResetAll", "Reset all", 220, 53 + 17 * kRow, 200, [this] { ResetAll(); });
    AddText(*right, "EnvironmentNote", "Environment brightness affects background + IBL.\n0 removes contribution; baking status is unavailable.",
        12, 53 + 18 * kRow, 410, 62, 18);

    AddText(*view, "ViewTitle", "ENVIRONMENT / SUN / VIEW", 12, 4, 420, 42, 25);
    AddControl(*view, "EnvironmentMap", 12, 53,
        [this] { SetEnvironmentMap(m_environmentIndex - 1); },
        [this] { SetEnvironmentMap(m_environmentIndex + 1); });
    AddControl(*view, "SunYaw", 12, 53 + kRow,
        [this] { m_sunYaw = std::fmod(m_sunYaw + 350.0f, 360.0f); ApplySun(); },
        [this] { m_sunYaw = std::fmod(m_sunYaw + 10.0f, 360.0f); ApplySun(); });
    AddControl(*view, "SunPitch", 12, 53 + 2 * kRow,
        [this] { m_sunPitch = std::max(-80.0f, m_sunPitch - 10.0f); ApplySun(); },
        [this] { m_sunPitch = std::min(80.0f, m_sunPitch + 10.0f); ApplySun(); });
    AddControl(*view, "SunIntensity", 12, 53 + 3 * kRow,
        [this] { m_sunIntensity = std::max(0.0f, m_sunIntensity - 0.25f); ApplySun(); },
        [this] { m_sunIntensity = std::min(5.0f, m_sunIntensity + 0.25f); ApplySun(); });
    AddControl(*view, "RotationMode", 12, 53 + 4 * kRow,
        [this] { SetRotationMode(static_cast<RotationMode>((static_cast<int>(m_rotationMode) + 2) % 3)); },
        [this] { SetRotationMode(static_cast<RotationMode>((static_cast<int>(m_rotationMode) + 1) % 3)); });
    AddControl(*view, "RotationSpeed", 12, 53 + 5 * kRow,
        [this] { m_rotationSpeed = std::max(0.0f, m_rotationSpeed - 5.0f); RefreshHud(); },
        [this] { m_rotationSpeed = std::min(120.0f, m_rotationSpeed + 5.0f); RefreshHud(); });
    AddText(*view, "ViewHelp", "Camera orbit pauses fly controls until switched off.\nRotation speed is in degrees per second.",
        12, 53 + 6 * kRow, 410, 65, 18);
}

void RendererSample::RefreshHud()
{
    if (m_labels.size() != 31 || m_models.empty()) return;
    const Model& model = m_models[m_selected];
    const Slot* slot = model.slots.empty() ? nullptr : &model.slots[m_slot < 0 ? 0 : m_slot];
    m_labels[0]->SetText(std::format("Model: {}", model.name));
    m_labels[1]->SetText(m_slot < 0 ? "Material: all slots" : std::format("Material: slot {}", m_slot + 1));
    const Vector4 color = slot ? slot->current.desc.baseColor : Vector4{ 1, 1, 1, 1 };
    m_labels[2]->SetText(std::format("BaseColor R: {:.2f}", color.X));
    m_labels[3]->SetText(std::format("BaseColor G: {:.2f}", color.Y));
    m_labels[4]->SetText(std::format("BaseColor B: {:.2f}", color.Z));
    m_labels[5]->SetText(std::format("Metallic: {:.2f}", slot ? slot->current.desc.metallic : 0));
    m_labels[6]->SetText(std::format("Roughness: {:.2f}", slot ? slot->current.desc.roughness : 0));
    m_labels[7]->SetText(!slot || slot->original.metallicRoughnessTexture.empty() ? "MR Map: absent" :
        std::format("MR Map: {}", slot->mrEnabled ? "ON" : "OFF"));
    m_labels[8]->SetText(!slot || slot->original.normalTexture.empty() ? "Normal Map: absent" :
        std::format("Normal Map: {}", slot->normalEnabled ? "ON" : "OFF"));
    m_labels[9]->SetText(std::format("Environment: {}", !m_sky || !m_sky->GetCubemap().IsValid() ? "unavailable" : m_environment ? "ON" : "OFF"));
    m_labels[10]->SetText(std::format("Brightness: {:.1f}", m_brightness));
    m_labels[11]->SetText(std::format("Sun shadows: {}", m_light && m_light->GetCastShadow() ? "ON" : "OFF"));
    m_labels[12]->SetText(model.animators.empty() ? "Animation: unavailable" : std::format("Animation: {}", model.playing ? "playing" : "paused"));
    m_labels[13]->SetText(model.animators.empty() ? "Speed: unavailable" : std::format("Speed: {:.2f}x", model.speed));
    m_labels[14]->SetText(model.animators.empty() ? "Loop: unavailable" : std::format("Loop: {}", model.loop ? "ON" : "OFF"));
    m_labels[15]->SetText(std::format("Fog markers: {}", m_fogObjects ? "shown" : "hidden"));
    m_labels[16]->SetText(std::format("Camera: {}", m_fogView ? "fog" : "model"));
    m_labels[17]->SetText(std::format("Fog: {}", m_fog && m_fog->IsEnabled() ? "ON" : "OFF"));
    m_labels[18]->SetText(std::format("Fog mode: {}", m_fogMode == FogMode::Linear ? "Linear" : m_fogMode == FogMode::Exp ? "Exp" : "Exp2"));
    m_labels[19]->SetText(std::format("Fog color R: {:.2f}", m_fogColor.X));
    m_labels[20]->SetText(std::format("Fog color G: {:.2f}", m_fogColor.Y));
    m_labels[21]->SetText(std::format("Fog color B: {:.2f}", m_fogColor.Z));
    m_labels[22]->SetText(m_fogMode == FogMode::Linear ? std::format("Fog start: {:.0f}", m_fogStart) : "Fog start: Linear only");
    m_labels[23]->SetText(m_fogMode == FogMode::Linear ? std::format("Fog end: {:.0f}", m_fogEnd) : "Fog end: Linear only");
    m_labels[24]->SetText(m_fogMode != FogMode::Linear ? std::format("Density: {:.3f}", m_fogDensity) : "Density: Exp / Exp2 only");
    m_labels[25]->SetText(std::format("Env map: {}", kEnvironmentNames[m_environmentIndex]));
    m_labels[26]->SetText(std::format("Sun yaw: {:.0f}°", m_sunYaw));
    m_labels[27]->SetText(std::format("Sun pitch: {:.0f}°", m_sunPitch));
    m_labels[28]->SetText(std::format("Sun intensity: {:.2f}", m_sunIntensity));
    m_labels[29]->SetText(std::format("Auto rotate: {}", m_rotationMode == RotationMode::Off ? "off" :
        m_rotationMode == RotationMode::Model ? "model" : "camera"));
    m_labels[30]->SetText(std::format("Rotation speed: {:.0f}°/s", m_rotationSpeed));
}
