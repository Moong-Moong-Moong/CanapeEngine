#pragma once

#include <functional>
#include <unordered_map>

#include "DX11Simulation/SlotMap.h"

namespace Canape
{
    template<typename Desc, typename T, typename Hash = std::hash<Desc>, typename Equal = std::equal_to<Desc>>
    class FlyweightPool
    {
    public:
        using Handle = SlotHandle<T>;

    private:
        SlotMap<T> slots;
        std::unordered_map<Desc, Handle, Hash, Equal> handles;

    public:
        template<typename Create>
        [[nodiscard]] Handle GetOrCreate(const Desc& desc, Create&& create)
        {
            if(const auto kept = handles.find(desc); kept != handles.end())
            {
                return kept->second;
            }

            const auto handle = slots.Add(create());
            handles.emplace(desc, handle);

            return handle;
        }

        T* Find(Handle handle)
        {
            return slots.Find(handle);
        }
    };
}
