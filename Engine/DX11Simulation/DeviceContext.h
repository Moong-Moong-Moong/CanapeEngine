#pragma once

#include <cstdint>

#include "DX11Simulation/D3D.h"

namespace Canape
{
    class Buffer;
    class ComputeShader;
    class Texture;

    class DeviceContext
    {
    private:
        DeviceContextRef context = nullptr;
        // Issued since construction.
        std::uint64_t dispatchCount = 0;
        std::uint64_t drawCount = 0;

    public:
        explicit DeviceContext(ID3D11DeviceContext&);
        ~DeviceContext() = default;

        DeviceContext(const DeviceContext&) = delete;
        DeviceContext& operator=(const DeviceContext&) = delete;
        DeviceContext(DeviceContext&&) = delete;
        DeviceContext& operator=(DeviceContext&&) = delete;

        void SetComputeShader(const ComputeShader&);
        void ClearVertexShader();
        void ClearPixelShader();
        void ClearComputeShader();

        void SetVertexConstant(UINT slot, const Buffer&);
        void SetPixelConstant(UINT slot, const Buffer&);
        void SetComputeConstant(UINT slot, const Buffer&);
        void ClearVertexConstant(UINT slot);
        void ClearPixelConstant(UINT slot);
        void ClearComputeConstant(UINT slot);

        void SetVertexReadable(UINT slot, const Buffer&);
        void SetVertexReadable(UINT slot, const Texture&);
        void SetPixelReadable(UINT slot, const Buffer&);
        void SetPixelReadable(UINT slot, const Texture&);
        void SetComputeReadable(UINT slot, const Buffer&);
        void SetComputeReadable(UINT slot, const Texture&);
        void ClearVertexReadable(UINT slot);
        void ClearPixelReadable(UINT slot);
        void ClearComputeReadable(UINT slot);

        void SetComputeWritable(UINT slot, Buffer&);
        void SetComputeWritable(UINT slot, Texture&);
        void ClearComputeWritable(UINT slot);

        void ClearVertexSampler(UINT slot);
        void ClearPixelSampler(UINT slot);
        void ClearComputeSampler(UINT slot);

        void Dispatch(UINT groupCountX, UINT groupCountY);
        void Draw(UINT vertexCount, UINT instanceCount);

        std::uint64_t GetDispatchCount() const
        {
            return dispatchCount;
        }
        std::uint64_t GetDrawCount() const
        {
            return drawCount;
        }

        ID3D11DeviceContext& GetRef()
        {
            return *context.Get();
        }
    };
}
