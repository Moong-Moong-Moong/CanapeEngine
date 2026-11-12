#pragma once

#include "EditorContext.h"
#include "Panels/StateMachineInspector.h"

#include <filesystem>
#include <string>
#include <unordered_map>

class InspectorPanel
{
public:
    static constexpr const char* kName = "Inspector";

    void Draw(EditorContext& context, Canape::Scene* scene);

private:
    struct EulerCache
    {
        Canape::Quaternion rotation;
        Canape::Vector3 euler;
    };

    void DrawAsset(Canape::Scene* scene, const std::filesystem::path& file);
    void DrawGameObjectHeader(Canape::GameObject& gameObject);
    void DrawComponent(Canape::Scene& scene, Canape::Component& component, Canape::Component*& removeRequest);
    void DrawAddComponent(Canape::Scene& scene, Canape::GameObject& gameObject);

    bool DrawValue(Canape::Scene* scene, rttr::variant& value, const std::string& cacheKey);
    bool DrawGameObjectReference(Canape::Scene* scene, rttr::variant& value);
    bool DrawComponentReference(Canape::Scene* scene, rttr::variant& value);
    bool DrawSequence(Canape::Scene* scene, rttr::variant& value, const std::string& cacheKey);
    bool DrawObject(Canape::Scene* scene, rttr::variant& value, const std::string& cacheKey);
    bool DrawRotation(rttr::variant& value, const std::string& cacheKey);
    bool EditText(const char* label, std::string& value);

    unsigned int m_ActiveTextId = 0;
    std::string m_ActiveTextBuffer;
    std::unordered_map<std::string, EulerCache> m_EulerCache;
    StateMachineInspector m_StateMachineInspector;

    std::filesystem::path m_MaterialFile;
    Canape::MaterialData m_Material;
    bool m_MaterialLoaded = false;
};
