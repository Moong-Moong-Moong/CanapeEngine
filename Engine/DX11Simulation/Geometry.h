#pragma once

#include <cstdint>

#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/Handles.h"

namespace Canape
{
    class Session;

    // There is no vertex buffer: the vertex shader makes the vertices from SV_VertexID,
    // SV_InstanceID and the geometry's values.
    class Geometry
    {
    private:
        BufferHandle constants;
        // Per instance.
        std::uint32_t vertexCount = 0;
        std::uint32_t instanceCount = 1;

    public:
        Geometry(Session&, ConstantView, std::uint32_t vertexCount);

        void Upload(Session&, ConstantView);

        BufferHandle GetConstants() const
        {
            return constants;
        }

        std::uint32_t GetVertexCount() const
        {
            return vertexCount;
        }

        void SetVertexCount(std::uint32_t count)
        {
            vertexCount = count;
        }

        std::uint32_t GetInstanceCount() const
        {
            return instanceCount;
        }
    };
}
