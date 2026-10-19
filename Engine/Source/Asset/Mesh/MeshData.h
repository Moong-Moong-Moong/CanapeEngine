#pragma once

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

    struct SubMesh
    {
        uint32_t startIndex = 0;
        uint32_t indexCount = 0;
        int32_t baseVertex = 0;
        uint32_t materialSlot = 0; 
    };

    static_assert(std::is_standard_layout_v<StaticVertex>);
    static_assert(sizeof(StaticVertex) == 32);
    static_assert(offsetof(StaticVertex, Position) == 0);
    static_assert(offsetof(StaticVertex, Normal) == 12);
    static_assert(offsetof(StaticVertex, UV) == 24);

    inline const char* ValidateMeshIndices(size_t vertexCount, std::span<const uint32_t> indices,
        std::span<const SubMesh> submeshes)
    {
        if (vertexCount > UINT32_MAX || indices.size() > UINT32_MAX || submeshes.size() > UINT32_MAX)
            return "Mesh exceeds 32-bit vertex/index/submesh limits";
        if (vertexCount == 0 || indices.empty())
            return "Mesh requires non-empty vertex and index data";
        if (indices.size() % 3 != 0)
            return "Mesh indices must contain complete triangles";

        const SubMesh whole{ 0, static_cast<uint32_t>(indices.size()), 0, 0 };
        if (submeshes.empty()) submeshes = std::span<const SubMesh>(&whole, 1);
        for (const SubMesh& part : submeshes)
        {
            const uint64_t end = uint64_t(part.startIndex) + part.indexCount;
            if (part.indexCount == 0 || part.indexCount % 3 != 0 || end > indices.size())
                return "SubMesh has an invalid triangle/index range";
            for (uint64_t i = part.startIndex; i < end; ++i)
            {
                const int64_t vertex = int64_t(indices[static_cast<size_t>(i)]) + part.baseVertex;
                if (vertex < 0 || uint64_t(vertex) >= vertexCount)
                    return "SubMesh index plus baseVertex is outside the vertex buffer";
            }
        }
        return nullptr;
    }
}
