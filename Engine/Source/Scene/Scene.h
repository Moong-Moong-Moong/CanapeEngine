#pragma once

#include "Scene/GameObject/GameObjectManager.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Canape
{
	class GameObject;
	class CameraComponent;
	class AssetManager;
	class Renderer;
	struct RenderScene;

	class Scene final
	{
	public:
		explicit Scene(std::string name);
		~Scene();

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		const std::string& GetName() const { return m_Name; }

		void UpdateGameObjects();
		void BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer);

		//***** GameObject
		//*** Update, Find 는 다음 프레임부터 활성화됩니다.
		//*** 리턴 받은 Handle 로 GameObject* 접근과 내부 데이터 수정은 바로 가능합니다.
		Handle<GameObject> DeferAddGameObject(const std::string& name = "GameObject");
		//*** 다음 프레임부터 모든 데이터 삭제, 기능이 비활성화 됩니다.
		void DeferRemoveGameObject(const Handle<GameObject> handle);
		GameObject* GetGameObject(const Handle<GameObject> handle);
		Handle<GameObject> FindGameObjectByName(const std::string& name);

		//***** Camera
		CameraComponent* GetMainCamera() ;
		void SetMainCamera(CameraComponent* camera);

	private:
		//***** GameObject
		Handle<GameObject> AddGameObject(const std::string& name = "GameObject");
		void RemoveGameObject(const Handle<GameObject> handle);

	private:
		friend class SceneManager;

		std::string m_Name;
		GameObjectManager m_gameObjects;
		Handle<GameObject> m_MainCamera;
	};
}
