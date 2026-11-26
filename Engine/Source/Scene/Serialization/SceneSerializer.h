#pragma once

#include <memory>
#include <string>

namespace Canape
{
	class GameObject;
	class JoltRuntime;
	class Scene;
	class Session;
	class TransformComponent;

	class SceneSerializer final
	{
	public:
		static bool Save(Scene& scene, const std::string& path);
		static std::string SaveToString(Scene& scene);
		static std::unique_ptr<Scene> Load(const std::string& path, JoltRuntime* physicsRuntime = nullptr, Session* simulationSession = nullptr);
		// Same as Load, from text produced by SaveToString (editor undo). Object ids are kept.
		static std::unique_ptr<Scene> LoadFromString(const std::string& text, JoltRuntime* physicsRuntime = nullptr, Session* simulationSession = nullptr);

		static bool SavePrefab(GameObject& root, const std::string& path);
		static GameObject* InstantiatePrefab(Scene& scene, const std::string& path, TransformComponent* parent = nullptr);

		// Same format as a prefab, kept in memory (editor copy / paste / duplicate).
		// References to objects outside the copied tree are cleared in the instantiated copy.
		static std::string SaveTreeToString(GameObject& root);
		static GameObject* InstantiateFromString(Scene& scene, const std::string& text, TransformComponent* parent = nullptr);
	};
}
