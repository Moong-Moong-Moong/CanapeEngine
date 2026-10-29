#pragma once

#include <cstdint>
#include <functional>

namespace Canape
{
    struct DX11MeshResource;
    struct DX11MaterialResource;
    struct DX11TextureResource;

    template <typename Tag>
    struct ResourceId
    {
        uint64_t value = 0;

        bool IsValid() const { return value != 0; }

        bool operator==(const ResourceId& other) const
        {
            return other.value == value;
        }

        bool operator!=(const ResourceId& other) const
        {
            return other.value != value;
        }
    };

    using MeshResourceId = ResourceId<DX11MeshResource>;
    using MaterialResourceId = ResourceId<DX11MaterialResource>;
    using TextureResourceId = ResourceId<DX11TextureResource>;

    template <typename T>
    struct ResourceHash
    {
        size_t operator()(const ResourceId<T>& id) const noexcept
        {
            return std::hash<uint64_t>{}(id.value);
        }
    };
}
