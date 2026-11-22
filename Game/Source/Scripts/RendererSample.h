#pragma once

#include <Engine.h>
#include <Asset/Material/MaterialData.h>

#include <functional>
#include <string>
#include <vector>

// Controls only objects owned by RendererSample. Model registrations live in Start().
class RendererSample final : public Canape::ScriptComponent
{
    RTTR_ENABLE(Canape::ScriptComponent)
    RTTR_REGISTRATION_FRIEND

protected:
    void Start() override;
    void Update() override;
    void OnDestroy() override;

private:
    enum class RotationMode { Off, Model, Camera };

    struct Slot
    {
        std::string name;
        Canape::MeshRendererComponent* renderer = nullptr;
        uint32_t index = 0;
        Canape::MaterialData original;
        Canape::MaterialData current;
        bool mrEnabled = true;
        bool normalEnabled = true;
    };
    struct Model
    {
        std::string name;
        std::string path;
        Canape::Vector3 position{};
        Canape::Rotator rotation{};
        Canape::Vector3 scale{ 1.0f, 1.0f, 1.0f };
        float orbitHeight = 1.0f;
        bool skinned = false;
        Canape::GameObject* root = nullptr;
        std::vector<Canape::MeshRendererComponent*> renderers;
        std::vector<Canape::AnimatorComponent*> animators;
        std::vector<Slot> slots;
        std::vector<std::string> clips;
        size_t clipIndex = 0;
        bool playing = true;
        bool loop = true;
        float speed = 1.0f;
    };

    void AddStaticModel(const char* name, const char* mesh, const Canape::Vector3& position,
        const Canape::Rotator& rotation, const Canape::Vector3& scale, float orbitHeight);
    void AddSkinnedModel(const char* name, const char* prefab, std::vector<std::string> clips,
        const Canape::Vector3& position, const Canape::Rotator& rotation, const Canape::Vector3& scale,
        float orbitHeight);
    bool EnsureModelLoaded(size_t index);
    void CollectModel(Canape::TransformComponent* node, Model& model);
    void PrepareMaterials(Model& model, size_t modelIndex);
    void SelectModel(int index);
    void ResetMaterial(Model& model);
    void EditSlots(const std::function<void(Slot&)>& edit);
    void ResetAll();
    void ResetLighting();
    void SetEnvironmentMap(int index);
    void SetRotationMode(RotationMode mode);
    void ApplyOrbitPose();
    void SetHudMinimized(bool minimized);
    void ApplySun();
    void SetFogMode(Canape::FogMode mode);
    void SetCameraPose(bool fogView);
    void BuildHud();
    Canape::UIText* AddText(Canape::UIElement& parent, const std::string& name, const std::string& value,
        float x, float y, float width, float height, float size = 21.0f);
    Canape::UIButton* AddButton(Canape::UIElement& parent, const std::string& name, const std::string& caption,
        float x, float y, float width, const std::function<void()>& action);
    Canape::UIText* AddControl(Canape::UIElement& parent, const std::string& name, float x, float y,
        const std::function<void()>& minus, const std::function<void()>& plus);
    void RefreshHud();

    std::vector<Model> m_models;
    int m_selected = 0;
    int m_slot = -1; // -1: all material slots
    Canape::SkyboxComponent* m_sky = nullptr;
    Canape::LightComponent* m_light = nullptr;
    Canape::FogComponent* m_fog = nullptr;
    Canape::GameObject* m_camera = nullptr;
    Canape::UIElement* m_materialPanel = nullptr;
    Canape::UIElement* m_scenePanel = nullptr;
    Canape::UIElement* m_viewPanel = nullptr;
    Canape::UIText* m_uiToggleLabel = nullptr;
    std::vector<Canape::MeshRendererComponent*> m_fogRenderers;
    bool m_uiMinimized = false;
    bool m_environment = true;
    int m_environmentIndex = 0;
    float m_sunPitch = 30.0f;
    float m_sunYaw = 0.0f;
    float m_sunIntensity = 2.0f;
    RotationMode m_rotationMode = RotationMode::Off;
    float m_orbitAngle = 0.0f;
    float m_rotationSpeed = 20.0f;
    bool m_fogObjects = false;
    bool m_fogView = false;
    float m_brightness = 1.0f;
    Canape::FogMode m_fogMode = Canape::FogMode::Linear;
    Canape::Vector3 m_fogColor{ 0.62f, 0.70f, 0.78f };
    float m_fogStart = 18.0f;
    float m_fogEnd = 75.0f;
    float m_fogDensity = 0.035f;
    std::vector<Canape::UIText*> m_labels;
};
