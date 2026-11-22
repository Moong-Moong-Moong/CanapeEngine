#include "Core/Reflection/Reflection.h"
#include "Scene/Component/MeshRendererComponent.h"
#include "Scene/Component/SkinnedMeshRendererComponent.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"
#include "Scene/Serialization/SceneSerializer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <limits>

using namespace Canape;
using Json = nlohmann::json;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    try
    {
        RegisterEngineTypes();
        Scene scene("Outline serialization");
        auto* object = scene.GetGameObject(scene.AddGameObject("Outline"));
        auto* mesh = object->AddComponent<MeshRendererComponent>();
        auto* skin = object->AddComponent<SkinnedMeshRendererComponent>();
        Require(!mesh->IsOutlineEnabled() && !skin->IsOutlineEnabled(), "static/skinned default false");
        Require(mesh->GetOutlineColor().X == 1.0f && mesh->GetOutlineColor().Y == .8f
            && mesh->GetOutlineThickness() == 2 && skin->GetOutlineOpacity() == 1.0f, "legacy style defaults");
        auto meshProperty = rttr::type::get<MeshRendererComponent>().get_property("outlineEnabled");
        auto skinProperty = rttr::type::get<SkinnedMeshRendererComponent>().get_property("outlineEnabled");
        Require(meshProperty.is_valid() && skinProperty.is_valid(), "RTTR exposes inherited outline property");
        Require(meshProperty.set_value(mesh, true) && skinProperty.set_value(skin, true), "RTTR inspector bool edit");
        for (MeshRendererComponent* component : {mesh, static_cast<MeshRendererComponent*>(skin)})
        {
            const auto type = rttr::type::get(*component);
            Require(type.get_property("outlineColor").set_value(component, Vector3{.2f, .4f, .6f})
                && type.get_property("outlineThickness").set_value(component, uint32_t(5))
                && type.get_property("outlineOpacity").set_value(component, .35f), "inherited RTTR style editing");
        }
        auto json = Json::parse(SceneSerializer::SaveToString(scene));
        unsigned count = 0;
        for (auto& entry : json["gameObjects"][0]["components"])
        {
            if (entry["type"] == "MeshRendererComponent" || entry["type"] == "SkinnedMeshRendererComponent")
            {
                Require(entry["properties"]["outlineEnabled"] == true, "scene writes inherited bool");
                Require(entry["properties"]["outlineThickness"] == 5
                    && entry["properties"]["outlineOpacity"].get<float>() == .35f
                    && entry["properties"]["outlineColor"]["Y"].get<float>() == .4f, "scene writes full style");
                ++count;
            }
        }
        Require(count == 2, "both renderers serialized");
        const char* path = "Intermediate/OutlineValidation/roundtrip.json";
        { std::ofstream file(path); file << json.dump(); }
        auto restored = SceneSerializer::Load(path);
        Require(restored != nullptr, "load serialized scene");
        auto* restoredObject = restored->FindGameObjectByName("Outline");
        Require(restoredObject->GetComponent<MeshRendererComponent>()->IsOutlineEnabled()
            && restoredObject->GetComponent<SkinnedMeshRendererComponent>()->IsOutlineEnabled(), "true survives round trip");
        for (auto* component : restoredObject->GetComponents<MeshRendererComponent>())
            Require(component->GetOutlineThickness() == 5 && component->GetOutlineOpacity() == .35f
                && component->GetOutlineColor().Z == .6f, "static/skinned styles survive round trip");
        for (auto& entry : json["gameObjects"][0]["components"])
        {
            entry["properties"].erase("outlineEnabled");
            entry["properties"].erase("outlineColor");
            entry["properties"].erase("outlineThickness");
            entry["properties"].erase("outlineOpacity");
        }
        { std::ofstream file(path); file << json.dump(); }
        restored = SceneSerializer::Load(path);
        Require(restored != nullptr, "load legacy scene");
        restoredObject = restored->FindGameObjectByName("Outline");
        Require(!restoredObject->GetComponent<MeshRendererComponent>()->IsOutlineEnabled()
            && !restoredObject->GetComponent<SkinnedMeshRendererComponent>()->IsOutlineEnabled(), "legacy scene defaults false");
        for (auto* component : restoredObject->GetComponents<MeshRendererComponent>())
            Require(component->GetOutlineThickness() == 2 && component->GetOutlineOpacity() == 1.0f
                && component->GetOutlineColor().Y == .8f, "missing legacy properties retain defaults");
        mesh->SetOutlineColor({-1, 2, std::numeric_limits<float>::quiet_NaN()});
        mesh->SetOutlineThickness(0);
        mesh->SetOutlineOpacity(-1);
        Require(mesh->GetOutlineColor().X == 0 && mesh->GetOutlineColor().Y == 1 && mesh->GetOutlineColor().Z == .1f
            && mesh->GetOutlineThickness() == 1 && mesh->GetOutlineOpacity() == 0, "lower bounds and nonfinite color");
        mesh->SetOutlineThickness(UINT32_MAX);
        mesh->SetOutlineOpacity(2);
        Require(mesh->GetOutlineThickness() == MaxOutlineThickness && mesh->GetOutlineOpacity() == 1, "upper bounds");
        mesh->SetOutlineOpacity(std::numeric_limits<float>::infinity());
        Require(mesh->GetOutlineOpacity() == 1, "nonfinite opacity sanitized");
        std::cout << "PASS: static/skinned defaults, inherited RTTR styles, scene round trip, legacy defaults and input bounds\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
