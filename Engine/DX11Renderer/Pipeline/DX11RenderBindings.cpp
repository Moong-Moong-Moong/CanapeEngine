#include "DX11Renderer/Pipeline/DX11RenderBindings.h"

namespace Canape::RenderBindings
{
    void BindMainTargets(ID3D11DeviceContext& context, const MainTargets& targets)
    {
        ID3D11RenderTargetView* target = targets.Color;
        context.OMSetRenderTargets(1, &target, targets.Depth);
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(targets.Width);
        viewport.Height = static_cast<float>(targets.Height);
        viewport.MaxDepth = 1.0f;
        context.RSSetViewports(1, &viewport);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    void BindScene(ID3D11DeviceContext& context, const SceneBindings& resources)
    {
        ID3D11Buffer* viewBuffer = resources.Constants.View;
        ID3D11Buffer* sceneBuffer = resources.Constants.Scene;
        ID3D11Buffer* lightBuffer = resources.Constants.Lights;
        context.VSSetConstantBuffers(2, 1, &viewBuffer);
        context.PSSetConstantBuffers(2, 1, &viewBuffer);
        context.PSSetConstantBuffers(3, 1, &lightBuffer);
        context.PSSetConstantBuffers(4, 1, &sceneBuffer);
        ID3D11Buffer* shadowBuffer = resources.Constants.Shadows;
        context.VSSetConstantBuffers(5, 1, &shadowBuffer);
        context.PSSetConstantBuffers(5, 1, &shadowBuffer);
        ID3D11Buffer* pcssBuffer = resources.Constants.Pcss;
        context.PSSetConstantBuffers(7, 1, &pcssBuffer);
        ID3D11ShaderResourceView* shadowView = resources.ShadowView;
        ID3D11SamplerState* shadowSampler = resources.ShadowSampler;
        context.PSSetShaderResources(1, 1, &shadowView);
        context.PSSetSamplers(1, 1, &shadowSampler);
        context.PSSetShaderResources(2, 1, &resources.EnvironmentView);
        context.PSSetSamplers(2, 1, &resources.EnvironmentSampler);
        context.PSSetConstantBuffers(6, 1, &resources.SkyboxConstants);
    }

    void BindPipeline(ID3D11DeviceContext& context, const DX11PipelineSelection& pipeline)
    {
        context.IASetInputLayout(pipeline.InputLayout);
        context.VSSetShader(pipeline.VS, nullptr, 0);
        context.PSSetShader(pipeline.PS, nullptr, 0);
        context.RSSetState(pipeline.Rasterizer);
        context.OMSetBlendState(pipeline.Blend, nullptr, UINT32_MAX);
        context.OMSetDepthStencilState(pipeline.Depth, 0);
        context.PSSetSamplers(0, 1, &pipeline.Sampler);
    }

    void BindObject(ID3D11DeviceContext& context, ID3D11Buffer* buffer)
    {
        context.VSSetConstantBuffers(0, 1, &buffer);
    }

    void BindMaterial(ID3D11DeviceContext& context, ID3D11Buffer* buffer, ID3D11ShaderResourceView* texture)
    {
        context.PSSetConstantBuffers(1, 1, &buffer);
        context.PSSetShaderResources(0, 1, &texture);
    }

    void BindGeometry(ID3D11DeviceContext& context, const DX11MeshResource& mesh)
    {
        const UINT stride = mesh.Stride;
        constexpr UINT offset = 0;
        ID3D11Buffer* vertexBuffer = mesh.VertexBuffer.Get();
        context.IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context.IASetIndexBuffer(mesh.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
    }
}
