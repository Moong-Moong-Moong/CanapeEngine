#include "Scene/Serialization/SceneSerializer.h"
#include "Asset/AssetManager.h"
#include "CNDebug/DebugLog.h"
#include "Core/Application.h"
#include "Core/Reflection/Reflection.h"
#include "Scene/Component/CameraComponent.h"
#include "Scene/Component/MeshRendererComponent.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Scene.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Canape
{
	namespace
	{
		using Json = nlohmann::basic_json<nlohmann::ordered_map, std::vector, std::string, bool, std::int64_t, std::uint64_t, float>;

		constexpr int kSceneFormatVersion = 1;

		bool IsComponentPointer(const rttr::type& type)
		{
			return type.is_pointer() && type.get_raw_type().is_derived_from(rttr::type::get<Component>());
		}

		bool IsReferenceType(const rttr::type& type)
		{
			if (type == rttr::type::get<Handle<GameObject>>() || IsComponentPointer(type))
			{
				return true;
			}

			for (const rttr::type& argument : type.get_template_arguments())
			{
				if (IsReferenceType(argument))
				{
					return true;
				}
			}
			return false;
		}

		class SceneWriter
		{
		public:
			explicit SceneWriter(Scene& scene) : m_Scene(scene) {}

			Json Write()
			{
				const std::vector<GameObject*> gameObjects = CollectInHierarchyOrder();

				Json root;
				root["version"] = kSceneFormatVersion;
				root["name"] = m_Scene.GetName();

				CameraComponent* mainCamera = m_Scene.GetMainCamera();
				root["mainCamera"] = mainCamera ? mainCamera->GetId() : 0;
				root["materials"] = WriteMaterials(gameObjects);

				Json objects = Json::array();
				for (GameObject* gameObject : gameObjects)
				{
					objects.push_back(WriteGameObject(*gameObject));
				}
				root["gameObjects"] = std::move(objects);
				return root;
			}

		private:
			std::vector<GameObject*> CollectInHierarchyOrder()
			{
				std::vector<GameObject*> result;
				for (GameObject* gameObject : m_Scene.GetAllGameObjects())
				{
					if (!gameObject->IsPendingDestroy() && !gameObject->GetTransform()->GetParent())
					{
						AppendHierarchy(*gameObject, result);
					}
				}
				return result;
			}

			void AppendHierarchy(GameObject& gameObject, std::vector<GameObject*>& result)
			{
				result.push_back(&gameObject);
				for (TransformComponent* child : gameObject.GetTransform()->GetChildren())
				{
					GameObject* childObject = child->GetOwnerObject();
					if (childObject && !childObject->IsPendingDestroy())
					{
						AppendHierarchy(*childObject, result);
					}
				}
			}

			Json WriteMaterials(const std::vector<GameObject*>& gameObjects)
			{
				const AssetManager& assets = Application::Get().GetAssetManager();

				Json materials = Json::array();
				std::unordered_set<std::string> written;
				for (GameObject* gameObject : gameObjects)
				{
					for (MeshRendererComponent* renderer : gameObject->GetComponents<MeshRendererComponent>())
					{
						for (const std::string& name : renderer->GetMaterialNames())
						{
							if (name.empty() || MaterialFile::IsMaterialPath(name) || !written.insert(name).second)
							{
								continue;
							}

							const MaterialAsset* material = assets.FindMaterialByName(name);
							if (!material)
							{
								CN_LOG_RUNTIME("Scene", "씬 저장 경고 : 머티리얼을 찾을 수 없음, ", name);
								continue;
							}

							Json entry;
							entry["name"] = name;
							entry["baseColorTexture"] = AssetManager::ToAssetPath(assets.GetTexturePath(material->GetBaseColorTexture()));
							entry["desc"] = WriteValue(material->GetDesc());
							materials.push_back(std::move(entry));
						}
					}
				}
				return materials;
			}

			Json WriteGameObject(GameObject& gameObject)
			{
				Json object;
				object["id"] = gameObject.GetId();
				object["name"] = gameObject.GetName();
				object["active"] = gameObject.IsActive();

				Json components = Json::array();
				for (Component* component : gameObject.GetAllComponents())
				{
					const rttr::type type = rttr::type::get(*component);
					const bool creatable = type == rttr::type::get<TransformComponent>() || type.get_constructor().is_valid();
					if (!type.is_valid() || !creatable)
					{
						CN_LOG_RUNTIME("Scene", "씬 저장 경고 : 리플렉션 등록되지 않은 컴포넌트, ", gameObject.GetName());
						continue;
					}

					Json entry;
					entry["type"] = type.get_name().to_string();
					entry["id"] = component->GetId();

					Json properties = Json::object();
					const rttr::instance instance(component);
					for (const rttr::property& property : type.get_properties())
					{
						properties[property.get_name().to_string()] = WriteValue(property.get_value(instance));
					}
					entry["properties"] = std::move(properties);
					components.push_back(std::move(entry));
				}
				object["components"] = std::move(components);
				return object;
			}

			Json WriteValue(const rttr::variant& input)
			{
				const rttr::variant value = input.get_type().is_wrapper() ? input.extract_wrapped_value() : input;
				const rttr::type type = value.get_type();

				if (type == rttr::type::get<Handle<GameObject>>())
				{
					const GameObject* gameObject = m_Scene.GetGameObject(value.get_value<Handle<GameObject>>());
					return gameObject ? gameObject->GetId() : 0;
				}
				if (IsComponentPointer(type))
				{
					bool ok = false;
					const Component* component = value.convert<Component*>(&ok);
					return ok && component ? component->GetId() : 0;
				}
				if (type.is_enumeration())
				{
					return type.get_enumeration().value_to_name(value).to_string();
				}
				if (type == rttr::type::get<std::string>())
				{
					return value.get_value<std::string>();
				}
				if (type == rttr::type::get<bool>())
				{
					return value.get_value<bool>();
				}
				if (type == rttr::type::get<float>() || type == rttr::type::get<double>())
				{
					return value.to_float();
				}
				if (type.is_arithmetic())
				{
					if (type == rttr::type::get<uint8_t>() || type == rttr::type::get<uint16_t>() ||
						type == rttr::type::get<uint32_t>() || type == rttr::type::get<uint64_t>())
					{
						return value.to_uint64();
					}
					return value.to_int64();
				}
				if (value.is_sequential_container())
				{
					Json array = Json::array();
					for (const rttr::variant& item : value.create_sequential_view())
					{
						array.push_back(WriteValue(item));
					}
					return array;
				}
				if (type.is_class())
				{
					Json object = Json::object();
					for (const rttr::property& property : type.get_properties())
					{
						object[property.get_name().to_string()] = WriteValue(property.get_value(value));
					}
					return object;
				}
				return nullptr;
			}

			Scene& m_Scene;
		};

		class SceneReader
		{
		public:
			std::unique_ptr<Scene> Read(const Json& root, JoltRuntime* physicsRuntime)
			{
				auto scene = std::make_unique<Scene>(root.value("name", std::string("Scene")), physicsRuntime);
				m_Scene = scene.get();

				ReadMaterials(root.value("materials", Json::array()));

				struct PendingComponent
				{
					Component* component;
					Json properties;
				};
				std::vector<PendingComponent> pending;

				for (const Json& objectJson : root.value("gameObjects", Json::array()))
				{
					GameObject* gameObject = m_Scene->GetGameObject(m_Scene->AddGameObject(objectJson.value("name", std::string("GameObject"))));
					const uint64_t objectId = objectJson.value("id", uint64_t{ 0 });
					if (objectId != 0)
					{
						gameObject->SetId(objectId);
						m_Scene->ReserveId(objectId);
					}
					gameObject->SetActive(objectJson.value("active", true));
					m_Objects[gameObject->GetId()] = gameObject;

					for (const Json& componentJson : objectJson.value("components", Json::array()))
					{
						Component* component = CreateComponent(*gameObject, componentJson.value("type", std::string()));
						if (!component)
						{
							continue;
						}

						const uint64_t componentId = componentJson.value("id", uint64_t{ 0 });
						if (componentId != 0)
						{
							component->SetId(componentId);
							m_Scene->ReserveId(componentId);
						}
						m_Components[component->GetId()] = component;

						const auto properties = componentJson.find("properties");
						if (properties != componentJson.end() && properties->is_object())
						{
							ReadProperties(rttr::instance(component), *properties, false);
							pending.push_back({ component, *properties });
						}
					}
				}

				for (const PendingComponent& entry : pending)
				{
					ReadProperties(rttr::instance(entry.component), entry.properties, true);
				}

				const auto mainCamera = m_Components.find(root.value("mainCamera", uint64_t{ 0 }));
				if (mainCamera != m_Components.end())
				{
					m_Scene->SetMainCamera(dynamic_cast<CameraComponent*>(mainCamera->second));
				}

				return scene;
			}

		private:
			void ReadMaterials(const Json& materials)
			{
				AssetManager& assets = Application::Get().GetAssetManager();
				const AssetOwnerInfo owner{ m_Scene->GetName(), "SceneSerializer", "MaterialSetup" };

				for (const Json& entry : materials)
				{
					const std::string name = entry.value("name", std::string());
					if (name.empty() || assets.FindMaterialByName(name))
					{
						continue;
					}

					MaterialDesc desc;
					const auto descJson = entry.find("desc");
					if (descJson != entry.end() && descJson->is_object())
					{
						ReadProperties(rttr::instance(desc), *descJson, false);
					}

					TextureAssetHandle texture;
					const std::string texturePath = entry.value("baseColorTexture", std::string());
					if (!texturePath.empty())
					{
						texture = assets.GetTextureHandle(texturePath, owner);
					}

					assets.CreateMaterial(name, desc, texture, owner);
				}
			}

			Component* CreateComponent(GameObject& gameObject, const std::string& typeName)
			{
				const rttr::type type = rttr::type::get_by_name(typeName);
				if (type == rttr::type::get<TransformComponent>())
				{
					return gameObject.GetTransform();
				}

				if (!type.is_valid() || !type.is_derived_from<Component>())
				{
					CN_LOG_RUNTIME("Scene", "씬 로드 경고 : 알 수 없는 컴포넌트 타입, ", typeName);
					return nullptr;
				}

				rttr::variant created = type.create();
				bool ok = false;
				Component* component = created.is_valid() ? created.convert<Component*>(&ok) : nullptr;
				if (!ok || !component)
				{
					CN_LOG_RUNTIME("Scene", "씬 로드 경고 : 컴포넌트 생성 실패, ", typeName);
					if (created.is_valid())
					{
						type.destroy(created);
					}
					return nullptr;
				}

				return gameObject.AddComponent(std::unique_ptr<Component>(component));
			}

			void ReadProperties(rttr::instance instance, const Json& json, bool references)
			{
				for (const rttr::property& property : instance.get_derived_type().get_properties())
				{
					if (property.is_readonly() || IsReferenceType(property.get_type()) != references)
					{
						continue;
					}

					const auto found = json.find(property.get_name().to_string());
					if (found == json.end())
					{
						continue;
					}

					rttr::variant value = ReadValue(property.get_type(), *found, property.get_value(instance));
					if (value.is_valid())
					{
						property.set_value(instance, value);
					}
				}
			}

			rttr::variant ReadValue(const rttr::type& type, const Json& json, rttr::variant current)
			{
				if (type == rttr::type::get<Handle<GameObject>>())
				{
					const auto found = json.is_number() ? m_Objects.find(json.get<uint64_t>()) : m_Objects.end();
					return found != m_Objects.end() ? found->second->GetHandle() : Handle<GameObject>{};
				}
				if (IsComponentPointer(type))
				{
					const auto found = json.is_number() ? m_Components.find(json.get<uint64_t>()) : m_Components.end();
					if (found == m_Components.end())
					{
						return {};
					}

					rttr::variant pointer = found->second;
					return pointer.convert(type) ? pointer : rttr::variant();
				}
				if (type.is_enumeration())
				{
					return json.is_string() ? type.get_enumeration().name_to_value(json.get<std::string>()) : rttr::variant();
				}
				if (type == rttr::type::get<std::string>())
				{
					return json.is_string() ? rttr::variant(json.get<std::string>()) : rttr::variant();
				}
				if (type == rttr::type::get<bool>())
				{
					return json.is_boolean() ? rttr::variant(json.get<bool>()) : rttr::variant();
				}
				if (type.is_arithmetic())
				{
					if (!json.is_number())
					{
						return {};
					}

					rttr::variant number;
					if (json.is_number_unsigned())
					{
						number = json.get<uint64_t>();
					}
					else if (json.is_number_integer())
					{
						number = json.get<int64_t>();
					}
					else
					{
						number = json.get<float>();
					}
					return number.convert(type) ? number : rttr::variant();
				}
				if (current.is_sequential_container())
				{
					if (!json.is_array())
					{
						return {};
					}

					rttr::variant_sequential_view view = current.create_sequential_view();
					if (!view.set_size(json.size()))
					{
						return {};
					}

					for (std::size_t i = 0; i < json.size(); ++i)
					{
						rttr::variant element = ReadValue(view.get_value_type(), json[i], view.get_value(i).extract_wrapped_value());
						if (element.is_valid())
						{
							view.set_value(i, element);
						}
					}
					return current;
				}
				if (type.is_class() && json.is_object())
				{
					rttr::variant object = current.is_valid() ? current : type.create();
					if (object.is_valid())
					{
						ReadProperties(rttr::instance(object), json, false);
						ReadProperties(rttr::instance(object), json, true);
					}
					return object;
				}
				return {};
			}

			Scene* m_Scene = nullptr;
			std::unordered_map<uint64_t, GameObject*> m_Objects;
			std::unordered_map<uint64_t, Component*> m_Components;
		};
	}

	bool SceneSerializer::Save(Scene& scene, const std::string& path)
	{
		const Json root = SceneWriter(scene).Write();

		const std::filesystem::path filePath(path);
		if (filePath.has_parent_path())
		{
			std::error_code error;
			std::filesystem::create_directories(filePath.parent_path(), error);
		}

		std::ofstream file(filePath);
		if (!file)
		{
			CN_LOG_RUNTIME("Scene", "씬 저장 실패 : 파일을 열 수 없음, ", path);
			return false;
		}

		file << root.dump(4);
		CN_LOG_RUNTIME("Scene", "씬 저장 완료 : ", path);
		return true;
	}

	std::unique_ptr<Scene> SceneSerializer::Load(const std::string& path, JoltRuntime* physicsRuntime)
	{
		std::ifstream file(std::filesystem::path(path), std::ios::binary);
		if (!file)
		{
			CN_LOG_RUNTIME("Scene", "씬 로드 실패 : 파일을 열 수 없음, ", path);
			return nullptr;
		}

		const Json root = Json::parse(file, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			CN_LOG_RUNTIME("Scene", "씬 로드 실패 : JSON 파싱 오류, ", path);
			return nullptr;
		}

		if (root.value("version", 0) != kSceneFormatVersion)
		{
			CN_LOG_RUNTIME("Scene", "씬 로드 실패 : 지원하지 않는 버전, ", path);
			return nullptr;
		}

		std::unique_ptr<Scene> scene = SceneReader().Read(root, physicsRuntime);
		CN_LOG_RUNTIME("Scene", "씬 로드 완료 : ", path);
		return scene;
	}
}
