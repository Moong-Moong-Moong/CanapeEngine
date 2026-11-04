#pragma once
#include "DX11Renderer/Pipeline/DX11ConstantBuffers.h"
#include "DX11Renderer/Pipeline/DX11PipelineState.h"
#include "DX11Renderer/Resource/DX11MeshResource.h"

namespace Canape
{
    struct MainTargets
    {
        ID3D11RenderTargetView* Color = nullptr;
        ID3D11DepthStencilView* Depth = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
    };
    struct SceneBindings
    {
        SceneConstantBuffers Constants;
        ID3D11ShaderResourceView* ShadowView = nullptr;
        ID3D11SamplerState* ShadowSampler = nullptr;
    };
    namespace RenderBindings
    {
        void BindMainTargets(ID3D11DeviceContext& context, const MainTargets& targets);
        void BindScene(ID3D11DeviceContext& context, const SceneBindings& resources);
        void BindPipeline(ID3D11DeviceContext& context, const DX11PipelineSelection& pipeline);
        void BindObject(ID3D11DeviceContext& context, ID3D11Buffer* buffer);
        void BindMaterial(ID3D11DeviceContext& context, ID3D11Buffer* buffer, ID3D11ShaderResourceView* texture);
        void BindGeometry(ID3D11DeviceContext& context, const DX11MeshResource& mesh);
    }
}
