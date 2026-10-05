#include "Scene/GameObject/GameObjectManager.h"
#include "Scene/GameObject/GameObject.h"

namespace Canape
{
	GameObjectManager::~GameObjectManager()
	{
		Clear();
	}

	GameObjectHandle GameObjectManager::Create(Scene* scene, const std::string& name)
	{
		std::uint32_t index = 0;
		if (!m_FreeIndices.empty())
		{
			index = m_FreeIndices.back();
			m_FreeIndices.pop_back();
		}
		else
		{
			index = static_cast<std::uint32_t>(m_Slots.size());
			m_Slots.emplace_back();
		}

		const GameObjectHandle handle{ index, m_Slots[index].Generation };
		m_Slots[index].Object = std::unique_ptr<GameObject>(new GameObject(scene, name, handle));
		++m_AliveCount;
		return handle;
	}

	GameObject* GameObjectManager::Get(const GameObjectHandle& handle) const
	{
		if (!IsAlive(handle))
		{
			return nullptr;
		}

		return m_Slots[handle.Index].Object.get();
	}

	bool GameObjectManager::IsAlive(const GameObjectHandle& handle) const
	{
		if (!handle.IsValid() || handle.Index >= m_Slots.size())
		{
			return false;
		}

		const Slot& slot = m_Slots[handle.Index];
		return slot.Object != nullptr && slot.Generation == handle.Generation;
	}

	bool GameObjectManager::Destroy(const GameObjectHandle& handle)
	{
		if (!IsAlive(handle))
		{
			return false;
		}

		std::unique_ptr<GameObject> destroyed = std::move(m_Slots[handle.Index].Object);
		++m_Slots[handle.Index].Generation;
		m_FreeIndices.push_back(handle.Index);
		--m_AliveCount;

		destroyed.reset();
		return true;
	}

	void GameObjectManager::Clear()
	{
		std::vector<std::unique_ptr<GameObject>> destroyed;
		destroyed.reserve(m_AliveCount);

		for (std::uint32_t i = 0; i < m_Slots.size(); ++i)
		{
			Slot& slot = m_Slots[i];
			if (slot.Object)
			{
				destroyed.push_back(std::move(slot.Object));
				++slot.Generation;
				m_FreeIndices.push_back(i);
			}
		}
		m_AliveCount = 0;

		destroyed.clear();
	}
}
