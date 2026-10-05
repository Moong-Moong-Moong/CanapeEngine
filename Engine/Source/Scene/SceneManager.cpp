#include "Scene/SceneManager.h"

#include <algorithm>

namespace Canape
{
	SceneManager::~SceneManager()
	{
		m_ActiveScene = nullptr;
		m_PendingActiveScene = nullptr;
		m_Scenes.clear();
	}

	Scene* SceneManager::CreateScene(const std::string& name)
	{
		if (Scene* existing = GetScene(name))
		{
			return existing;
		}

		m_Scenes.push_back(std::make_unique<Scene>(name));
		Scene* created = m_Scenes.back().get();

		if (!m_ActiveScene)
		{
			m_ActiveScene = created;
		}
		return created;
	}

	Scene* SceneManager::GetScene(std::string_view name) const
	{
		for (const auto& scene : m_Scenes)
		{
			if (scene->GetName() == name)
			{
				return scene.get();
			}
		}
		return nullptr;
	}

	void SceneManager::SetActiveScene(Scene* scene)
	{
		if (Owns(scene))
		{
			m_PendingActiveScene = scene;
		}
	}

	void SceneManager::LoadScene(std::string_view name)
	{
		SetActiveScene(GetScene(name));
	}

	void SceneManager::UnloadScene(std::string_view name)
	{
		if (Scene* scene = GetScene(name))
		{
			m_PendingUnloadScenes.push_back(scene);
		}
	}

	void SceneManager::Update()
	{
		if (m_ActiveScene)
		{
			m_ActiveScene->Update();
			m_ActiveScene->LateUpdate();
			m_ActiveScene->FlushDestroyed();
		}

		ApplyPendingChanges();
	}

	void SceneManager::ApplyPendingChanges()
	{
		if (m_PendingActiveScene)
		{
			m_ActiveScene = m_PendingActiveScene;
			m_PendingActiveScene = nullptr;
		}

		for (Scene* scene : m_PendingUnloadScenes)
		{
			if (scene == m_ActiveScene)
			{
				m_ActiveScene = nullptr;
			}

			std::erase_if(m_Scenes, [scene](const std::unique_ptr<Scene>& owned)
			{
				return owned.get() == scene;
			});
		}
		m_PendingUnloadScenes.clear();
	}

	bool SceneManager::Owns(const Scene* scene) const
	{
		return scene && std::any_of(m_Scenes.begin(), m_Scenes.end(), [scene](const std::unique_ptr<Scene>& owned)
		{
			return owned.get() == scene;
		});
	}
}
