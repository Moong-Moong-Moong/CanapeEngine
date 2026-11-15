#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "DX11Simulation/SlotHandle.h"

namespace Canape
{
    // Elements live in place, so Add may move them; a freed index is reused last in, first out.
    template<typename T>
    class SlotMap
    {
    public:
        using Handle = SlotHandle<T>;

    private:
        struct Slot
        {
            std::optional<T> value;
            std::uint32_t generation = 1;
        };

    private:
        std::vector<Slot> slots;
        std::vector<std::uint32_t> freeIndices;

    public:
        // An element whose constructor throws takes no slot.
        template<typename... Args>
        [[nodiscard]] Handle Add(Args&&... args)
        {
            if(freeIndices.empty())
            {
                slots.emplace_back();
                freeIndices.push_back(static_cast<std::uint32_t>(slots.size() - 1));
            }

            const auto index = freeIndices.back();
            auto& slot = slots[index];
            slot.value.emplace(std::forward<Args>(args)...);
            freeIndices.pop_back();

            return Handle { index, slot.generation };
        }

        void Remove(Handle handle)
        {
            if(Get(handle) == nullptr) return;

            auto& slot = slots[handle.index];
            slot.value.reset();
            if(++slot.generation == 0)
            {
                slot.generation = 1;
            }
            freeIndices.push_back(handle.index);
        }

        // nullptr for a handle that names nothing or something removed.
        const T* Get(Handle handle) const
        {
            if(handle.index >= slots.size()) return nullptr;

            const auto& slot = slots[handle.index];

            return slot.value && slot.generation == handle.generation ?
                std::addressof(*slot.value) :
                nullptr;
        }
        T* Get(Handle handle)
        {
            return const_cast<T*>(std::as_const(*this).Get(handle));
        }
    };
}
