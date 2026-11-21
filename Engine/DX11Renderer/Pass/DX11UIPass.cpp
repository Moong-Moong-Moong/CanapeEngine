#include "DX11Renderer/Pass/DX11UIPass.h"
#include "DX11Renderer/Pipeline/DX11ConstantBuffers.h"
#include "DX11Renderer/Pipeline/DX11RenderBindings.h"
#include "DX11Renderer/Resource/DX11ResourceCache.h"

#include <algorithm>
#include <cstring>
#include <sstream>

namespace Canape::UIPass
{
    namespace
    {
        std::string HResultMessage(const char* operation, HRESULT result)
        {
            std::ostringstream stream;
            stream << operation << " failed (HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(result) << ')';
            return stream.str();
        }

        bool EnsureBuffer(ID3D11Device& device, Microsoft::WRL::ComPtr<ID3D11Buffer>& buffer,
            uint32_t& capacity, uint32_t required, uint32_t stride, UINT bindFlags, const char* name,
            std::string& error)
        {
            if (buffer && capacity >= required) return true;
            const uint32_t grown = (std::max)({ required, capacity * 2, 1024u });
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = grown * stride;
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = bindFlags;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            Microsoft::WRL::ComPtr<ID3D11Buffer> next;
            const HRESULT result = device.CreateBuffer(&desc, nullptr, &next);
            if (FAILED(result))
            {
                error = HResultMessage(name, result);
                return false;
            }
            buffer = std::move(next);
            capacity = grown;
            return true;
        }

        bool Write(ID3D11DeviceContext& context, ID3D11Buffer* buffer, const void* data, size_t bytes,
            std::string& error)
        {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            const HRESULT result = context.Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
            if (FAILED(result))
            {
                error = HResultMessage("ID3D11DeviceContext::Map(UI buffer)", result);
                return false;
            }
            std::memcpy(mapped.pData, data, bytes);
            context.Unmap(buffer, 0);
            return true;
        }

        bool DecodesSrgb(ID3D11ShaderResourceView* view)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
            view->GetDesc(&desc);
            return desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        }
    }

    bool Execute(const UIDrawList& list, const DX11PipelineSelection& pipeline,
        const DX11ResourceCache& resources, DX11UIBuffers& buffers,
        ID3D11Device& device, ID3D11DeviceContext& context,
        ID3D11RenderTargetView* target, ID3D11ShaderResourceView* sceneDepth, uint32_t width, uint32_t height,
        RendererDebugStats* stats, std::string& error)
    {
        if (list.IsEmpty() || list.Vertices.empty() || list.Indices.empty() || !target || width == 0 || height == 0)
            return true;

        const auto vertexCount = static_cast<uint32_t>(list.Vertices.size());
        const auto indexCount = static_cast<uint32_t>(list.Indices.size());
        if (!EnsureBuffer(device, buffers.Vertices, buffers.VertexCapacity, vertexCount, sizeof(UIVertex),
                D3D11_BIND_VERTEX_BUFFER, "ID3D11Device::CreateBuffer(UI vertices)", error)
            || !EnsureBuffer(device, buffers.Indices, buffers.IndexCapacity, indexCount, sizeof(uint32_t),
                D3D11_BIND_INDEX_BUFFER, "ID3D11Device::CreateBuffer(UI indices)", error))
            return false;
        if (!buffers.Constants)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(ShaderConstants::UIConstants);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            const HRESULT result = device.CreateBuffer(&desc, nullptr, &buffers.Constants);
            if (FAILED(result))
            {
                error = HResultMessage("ID3D11Device::CreateBuffer(UI constants)", result);
                return false;
            }
        }
        if (!Write(context, buffers.Vertices.Get(), list.Vertices.data(), list.Vertices.size() * sizeof(UIVertex), error)
            || !Write(context, buffers.Indices.Get(), list.Indices.data(), list.Indices.size() * sizeof(uint32_t), error))
            return false;

        // No depth-stencil view: the camera depth may be multisampled while the UI target is not, and
        // a mismatched OMSetRenderTargets is dropped by the runtime. World-space commands read the depth
        // texture (t1) in the pixel shader instead.
        context.OMSetRenderTargets(1, &target, nullptr);
        if (sceneDepth)
        {
            context.PSSetShaderResources(1, 1, &sceneDepth);
        }
        D3D11_VIEWPORT viewport{};
        viewport.Width = static_cast<float>(width);
        viewport.Height = static_cast<float>(height);
        viewport.MaxDepth = 1.0f;
        context.RSSetViewports(1, &viewport);
        RenderBindings::BindPipeline(context, pipeline);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D11Buffer* vertexBuffer = buffers.Vertices.Get();
        const UINT stride = sizeof(UIVertex);
        const UINT offset = 0;
        context.IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
        context.IASetIndexBuffer(buffers.Indices.Get(), DXGI_FORMAT_R32_UINT, 0);
        ID3D11Buffer* constants = buffers.Constants.Get();
        context.VSSetConstantBuffers(0, 1, &constants);
        context.PSSetConstantBuffers(0, 1, &constants);

        // Render-target pixels -> clip space (row vector, row-major): x' = 2x/w - 1, y' = 1 - 2y/h.
        float screenFromPixels[16]{};
        screenFromPixels[0] = 2.0f / static_cast<float>(width);
        screenFromPixels[5] = -2.0f / static_cast<float>(height);
        screenFromPixels[10] = 1.0f;
        screenFromPixels[12] = -1.0f;
        screenFromPixels[13] = 1.0f;
        screenFromPixels[15] = 1.0f;
        const D3D11_RECT fullTarget{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };

        ShaderConstants::UIConstants uiConstants{};
        bool constantsUploaded = false;
        uint32_t uploadedTransform = 0;

        for (const UIDrawCommand& command : list.Commands)
        {
            if (command.IndexCount == 0 || command.IndexOffset > indexCount
                || command.IndexCount > indexCount - command.IndexOffset)
                continue;

            const bool screenSpace = command.Transform == UIDrawCommand::kScreenSpace;
            if (!screenSpace && command.Transform >= list.Transforms.size())
                continue;
            const UIDrawTransform* transform = screenSpace ? nullptr : &list.Transforms[command.Transform];

            ID3D11ShaderResourceView* view = resources.GetWhiteTextureView();
            if (command.Texture.IsValid())
            {
                const DX11TextureResource* texture = resources.GetTexture(command.Texture);
                if (!texture) continue; // Destroyed texture: skip rather than draw a white box.
                view = texture->ShaderResourceView.Get();
            }
            if (!view) continue;

            if (screenSpace)
            {
                const LONG left = std::clamp<LONG>(command.Clip.Left, 0, static_cast<LONG>(width));
                const LONG top = std::clamp<LONG>(command.Clip.Top, 0, static_cast<LONG>(height));
                const LONG right = std::clamp<LONG>(command.Clip.Right, 0, static_cast<LONG>(width));
                const LONG bottom = std::clamp<LONG>(command.Clip.Bottom, 0, static_cast<LONG>(height));
                if (right <= left || bottom <= top) continue;
                const D3D11_RECT scissor{ left, top, right, bottom };
                context.RSSetScissorRects(1, &scissor);
            }
            else
            {
                // Canvas-space clip rects do not map to a screen rectangle; world canvases are culled on the CPU.
                context.RSSetScissorRects(1, &fullTarget);
            }

            const uint32_t encodeSrgb = DecodesSrgb(view) ? 1u : 0u;
            if (!constantsUploaded || uiConstants.EncodeSrgb != encodeSrgb || uploadedTransform != command.Transform)
            {
                const float* matrix = transform ? transform->ClipFromCanvas : screenFromPixels;
                std::copy_n(matrix, 16, uiConstants.ClipFromCanvas);
                uiConstants.EncodeSrgb = encodeSrgb;
                uiConstants.DepthTest = transform && transform->DepthTest && sceneDepth ? 1u : 0u;
                ConstantBuffers::Upload(context, constants, uiConstants);
                constantsUploaded = true;
                uploadedTransform = command.Transform;
            }
            context.PSSetShaderResources(0, 1, &view);
            context.DrawIndexed(command.IndexCount, command.IndexOffset, 0);
#if CANAPE_RENDER_DEBUG
            if (stats)
            {
                ++stats->drawCalls;
                stats->triangleCount += command.IndexCount / 3;
            }
#else
            (void)stats;
#endif
        }

        // Unbind the color texture and the scene depth before the next frame binds depth as a DSV.
        ID3D11ShaderResourceView* unboundViews[2] = {};
        context.PSSetShaderResources(0, 2, unboundViews);
        ID3D11Buffer* unboundBuffer = nullptr;
        context.VSSetConstantBuffers(0, 1, &unboundBuffer);
        context.PSSetConstantBuffers(0, 1, &unboundBuffer);
        return true;
    }
}
