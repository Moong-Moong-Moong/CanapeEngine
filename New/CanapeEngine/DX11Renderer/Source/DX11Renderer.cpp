#include "DX11Renderer.h"

#include "../DX11Device.h"
#include "../DX11SwapChain.h"
#include "../DX11RenderScene.h"
#include "../DX11MeshResource.h"
#include "../DX11MaterialResource.h"
#include "../DX11TextureResource.h"
#include "../DX11Shader.h"
#include "../DX11RenderState.h"

namespace Canape
{
    namespace
    {
        constexpr wchar_t kDefaultShaderPath[] = L"../DX11Renderer/fx/Default.fx";

        struct DrawConstants
        {
            Matrix4 World;
            Matrix4 ViewProjection;
            Vector4 BaseColor;
        };

        static_assert(sizeof(DrawConstants) % 16 == 0);

        const D3D11_INPUT_ELEMENT_DESC kStaticVertexLayout[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
    }

    DX11Renderer::DX11Renderer()
        : m_device(std::make_unique<Device>())
        , m_swapChain(std::make_unique<DX11SwapChain>(*m_device))
        , m_resourceManager(std::make_unique<DX11ResourceManager>(*m_device))
        , m_renderScene(std::make_unique<DX11RenderScene>())
        , m_shader(std::make_unique<DX11Shader>())
        , m_renderState(std::make_unique<DX11RenderState>())
    {
    }

    DX11Renderer::~DX11Renderer() = default;

    bool DX11Renderer::Initialize(void* nativeWindow, uint32_t width, uint32_t height)
    {
        m_Width = width > 0 ? width : 1;
        m_Height = height > 0 ? height : 1;

        if (!m_device->Initialize())
        {
            m_LastError = "D3D11 device creation failed";
            return false;
        }

        if (!m_swapChain->Initialize(static_cast<HWND>(nativeWindow), m_Width, m_Height))
        {
            m_LastError = "D3D11 swap chain creation failed";
            return false;
        }

        if (!m_shader->Load(*m_device, kDefaultShaderPath, "VS_Main", "PS_Main", kStaticVertexLayout,
            sizeof(DrawConstants), m_LastError))
        {
            return false;
        }

        if (!m_renderState->Initialize(*m_device))
        {
            m_LastError = "D3D11 render state creation failed";
            return false;
        }

        const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
        m_whiteTexture = m_resourceManager->CreateTextureResource(1, 1, whitePixel);
        if (!m_whiteTexture.IsValid())
        {
            m_LastError = "D3D11 white texture creation failed";
            return false;
        }

        return true;
    }

    bool DX11Renderer::BeginFrame()
    {
        m_renderScene->Clear();

        ID3D11RenderTargetView* renderTargetView = m_swapChain->GetRenderTargetView();
        if (!renderTargetView)
        {
            return false;
        }

        m_device->GetDeviceContext()->OMSetRenderTargets(1, &renderTargetView, m_swapChain->GetDepthStencilView());
        m_swapChain->SetViewport();

        const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_swapChain->ClearBackBuffer(clearColor, D3D11_CLEAR_DEPTH, 1.0f, 0);

        return true;
    }

    void DX11Renderer::DrawScene()
    {
        ID3D11DeviceContext* context = m_device->GetDeviceContext();
        ID3D11SamplerState* sampler = m_renderState->GetSampler();

        m_shader->Bind(context);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->PSSetSamplers(0, 1, &sampler);
        context->RSSetState(m_renderState->GetRasterizerState(DX11RasterizerState::Solid));

        const DX11RenderView& camera = m_renderScene->GetCamera();

        DrawConstants constants;
        constants.ViewProjection = camera.view * camera.projection;

        for (const DX11RenderItem& item : m_renderScene->GetItems())
        {
            const DX11MeshResource* mesh = m_resourceManager->GetMeshResource(item.mesh);
            if (!mesh || item.submeshIndex >= mesh->SubMeshes.size())
            {
                continue;
            }

            const DX11MaterialResource* material = m_resourceManager->GetMaterialResource(item.material);
            const TextureResourceId textureId = material && material->BaseColorTexture.IsValid() ? material->BaseColorTexture : m_whiteTexture;
            const DX11TextureResource* texture = m_resourceManager->GetTextureResource(textureId);
            if (!texture)
            {
                continue;
            }

            const bool transparent = material && material->Desc.alphaMode == DX11AlphaMode::Transparent;
            context->OMSetBlendState(m_renderState->GetBlendState(transparent ? DX11BlendState::AlphaBlend : DX11BlendState::Default), nullptr, 0xFFFFFFFF);
            context->OMSetDepthStencilState(m_renderState->GetDepthStencilState(transparent ? DX11DepthState::DepthWriteOff : DX11DepthState::DepthOn), 0);

            constants.World = item.world;
            constants.BaseColor = material ? material->Desc.baseColor : Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
            m_shader->UpdateConstants(*m_device, &constants, sizeof(constants));

            ID3D11Buffer* vertexBuffer = mesh->VertexBuffer.Get();
            const UINT stride = mesh->VertexStride;
            const UINT offset = 0;
            context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            context->IASetIndexBuffer(mesh->IndexBuffer.Get(), mesh->IndexFormat, 0);

            ID3D11ShaderResourceView* shaderResourceView = texture->ShaderResourceView.Get();
            context->PSSetShaderResources(0, 1, &shaderResourceView);

            const DX11SubMesh& subMesh = mesh->SubMeshes[item.submeshIndex];
            context->DrawIndexed(subMesh.indexCount, subMesh.startIndex, subMesh.baseVertex);
        }
    }

    void DX11Renderer::EndFrame()
    {
        if (FAILED(m_swapChain->Flip(true)))
        {
            m_LastError = "D3D11 present failed";
        }
    }

    uint32_t DX11Renderer::GetWidth() const
    {
        return m_Width;
    }

    uint32_t DX11Renderer::GetHeight() const
    {
        return m_Height;
    }

    const std::string& DX11Renderer::GetLastError() const
    {
        return m_LastError;
    }

    bool DX11Renderer::UpLoadMeshResource()
    {
        return m_resourceManager->UpLoadMeshResouce();
    }

    MeshResourceId DX11Renderer::CreateMeshResource(const void* vertexData, uint32_t vertexCount, uint32_t vertexStride,
        std::span<const uint32_t> indices, std::span<const DX11SubMesh> subMeshes)
    {
        return m_resourceManager->CreateMeshResource(vertexData, vertexCount, vertexStride, indices, subMeshes);
    }

    TextureResourceId DX11Renderer::CreateTextureResource(uint32_t width, uint32_t height, std::span<const uint8_t> rgba8Pixels)
    {
        return m_resourceManager->CreateTextureResource(width, height, rgba8Pixels);
    }

    MaterialResourceId DX11Renderer::CreateMaterialResource(const DX11MaterialDesc& desc, TextureResourceId baseColorTexture)
    {
        return m_resourceManager->CreateMaterialResource(desc, baseColorTexture);
    }

    void DX11Renderer::DestroyMeshResource(MeshResourceId id)
    {
        m_resourceManager->DestroyMeshResource(id);
    }

    void DX11Renderer::DestroyTextureResource(TextureResourceId id)
    {
        m_resourceManager->DestroyTextureResource(id);
    }

    void DX11Renderer::DestroyMaterialResource(MaterialResourceId id)
    {
        m_resourceManager->DestroyMaterialResource(id);
    }

    void DX11Renderer::SetCamera(const DX11RenderView& camera)
    {
        m_renderScene->SetCamera(camera);
    }

    void DX11Renderer::AddRenderItem(const DX11RenderItem& item)
    {
        m_renderScene->AddRenderItem(item);
    }
}
