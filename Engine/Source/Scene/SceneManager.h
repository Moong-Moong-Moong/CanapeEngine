#pragma once

#include "Scene/Scene.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Canape
{
	class JoltRuntime;

	class SceneManager final
	{
	public:
		using SceneFactory = Scene* (*)(SceneManager&);

		// Scenes created by this manager get a physics world when a runtime is given.
		explicit SceneManager(JoltRuntime* physicsRuntime = nullptr);
		~SceneManager();

		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		Scene* CreateScene(const std::string& name);
		Scene* GetScene(std::string_view name) const;
		Scene* GetActiveScene() const { return m_activeScene; }
		std::size_t GetSceneCount() const { return m_Scenes.size(); }
		const std::vector<std::unique_ptr<Scene>>& GetScenes() const { return m_Scenes; }

		void SetActiveScene(Scene* scene);
		void LoadScene(std::string_view name);
		void UnloadScene(std::string_view name);
		void UnloadScene(Scene* scene);

		void RegisterSceneFactory(const std::string& name, SceneFactory factory);
		Scene* OpenScene(const std::string& name);

		Scene* LoadSceneFromFile(const std::string& path);
		bool SaveSceneToFile(Scene& scene, const std::string& path) const;

		void SetSceneDirectory(std::string directory) { m_SceneDirectory = std::move(directory); }
		const std::string& GetSceneDirectory() const { return m_SceneDirectory; }

	private:
		friend class Application;

		void Update(bool runScripts);
		void ApplyPendingChanges();
		bool Owns(const Scene* scene) const;

		JoltRuntime* m_PhysicsRuntime = nullptr;
		std::vector<std::unique_ptr<Scene>> m_Scenes;
		Scene* m_activeScene = nullptr;
		Scene* m_PendingActiveScene = nullptr;
		std::vector<Scene*> m_PendingUnloadScenes;
		std::unordered_map<std::string, SceneFactory> m_SceneFactories;
		std::string m_SceneDirectory = "Content/Scenes/";
	};
}
