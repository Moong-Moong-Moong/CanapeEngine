#pragma once

#include "Scene/GameObject/GameObjectHandle.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Canape
{
	class GameObject;
	class Scene;

	class GameObjectManager
	{
	public:
		GameObjectManager() = default;
		~GameObjectManager();

		GameObjectManager(const GameObjectManager&) = delete;
		GameObjectManager& operator=(const GameObjectManager&) = delete;

		GameObjectHandle Create(Scene* scene, const std::string& name);
		GameObject* Get(const GameObjectHandle& handle) const;
		bool IsAlive(const GameObjectHandle& handle) const;
		bool Destroy(const GameObjectHandle& handle);
		void Clear();

		std::size_t GetCount() const { return m_AliveCount; }

		template<typename Func>
		void ForEach(Func&& func) const
		{
			for (std::size_t i = 0; i < m_Slots.size(); ++i)
			{
				if (GameObject* object = m_Slots[i].Object.get())
				{
					func(object);
				}
			}
		}

	private:
		struct Slot
		{
			std::unique_ptr<GameObject> Object;
			std::uint32_t Generation = 0;
		};

		std::vector<Slot> m_Slots;
		std::vector<std::uint32_t> m_FreeIndices;
		std::size_t m_AliveCount = 0;
	};
}
