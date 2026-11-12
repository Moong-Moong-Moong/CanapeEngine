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
    void DrawComponent(EditorContext& context, Canape::Scene& scene, Canape::Component& component, Canape::Component*& removeRequest);
    void DrawMaterialSlots(EditorContext& context, Canape::Scene& scene, Canape::MeshRendererComponent& renderer);
    // MaterialData 편집 UI. 값이 바뀌면 true.
    bool DrawMaterialData(Canape::Scene* scene, Canape::MaterialData& material);
    // 에셋 경로 기준 캐시. reload면 파일에서 다시 읽는다. 로드 실패 시 nullptr.
    Canape::MaterialData* GetMaterialData(const std::string& assetPath, bool reload);
    void SaveMaterialData(const std::string& assetPath, const Canape::MaterialData& material);
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
    std::unordered_map<std::string, Canape::MaterialData> m_MaterialCache;
};
