#pragma once

#include <vector>
#include <memory>
#include <queue>

template<typename T>		
struct Handle
{
	int index = -1;
	int gen = -1;
};																		

template<typename T>
class SlotMap
{
public:
	Handle<T> Add();
	void Remove(Handle<T> handle);
	T* Get(Handle<T> handle);

	template<typename Func>
	Handle<T> Find(Func&& func);

	template<typename Func>
	std::vector<Handle<T>> Finds(Func&& func);

	std::vector<T*> GetAll();

private:
	struct Slot 
	{
		std::unique_ptr<T> pElement;
		int gen = 0;
	};

	std::vector<Slot> m_slots;
	std::vector<int> m_freeIndices;

	int head = -1;
};

template <typename T>
Handle<T> SlotMap<T>::Add()
{
	std::unique_ptr<T> pElement = std::make_unique<T>();
	Handle<T> handle;

	// 빈 슬롯이 있는 경우
	if (m_freeIndices.empty() == false)
	{
		int index = m_freeIndices.back();
		m_freeIndices.pop_back();
		m_slots[index].pElement = std::move(pElement);
		m_slots[index].gen += 1;

		handle.index = index;
		handle.gen = m_slots[index].gen;
	}

	// 빈 슬롯이 없는 경우
	else
	{
		Slot newSlot = {
			std::move(pElement),
			0
		};

		m_slots.push_back(std::move(newSlot));

		handle.index = m_slots.size() - 1;
		handle.gen = 0;
	}

	return handle;
}

template <typename T>
void SlotMap<T>::Remove(Handle<T> handle)
{
	if (handle.index < 0 ||
		static_cast<std::size_t>(handle.index) >= m_slots.size())
	{
		return;
	}

	int index = handle.index;
	int gen = handle.gen;

	if (m_slots[index].gen != gen)
		return;

	m_slots[index].pElement.reset();
	m_slots[index].pElement = nullptr;
	m_slots[index].gen += 1;

	m_freeIndices.push_back(index);
}

template <typename T>
T* SlotMap<T>::Get(Handle<T> handle)
{
	if (handle.index < 0 ||
		static_cast<std::size_t>(handle.index) >= m_slots.size())
	{
		return nullptr;
	}

	int index = handle.index;
	int gen = handle.gen;

	if (m_slots[index].gen != gen)
		return nullptr;

	return m_slots[index].pElement.get();
}

template <typename T>
std::vector<T*> SlotMap<T>::GetAll()
{
	std::vector<T*> elements;

	for (Slot& slot : m_slots)
	{
		if (slot.pElement)
		{
			elements.push_back(slot.pElement.get());
		}
	}

	return elements;
}


template <typename T>
template <typename Func>
Handle<T> SlotMap<T>::Find(Func&& func)
{
	for (std::size_t i = 0; i < m_slots.size(); i++)
	{
		T* t = m_slots[i].pElement.get();

		if (t && func(t))
		{
			return Handle<T>{
				static_cast<int>(i),
				m_slots[i].gen
			};
		}
	}

	return Handle<T>{};
}

template <typename T>
template <typename Func>
std::vector<Handle<T>> SlotMap<T>::Finds(Func&& func)
{
	std::vector<Handle<T>> handles;

	for (std::size_t i = 0; i < m_slots.size(); i++)
	{
		T* t = m_slots[i].pElement.get();

		if (t && func(t))
		{
			handles.push_back(Handle<T>{
				static_cast<int>(i),
				m_slots[i].gen
			});
		}
	}

	return handles;
}

