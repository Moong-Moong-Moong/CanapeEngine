#include "Scene/SceneManager.h"
#include "Scene/Serialization/SceneSerializer.h"

#include <algorithm>
#include <filesystem>
#include <system_error>

namespace Canape
{
	SceneManager::SceneManager(JoltRuntime* physicsRuntime)
		: m_PhysicsRuntime(physicsRuntime)
	{
	}

	SceneManager::~SceneManager()
	{
		m_activeScene = nullptr;
		m_PendingActiveScene = nullptr;
		m_Scenes.clear();
	}

	Scene* SceneManager::CreateScene(const std::string& name)
	{
		if (Scene* existing = GetScene(name))
		{
			return existing;
		}

		m_Scenes.push_back(std::make_unique<Scene>(name, m_PhysicsRuntime));
		Scene* created = m_Scenes.back().get();

		if (!m_activeScene)
		{
			m_activeScene = created;
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
		UnloadScene(GetScene(name));
	}

	void SceneManager::UnloadScene(Scene* scene)
	{
		if (Owns(scene))
		{
			m_PendingUnloadScenes.push_back(scene);
		}
	}

	void SceneManager::RegisterSceneFactory(const std::string& name, SceneFactory factory)
	{
		m_SceneFactories[name] = factory;
	}

	Scene* SceneManager::OpenScene(const std::string& name)
	{
		const auto found = m_SceneFactories.find(name);
		if (found != m_SceneFactories.end() && found->second)
		{
			return found->second(*this);
		}

		const std::string path = m_SceneDirectory + name + ".json";
		std::error_code error;
		if (!std::filesystem::exists(std::filesystem::path(path), error))
		{
			return nullptr;
		}
		return LoadSceneFromFile(path);
	}

	Scene* SceneManager::LoadSceneFromFile(const std::string& path)
	{
		std::unique_ptr<Scene> loaded = SceneSerializer::Load(path, m_PhysicsRuntime);
		if (!loaded)
		{
			return nullptr;
		}

		m_Scenes.push_back(std::move(loaded));
		Scene* scene = m_Scenes.back().get();
		if (!m_activeScene)
		{
			m_activeScene = scene;
		}
		SetActiveScene(scene);
		return scene;
	}

	bool SceneManager::SaveSceneToFile(Scene& scene, const std::string& path) const
	{
		return SceneSerializer::Save(scene, path);
	}

	void SceneManager::Update(bool runScripts)
	{
		if (m_activeScene)
		{
			if (runScripts)
			{
				m_activeScene->Update();
			}
			else
			{
				m_activeScene->BeginUpdate();
				m_activeScene->EndUpdate();
			}
		}

		ApplyPendingChanges();
	}

	void SceneManager::ApplyPendingChanges()
	{
		if (m_PendingActiveScene)
		{
			m_activeScene = m_PendingActiveScene;
			m_PendingActiveScene = nullptr;
		}

		for (Scene* scene : m_PendingUnloadScenes)
		{
			if (scene == m_activeScene)
			{
				m_activeScene = nullptr;
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
