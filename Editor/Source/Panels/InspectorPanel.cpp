#include "Panels/InspectorPanel.h"
#include "EditorUtils.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace Canape;

namespace
{
    constexpr float kLabelColumnWidth = 0.4f;

    template <typename T>
    bool DragInteger(rttr::variant& value, ImGuiDataType dataType)
    {
        T number = value.get_value<T>();
        if (ImGui::DragScalar("##value", dataType, &number, 0.2f))
        {
            value = number;
            return true;
        }
        return false;
    }

    bool DrawInteger(rttr::variant& value)
    {
        const rttr::type type = value.get_type();
        if (type == rttr::type::get<int8_t>()) return DragInteger<int8_t>(value, ImGuiDataType_S8);
        if (type == rttr::type::get<uint8_t>()) return DragInteger<uint8_t>(value, ImGuiDataType_U8);
        if (type == rttr::type::get<int16_t>()) return DragInteger<int16_t>(value, ImGuiDataType_S16);
        if (type == rttr::type::get<uint16_t>()) return DragInteger<uint16_t>(value, ImGuiDataType_U16);
        if (type == rttr::type::get<int32_t>()) return DragInteger<int32_t>(value, ImGuiDataType_S32);
        if (type == rttr::type::get<uint32_t>()) return DragInteger<uint32_t>(value, ImGuiDataType_U32);
        if (type == rttr::type::get<int64_t>()) return DragInteger<int64_t>(value, ImGuiDataType_S64);
        if (type == rttr::type::get<uint64_t>()) return DragInteger<uint64_t>(value, ImGuiDataType_U64);
        ImGui::TextDisabled("%s", type.get_name().data());
        return false;
    }

    template <typename T, int Count>
    bool DrawFloats(rttr::variant& value)
    {
        T vector = value.get_value<T>();
        if (ImGui::DragScalarN("##value", ImGuiDataType_Float, &vector.X, Count, 0.05f, nullptr, nullptr, "%.3f"))
        {
            value = vector;
            return true;
        }
        return false;
    }

    bool IsComponentPointer(const rttr::type& type)
    {
        return type.is_pointer() && type.get_raw_type().is_derived_from(rttr::type::get<Component>());
    }

    GameObject* AcceptGameObjectDrop(Scene* scene)
    {
        GameObject* dropped = nullptr;
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kGameObjectPayload))
            {
                dropped = scene ? scene->GetGameObject(scene->FindGameObjectById(*static_cast<const uint64_t*>(payload->Data))) : nullptr;
            }
            ImGui::EndDragDropTarget();
        }
        return dropped;
    }

    bool BeginPropertyTable(const char* id)
    {
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        {
            return false;
        }
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, kLabelColumnWidth);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f - kLabelColumnWidth);
        return true;
    }

    void BeginPropertyRow(const char* name)
    {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(name);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
    }
}

void InspectorPanel::Draw(EditorContext& context, Scene* scene)
{
    if (!ImGui::Begin(kName))
    {
        ImGui::End();
        return;
    }

    GameObject* gameObject = context.GetSelected(scene);
    if (!gameObject || gameObject->IsPendingDestroy())
    {
        if (!context.GetSelectedAsset().empty())
        {
            DrawAsset(scene, context.GetSelectedAsset());
        }
        else
        {
            ImGui::TextDisabled("Select a GameObject in the Hierarchy");
        }
        ImGui::End();
        return;
    }

    ImGui::PushID(reinterpret_cast<void*>(static_cast<uintptr_t>(gameObject->GetId())));
    DrawGameObjectHeader(*gameObject);

    Component* removeRequest = nullptr;
    for (Component* component : gameObject->GetAllComponents())
    {
        DrawComponent(*scene, *component, removeRequest);
    }

    ImGui::Spacing();
    DrawAddComponent(*scene, *gameObject);
    ImGui::PopID();

    if (removeRequest)
    {
        gameObject->RemoveComponent(removeRequest);
    }
    ImGui::End();
}

void InspectorPanel::DrawGameObjectHeader(GameObject& gameObject)
{
    bool active = gameObject.IsActive();
    if (ImGui::Checkbox("##Active", &active))
    {
        gameObject.SetActive(active);
    }
    ImGui::SameLine();

    std::string name = gameObject.GetName();
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (EditText("##Name", name) && !name.empty())
    {
        gameObject.SetName(name);
    }
    ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(gameObject.GetId()));
    ImGui::Separator();
}

void InspectorPanel::DrawComponent(Scene& scene, Component& component, Component*& removeRequest)
{
    const rttr::instance instance(&component);
    const rttr::type type = instance.get_derived_type();
    const bool isTransform = type == rttr::type::get<TransformComponent>();

    ImGui::PushID(&component);
    const bool open = ImGui::CollapsingHeader(type.get_name().data(), ImGuiTreeNodeFlags_DefaultOpen);

    if (!isTransform && ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Remove Component"))
        {
            removeRequest = &component;
        }
        ImGui::EndPopup();
    }

    if (open && BeginPropertyTable("##Properties"))
    {
        for (const rttr::property& property : type.get_properties())
        {
            const std::string name = property.get_name().to_string();
            const bool readOnly = property.is_readonly() || (isTransform && name == "parent");

            ImGui::PushID(name.c_str());
            BeginPropertyRow(name.c_str());
            ImGui::BeginDisabled(readOnly);

            rttr::variant value = property.get_value(instance);
            if (DrawValue(&scene, value, std::to_string(component.GetId()) + "." + name) && !readOnly)
            {
                property.set_value(instance, value);
            }

            ImGui::EndDisabled();
            ImGui::PopID();
        }

        if (auto* camera = dynamic_cast<CameraComponent*>(&component))
        {
            BeginPropertyRow("main camera");
            if (scene.GetMainCamera() == camera)
            {
                ImGui::TextDisabled("Main Camera");
            }
            else if (ImGui::Button("Set Main Camera"))
            {
                scene.SetMainCamera(camera);
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

void InspectorPanel::DrawAddComponent(Scene& scene, GameObject& gameObject)
{
    const float width = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("Add Component", ImVec2(width, 0.0f)))
    {
        ImGui::OpenPopup("##AddComponent");
    }

    if (!ImGui::BeginPopup("##AddComponent"))
    {
        return;
    }

    std::vector<rttr::type> types;
    for (const rttr::type& type : rttr::type::get<Component>().get_derived_classes())
    {
        if (type != rttr::type::get<TransformComponent>() && type.get_constructor().is_valid())
        {
            types.push_back(type);
        }
    }
    std::sort(types.begin(), types.end(), [](const rttr::type& lhs, const rttr::type& rhs)
    {
        return lhs.get_name() < rhs.get_name();
    });

    for (const rttr::type& type : types)
    {
        if (!ImGui::MenuItem(type.get_name().data()))
        {
            continue;
        }

        rttr::variant created = type.create();
        bool ok = false;
        Component* component = created.is_valid() ? created.convert<Component*>(&ok) : nullptr;
        if (!ok || !component)
        {
            if (created.is_valid())
            {
                type.destroy(created);
            }
            continue;
        }

        gameObject.AddComponent(std::unique_ptr<Component>(component));
        if (auto* camera = dynamic_cast<CameraComponent*>(component); camera && !scene.GetMainCamera())
        {
            scene.SetMainCamera(camera);
        }
    }
    ImGui::EndPopup();
}

void InspectorPanel::DrawAsset(Scene* scene, const std::filesystem::path& file)
{
    ImGui::TextUnformatted(EditorUtils::ToUtf8(file.filename()).c_str());
    ImGui::TextDisabled("%s", EditorUtils::ToGameRelativePath(file).c_str());
    ImGui::Separator();

    if (!MaterialFile::IsMaterialPath(EditorUtils::ToUtf8(file)))
    {
        ImGui::TextDisabled("No inspector for this file type");
        return;
    }

    if (file != m_MaterialFile)
    {
        m_MaterialFile = file;
        m_MaterialLoaded = MaterialFile::Load(file, m_Material);
    }
    if (!m_MaterialLoaded)
    {
        ImGui::TextDisabled("Failed to load material");
        return;
    }

    ImGui::PushID("MaterialAsset");
    bool changed = false;
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen) && BeginPropertyTable("##Material"))
    {
        BeginPropertyRow("baseColorTexture");
        ImGui::PushID("baseColorTexture");
        rttr::variant texture = m_Material.baseColorTexture;
        if (DrawValue(scene, texture, {}))
        {
            m_Material.baseColorTexture = AssetManager::ToAssetPath(texture.get_value<std::string>());
            changed = true;
        }
        ImGui::PopID();

        BeginPropertyRow("baseColor");
        ImGui::PushID("baseColor");
        if (ImGui::ColorEdit4("##value", &m_Material.desc.baseColor.X, ImGuiColorEditFlags_Float))
        {
            changed = true;
        }
        ImGui::PopID();

        const rttr::instance instance(m_Material.desc);
        for (const rttr::property& property : rttr::type::get<MaterialDesc>().get_properties())
        {
            const std::string name = property.get_name().to_string();
            if (name == "baseColor")
            {
                continue;
            }

            ImGui::PushID(name.c_str());
            BeginPropertyRow(name.c_str());
            rttr::variant value = property.get_value(instance);
            if (DrawValue(scene, value, {}))
            {
                changed |= property.set_value(instance, value);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();

    if (changed)
    {
        MaterialFile::Save(file, m_Material);
        Application::Get().GetAssetManager().UpdateMaterial(EditorUtils::ToUtf8(file), m_Material);
    }
}

bool InspectorPanel::DrawValue(Scene* scene, rttr::variant& value, const std::string& cacheKey)
{
    const rttr::type type = value.get_type();

    if (type == rttr::type::get<bool>())
    {
        bool flag = value.get_value<bool>();
        if (ImGui::Checkbox("##value", &flag))
        {
            value = flag;
            return true;
        }
        return false;
    }
    if (type == rttr::type::get<float>())
    {
        float number = value.get_value<float>();
        if (ImGui::DragFloat("##value", &number, 0.05f, 0.0f, 0.0f, "%.3f"))
        {
            value = number;
            return true;
        }
        return false;
    }
    if (type == rttr::type::get<double>())
    {
        double number = value.get_value<double>();
        if (ImGui::DragScalar("##value", ImGuiDataType_Double, &number, 0.05f))
        {
            value = number;
            return true;
        }
        return false;
    }
    if (type.is_arithmetic())
    {
        return DrawInteger(value);
    }
    if (type == rttr::type::get<std::string>())
    {
        std::string text = value.get_value<std::string>();
        bool changed = EditText("##value", text);
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(EditorUtils::kAssetPathPayload))
            {
                text.assign(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize));
                changed = true;
            }
            ImGui::EndDragDropTarget();
        }
        if (changed)
        {
            value = text;
        }
        return changed;
    }
    if (type.is_enumeration())
    {
        const rttr::enumeration enumeration = type.get_enumeration();
        const std::string current = enumeration.value_to_name(value).to_string();
        bool changed = false;
        if (ImGui::BeginCombo("##value", current.c_str()))
        {
            for (const rttr::string_view name : enumeration.get_names())
            {
                const std::string option = name.to_string();
                if (ImGui::Selectable(option.c_str(), option == current))
                {
                    value = enumeration.name_to_value(name);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }
    if (type == rttr::type::get<Vector2>()) return DrawFloats<Vector2, 2>(value);
    if (type == rttr::type::get<Vector3>()) return DrawFloats<Vector3, 3>(value);
    if (type == rttr::type::get<Vector4>()) return DrawFloats<Vector4, 4>(value);
    if (type == rttr::type::get<Quaternion>()) return DrawRotation(value, cacheKey);
    if (type == rttr::type::get<Handle<GameObject>>()) return DrawGameObjectReference(scene, value);
    if (IsComponentPointer(type)) return DrawComponentReference(scene, value);
    if (value.is_sequential_container()) return DrawSequence(scene, value, cacheKey);
    if (type.is_class() && !type.get_properties().empty()) return DrawObject(scene, value, cacheKey);

    ImGui::TextDisabled("%s", type.get_name().data());
    return false;
}

bool InspectorPanel::DrawGameObjectReference(Scene* scene, rttr::variant& value)
{
    const GameObject* target = scene ? scene->GetGameObject(value.get_value<Handle<GameObject>>()) : nullptr;
    const std::string label = target ? target->GetName() : std::string("None (GameObject)");
    ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f));

    bool changed = false;
    if (GameObject* dropped = AcceptGameObjectDrop(scene))
    {
        value = dropped->GetHandle();
        changed = true;
    }
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Clear"))
        {
            value = Handle<GameObject>{};
            changed = true;
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool InspectorPanel::DrawComponentReference(Scene* scene, rttr::variant& value)
{
    const rttr::type type = value.get_type();
    const rttr::type rawType = type.get_raw_type();

    bool ok = false;
    const Component* target = value.convert<Component*>(&ok);
    std::string label = std::string("None (") + rawType.get_name().data() + ")";
    if (ok && target && target->GetOwnerObject())
    {
        label = target->GetOwnerObject()->GetName() + " (" + rawType.get_name().data() + ")";
    }
    ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0.0f));

    bool changed = false;
    if (GameObject* dropped = AcceptGameObjectDrop(scene))
    {
        for (Component* component : dropped->GetAllComponents())
        {
            if (!rttr::type::get(*component).is_derived_from(rawType))
            {
                continue;
            }

            rttr::variant pointer = component;
            if (pointer.convert(type))
            {
                value = pointer;
                changed = true;
            }
            break;
        }
    }
    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Clear"))
        {
            rttr::variant pointer = static_cast<Component*>(nullptr);
            if (pointer.convert(type))
            {
                value = pointer;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

bool InspectorPanel::DrawSequence(Scene* scene, rttr::variant& value, const std::string& cacheKey)
{
    rttr::variant_sequential_view view = value.create_sequential_view();
    bool changed = false;

    ImGui::Text("Size %zu", view.get_size());
    if (view.is_dynamic())
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("+"))
        {
            changed |= view.set_size(view.get_size() + 1);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("-") && view.get_size() > 0)
        {
            changed |= view.set_size(view.get_size() - 1);
        }
    }

    for (size_t index = 0; index < view.get_size(); ++index)
    {
        ImGui::PushID(static_cast<int>(index));
        rttr::variant item = view.get_value(index).extract_wrapped_value();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (DrawValue(scene, item, cacheKey + "[" + std::to_string(index) + "]"))
        {
            changed |= view.set_value(index, item);
        }
        ImGui::PopID();
    }
    return changed;
}

bool InspectorPanel::DrawObject(Scene* scene, rttr::variant& value, const std::string& cacheKey)
{
    if (!ImGui::TreeNodeEx("##object", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", value.get_type().get_name().data()))
    {
        return false;
    }

    bool changed = false;
    const rttr::instance instance(value);
    for (const rttr::property& property : value.get_type().get_properties())
    {
        const std::string name = property.get_name().to_string();
        ImGui::PushID(name.c_str());
        ImGui::TextUnformatted(name.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);

        rttr::variant member = property.get_value(instance);
        if (DrawValue(scene, member, cacheKey + "." + name) && !property.is_readonly())
        {
            changed |= property.set_value(instance, member);
        }
        ImGui::PopID();
    }
    ImGui::TreePop();
    return changed;
}

bool InspectorPanel::DrawRotation(rttr::variant& value, const std::string& cacheKey)
{
    const Quaternion rotation = value.get_value<Quaternion>();
    EulerCache& cache = m_EulerCache[cacheKey];
    if (cache.rotation.X != rotation.X || cache.rotation.Y != rotation.Y
        || cache.rotation.Z != rotation.Z || cache.rotation.W != rotation.W)
    {
        cache.rotation = rotation;
        cache.euler = EditorUtils::QuaternionToEuler(rotation);
    }

    Vector3 euler = cache.euler;
    if (!ImGui::DragScalarN("##value", ImGuiDataType_Float, &euler.X, 3, 0.5f, nullptr, nullptr, "%.2f"))
    {
        return false;
    }

    cache.euler = euler;
    cache.rotation = EditorUtils::EulerToQuaternion(euler);
    value = cache.rotation;
    return true;
}

bool InspectorPanel::EditText(const char* label, std::string& value)
{
    const ImGuiID id = ImGui::GetID(label);
    std::string temporary = value;
    std::string& buffer = m_ActiveTextId == id ? m_ActiveTextBuffer : temporary;

    ImGui::InputText(label, &buffer);
    if (ImGui::IsItemActivated())
    {
        m_ActiveTextId = id;
        m_ActiveTextBuffer = buffer;
    }

    const bool committed = ImGui::IsItemDeactivatedAfterEdit();
    if (committed)
    {
        value = buffer;
    }
    if (ImGui::IsItemDeactivated() && m_ActiveTextId == id)
    {
        m_ActiveTextId = 0;
    }
    return committed;
}
