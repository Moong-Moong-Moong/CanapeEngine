#pragma once

#include <wrl/client.h>
#include <D3D11.h>
#include <cstddef>

namespace Canape
{
    class Device;

    enum class DX11DepthState { DepthOn, DepthOff, DepthWriteOff, Count };
    enum class DX11RasterizerState { Solid, Wireframe, CullBack, WireframeCullBack, Count };
    enum class DX11BlendState { Default, AlphaBlend, Count };

    class DX11RenderState
    {
    public:
        DX11RenderState() = default;
        DX11RenderState(const DX11RenderState&) = delete;
        DX11RenderState& operator=(const DX11RenderState&) = delete;

        bool Initialize(Device& device);

        ID3D11DepthStencilState* GetDepthStencilState(DX11DepthState state) const { return m_depthStencilStates[ToIndex(state)].Get(); }
        ID3D11RasterizerState* GetRasterizerState(DX11RasterizerState state) const { return m_rasterizerStates[ToIndex(state)].Get(); }
        ID3D11BlendState* GetBlendState(DX11BlendState state) const { return m_blendStates[ToIndex(state)].Get(); }
        ID3D11SamplerState* GetSampler() const { return m_sampler.Get(); }

    private:
        template <typename T>
        static constexpr size_t ToIndex(T state) { return static_cast<size_t>(state); }

        bool CreateDepthStencilStates(ID3D11Device* device);
        bool CreateRasterizerStates(ID3D11Device* device);
        bool CreateBlendStates(ID3D11Device* device);
        bool CreateSampler(ID3D11Device* device);

    private:
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilStates[ToIndex(DX11DepthState::Count)];
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizerStates[ToIndex(DX11RasterizerState::Count)];
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendStates[ToIndex(DX11BlendState::Count)];
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    };
}
