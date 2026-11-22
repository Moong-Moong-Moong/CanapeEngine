#include "DX11Simulation/DeviceContext.h"

#include <cassert>

#include "DX11Simulation/Buffer.h"
#include "DX11Simulation/Shader.h"
#include "DX11Simulation/Texture.h"

namespace Canape
{
    DeviceContext::DeviceContext(ID3D11DeviceContext& context)
        : context(&context) {}

    void DeviceContext::SetComputeShader(const ComputeShader& shader)
    {
        context->CSSetShader(shader.Get(), nullptr, 0);
    }

    void DeviceContext::ClearVertexShader()
    {
        context->VSSetShader(nullptr, nullptr, 0);
    }

    void DeviceContext::ClearPixelShader()
    {
        context->PSSetShader(nullptr, nullptr, 0);
    }

    void DeviceContext::ClearComputeShader()
    {
        context->CSSetShader(nullptr, nullptr, 0);
    }

    void DeviceContext::SetVertexConstant(UINT slot, const Buffer& buffer)
    {
        ID3D11Buffer* buffers[] = {
            buffer.Get()
        };
        context->VSSetConstantBuffers(slot, 1, buffers);
    }

    void DeviceContext::SetPixelConstant(UINT slot, const Buffer& buffer)
    {
        ID3D11Buffer* buffers[] = {
            buffer.Get()
        };
        context->PSSetConstantBuffers(slot, 1, buffers);
    }

    void DeviceContext::SetComputeConstant(UINT slot, const Buffer& buffer)
    {
        ID3D11Buffer* buffers[] = {
            buffer.Get()
        };
        context->CSSetConstantBuffers(slot, 1, buffers);
    }

    void DeviceContext::ClearVertexConstant(UINT slot)
    {
        ID3D11Buffer* buffers[] = {
            nullptr
        };
        context->VSSetConstantBuffers(slot, 1, buffers);
    }

    void DeviceContext::ClearPixelConstant(UINT slot)
    {
        ID3D11Buffer* buffers[] = {
            nullptr
        };
        context->PSSetConstantBuffers(slot, 1, buffers);
    }

    void DeviceContext::ClearComputeConstant(UINT slot)
    {
        ID3D11Buffer* buffers[] = {
            nullptr
        };
        context->CSSetConstantBuffers(slot, 1, buffers);
    }

    void DeviceContext::SetVertexReadable(UINT slot, const Buffer& buffer)
    {
        ID3D11ShaderResourceView* views[] = {
            buffer.GetSRV()
        };
        assert(views[0] != nullptr);

        context->VSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::SetVertexReadable(UINT slot, const Texture& texture)
    {
        ID3D11ShaderResourceView* views[] = {
            texture.GetSRV()
        };
        assert(views[0] != nullptr);

        context->VSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::SetPixelReadable(UINT slot, const Buffer& buffer)
    {
        ID3D11ShaderResourceView* views[] = {
            buffer.GetSRV()
        };
        assert(views[0] != nullptr);

        context->PSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::SetPixelReadable(UINT slot, const Texture& texture)
    {
        ID3D11ShaderResourceView* views[] = {
            texture.GetSRV()
        };
        assert(views[0] != nullptr);

        context->PSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::SetComputeReadable(UINT slot, const Buffer& buffer)
    {
        ID3D11ShaderResourceView* views[] = {
            buffer.GetSRV()
        };
        assert(views[0] != nullptr);

        context->CSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::SetComputeReadable(UINT slot, const Texture& texture)
    {
        ID3D11ShaderResourceView* views[] = {
            texture.GetSRV()
        };
        assert(views[0] != nullptr);

        context->CSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::ClearVertexReadable(UINT slot)
    {
        ID3D11ShaderResourceView* views[] = {
            nullptr
        };

        context->VSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::ClearPixelReadable(UINT slot)
    {
        ID3D11ShaderResourceView* views[] = {
            nullptr
        };

        context->PSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::ClearComputeReadable(UINT slot)
    {
        ID3D11ShaderResourceView* views[] = {
            nullptr
        };

        context->CSSetShaderResources(slot, 1, views);
    }

    void DeviceContext::SetComputeWritable(UINT slot, Buffer& buffer)
    {
        ID3D11UnorderedAccessView* views[] = {
            buffer.GetUAV()
        };
        assert(views[0] != nullptr);

        context->CSSetUnorderedAccessViews(slot, 1, views, nullptr);
    }

    void DeviceContext::SetComputeWritable(UINT slot, Texture& texture)
    {
        ID3D11UnorderedAccessView* views[] = {
            texture.GetUAV()
        };
        assert(views[0] != nullptr);

        context->CSSetUnorderedAccessViews(slot, 1, views, nullptr);
    }

    void DeviceContext::ClearComputeWritable(UINT slot)
    {
        ID3D11UnorderedAccessView* views[] = {
            nullptr
        };

        context->CSSetUnorderedAccessViews(slot, 1, views, nullptr);
    }

    void DeviceContext::ClearVertexSampler(UINT slot)
    {
        ID3D11SamplerState* states[] = {
            nullptr
        };

        context->VSSetSamplers(slot, 1, states);
    }

    void DeviceContext::ClearPixelSampler(UINT slot)
    {
        ID3D11SamplerState* states[] = {
            nullptr
        };

        context->PSSetSamplers(slot, 1, states);
    }

    void DeviceContext::ClearComputeSampler(UINT slot)
    {
        ID3D11SamplerState* states[] = {
            nullptr
        };

        context->CSSetSamplers(slot, 1, states);
    }

    void DeviceContext::Dispatch(UINT groupCountX, UINT groupCountY)
    {
        assert(groupCountX <= D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);
        assert(groupCountY <= D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION);

        context->Dispatch(groupCountX, groupCountY, 1);
        ++dispatchCount;
    }

    void DeviceContext::Draw(UINT vertexCount, UINT instanceCount)
    {
        context->DrawInstanced(vertexCount, instanceCount, 0, 0);
        ++drawCount;
    }
}
