#pragma once

#include "Math/Vector2.h"
#include "Math/Vector3.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>

namespace Canape
{

    enum class VertexFormat { Position, Static };

    struct StaticVertex
    {
        Vector3 Position{};
        Vector3 Normal{}; 
        Vector2 UV{};
    };

    struct DX11SubMesh
    {
        uint32_t startIndex = 0;   // 인덱스 버퍼에서 이 구간이 시작하는 위치
        uint32_t indexCount = 0;   // 이 구간의 인덱스 개수 (삼각형이므로 3의 배수 )
        int32_t baseVertex = 0;    // 여러 메쉬들을 한 메쉬 에셋에 저장하고 싶을때, 버텍스 버퍼를 공유하고 싶을 때 시작점을 등록
        uint32_t materialSlot = 0; // 사용할 머터리얼 번호
    };

    static_assert(std::is_standard_layout_v<StaticVertex>);
    static_assert(sizeof(StaticVertex) == 32);
    static_assert(offsetof(StaticVertex, Position) == 0);
    static_assert(offsetof(StaticVertex, Normal) == 12);
    static_assert(offsetof(StaticVertex, UV) == 24);

    inline bool ValidateMeshIndices(size_t vertexCount, std::span<const uint32_t> indices,
        std::span<const DX11SubMesh> submeshes)
    {
        if (vertexCount > UINT32_MAX || indices.size() > UINT32_MAX || submeshes.size() > UINT32_MAX)
            return false;
        if (vertexCount == 0 || indices.empty())
            return false;
        if (indices.size() % 3 != 0)
            return false;

        const DX11SubMesh whole{ 0, static_cast<uint32_t>(indices.size()), 0, 0 };
        if (submeshes.empty()) submeshes = std::span<const DX11SubMesh>(&whole, 1);
        for (const DX11SubMesh& part : submeshes)
        {
            const uint64_t end = uint64_t(part.startIndex) + part.indexCount;
            if (part.indexCount == 0 || part.indexCount % 3 != 0 || end > indices.size())
                return false;
            for (uint64_t i = part.startIndex; i < end; ++i)
            {
                const int64_t vertex = int64_t(indices[static_cast<size_t>(i)]) + part.baseVertex;
                if (vertex < 0 || uint64_t(vertex) >= vertexCount)
                    return false;
            }
        }
        return true;
    }
}
