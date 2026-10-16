#pragma once

#include <cstdint>
#include <functional>

namespace Canape
{
    class MeshAsset;
    class MaterialAsset;
    class TextureAsset;

    template <typename Tag>
    struct AssetId
    {
        uint64_t value = 0;

        bool IsValid() const { return value != 0; }

        // 컴파일러가 멤버 변수들을 비교하는 코드를 자동으로 생성한다.
        //constexpr bool operator==(const AssetId&) const = default;

        bool operator==(const AssetId& other) const
        {
            return other.value == value;
        }

        bool operator!=(const AssetId& other) const
        {
            return other.value != value;
        }
    };

    using MeshAssetId = AssetId<MeshAsset>;
    using MaterialAssetId = AssetId<MaterialAsset>;
    using TextureAssetId = AssetId<TextureAsset>;

    // Hash for unordered_map keys: std::unordered_map<MeshAssetId, V, AssetIdHash<MeshAsset>>.
    template <typename T>
    struct AssetIdHash
    {
        size_t operator()(const AssetId<T>& id) const noexcept
        {
            return std::hash<uint64_t>{}(id.value);
        }
    };
}
