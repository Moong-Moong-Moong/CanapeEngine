#pragma once

#include <memory>
#include <string>

namespace Canape
{
	class GameObject;
	class JoltRuntime;
	class Scene;
	class TransformComponent;

	class SceneSerializer final
	{
	public:
		static bool Save(Scene& scene, const std::string& path);
		static std::unique_ptr<Scene> Load(const std::string& path, JoltRuntime* physicsRuntime = nullptr);

		static bool SavePrefab(GameObject& root, const std::string& path);
		static GameObject* InstantiatePrefab(Scene& scene, const std::string& path, TransformComponent* parent = nullptr);
	};
}
