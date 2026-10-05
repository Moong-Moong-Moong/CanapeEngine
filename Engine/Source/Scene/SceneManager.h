#pragma once

#include "Scene/Scene.h"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Canape
{
	class SceneManager final
	{
	public:
		SceneManager() = default;
		~SceneManager();

		SceneManager(const SceneManager&) = delete;
		SceneManager& operator=(const SceneManager&) = delete;

		Scene* CreateScene(const std::string& name);
		Scene* GetScene(std::string_view name) const;
		Scene* GetActiveScene() const { return m_ActiveScene; }
		std::size_t GetSceneCount() const { return m_Scenes.size(); }

		void SetActiveScene(Scene* scene);
		void LoadScene(std::string_view name);
		void UnloadScene(std::string_view name);

	private:
		friend class Application;

		void Update();
		void ApplyPendingChanges();
		bool Owns(const Scene* scene) const;

		std::vector<std::unique_ptr<Scene>> m_Scenes;
		Scene* m_ActiveScene = nullptr;
		Scene* m_PendingActiveScene = nullptr;
		std::vector<Scene*> m_PendingUnloadScenes;
	};
}
