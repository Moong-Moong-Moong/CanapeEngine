#pragma once

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace Canape
{
    // A default handle names nothing: generations start at 1 and skip 0 when they wrap.
    template<typename T>
    struct SlotHandle {
        std::uint32_t index = 0;
        std::uint32_t generation = 0;

        constexpr bool operator==(const SlotHandle&) const = default;
    };

    // Elements live in place, so Add may move them; a freed index is reused last in, first out.
    template<typename T>
    class SlotMap {
    public:
        using Handle = SlotHandle<T>;

    private:
        struct Slot {
            std::optional<T> value;
            std::uint32_t generation = 1;
        };

        std::vector<Slot> slots;
        std::vector<std::uint32_t> freeIndices;
        std::size_t count = 0;

    public:
        template<typename... Args>
        [[nodiscard]] Handle Add(Args&&... args) {
            std::uint32_t index = 0;
            if(freeIndices.empty()) {
                index = static_cast<std::uint32_t>(slots.size());
                slots.emplace_back();
            }
            else {
                index = freeIndices.back();
                freeIndices.pop_back();
            }
            Slot& slot = slots[index];
            slot.value.emplace(std::forward<Args>(args)...);
            ++count;
            return Handle{index, slot.generation};
        }

        void Remove(Handle handle) {
            if(Get(handle) == nullptr) {
                return;
            }
            Slot& slot = slots[handle.index];
            slot.value.reset();
            if(++slot.generation == 0) {
                slot.generation = 1;
            }
            freeIndices.push_back(handle.index);
            --count;
        }

        const T* Get(Handle handle) const {
            if(handle.index >= slots.size()) {
                return nullptr;
            }
            const Slot& slot = slots[handle.index];
            return slot.value && slot.generation == handle.generation ?
                &*slot.value :
                nullptr;
        }
        T* Get(Handle handle) {
            return const_cast<T*>(std::as_const(*this).Get(handle));
        }

        template<typename Func>
            requires std::predicate<Func&, const T&>
        std::vector<Handle> Finds(Func&& func) const {
            std::vector<Handle> found;
            for(std::uint32_t index = 0; index < slots.size(); ++index) {
                const Slot& slot = slots[index];
                if(slot.value && func(*slot.value)) {
                    found.push_back(Handle{index, slot.generation});
                }
            }
            return found;
        }

        template<typename Func>
            requires std::invocable<Func&, const T&>
        void ForEach(Func&& func) const {
            for(const Slot& slot : slots) {
                if(slot.value) {
                    func(*slot.value);
                }
            }
        }

        Handle HandleAt(std::uint32_t index) const {
            assert(index < slots.size() && slots[index].value);
            return Handle{index, slots[index].generation};
        }

        std::size_t GetCount() const { return count; }
    };
}
