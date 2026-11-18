#include "DX11Renderer/Pass/DX11IBLBaker.h"
#include "DX11Renderer/Pipeline/DX11PipelineState.h"
#include "CNDebug/DebugLog.h"

#include <DirectXPackedVector.h>
#include <d3d11sdklayers.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Canape::Debug
{
void Write(MainTag, std::string_view, std::string message, const char*, int)
{
    std::cerr << message << '\n';
}
}

using namespace Canape;
using Microsoft::WRL::ComPtr;

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static std::array<float, 3> Direction(uint32_t face, float u, float v)
{
    std::array<float, 3> d{};
    switch (face)
    {
    case 0: d = { 1, -v, -u }; break;
    case 1: d = {-1, -v,  u }; break;
    case 2: d = { u,  1,  v }; break;
    case 3: d = { u, -1, -v }; break;
    case 4: d = { u, -v,  1 }; break;
    default:d = {-u, -v, -1 }; break;
    }
    const float length = std::sqrt(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
    for (auto& value : d) value /= length;
    return d;
}

static ComPtr<ID3D11ShaderResourceView> CreateEnvironment(ID3D11Device& device, bool gradient)
{
    constexpr uint32_t size = 32;
    std::array<std::vector<float>, 6> faces;
    std::array<D3D11_SUBRESOURCE_DATA, 6> data{};
    for (uint32_t face = 0; face < 6; ++face)
    {
        faces[face].resize(size * size * 4);
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const auto d = Direction(face, 2.0f * (x + .5f) / size - 1.0f,
                    2.0f * (y + .5f) / size - 1.0f);
                const size_t index = (size_t(y) * size + x) * 4;
                for (uint32_t c = 0; c < 3; ++c)
                    faces[face][index + c] = gradient ? .5f + .5f * d[c]
                        : (c == 0 ? 2.0f : c == 1 ? 4.0f : 1.5f);
                faces[face][index + 3] = 1.0f;
            }
        data[face] = { faces[face].data(), size * 16, size * size * 16 };
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = size;
    desc.MipLevels = 1;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device.CreateTexture2D(&desc, data.data(), &texture)), "source cube texture");
    ComPtr<ID3D11ShaderResourceView> view;
    Require(SUCCEEDED(device.CreateShaderResourceView(texture.Get(), nullptr, &view)), "source cube SRV");
    return view;
}

static std::array<float, 3> ReadPixel(ID3D11DeviceContext& context, ID3D11Texture2D& staging,
    uint32_t face, uint32_t x, uint32_t y)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(SUCCEEDED(context.Map(&staging, face, D3D11_MAP_READ, 0, &mapped)), "map irradiance face");
    const auto* pixel = reinterpret_cast<const DirectX::PackedVector::HALF*>(
        static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch + size_t(x) * 8);
    std::array<float, 3> color{};
    for (uint32_t c = 0; c < 3; ++c) color[c] = DirectX::PackedVector::XMConvertHalfToFloat(pixel[c]);
    context.Unmap(&staging, face);
    return color;
}

static void CheckResult(ID3D11Device& device, ID3D11DeviceContext& context,
    const DX11IrradianceCubemap& output, bool gradient)
{
    D3D11_TEXTURE2D_DESC desc{};
    output.Texture->GetDesc(&desc);
    Require(desc.Width == 32 && desc.Height == 32 && desc.ArraySize == 6 && desc.MipLevels == 1
        && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT
        && (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE), "irradiance texture layout");
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    output.ShaderResourceView->GetDesc(&view);
    Require(view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE && view.TextureCube.MipLevels == 1,
        "irradiance cube SRV");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.CPUAccessFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device.CreateTexture2D(&desc, nullptr, &staging)), "irradiance staging");
    context.CopyResource(staging.Get(), output.Texture.Get());
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t y : { 8u, 16u, 24u })
            for (uint32_t x : { 8u, 16u, 24u })
            {
                const auto color = ReadPixel(context, *staging.Get(), face, x, y);
                const auto n = Direction(face, 2.0f * (x + .5f) / 32.0f - 1.0f,
                    2.0f * (y + .5f) / 32.0f - 1.0f);
                for (uint32_t c = 0; c < 3; ++c)
                {
                    // For Li(L)=0.5+0.5*L, cosine-weighted E/PI=0.5+L(N)/3.
                    const float expected = gradient ? .5f + n[c] / 3.0f
                        : (c == 0 ? 2.0f : c == 1 ? 4.0f : 1.5f);
                    Require(std::abs(color[c] - expected) < (gradient ? .07f : .01f),
                        "irradiance color, HDR, or face orientation");
                }
            }
}

int main()
{
    try
    {
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_DEBUG, &level, 1, D3D11_SDK_VERSION,
            &device, nullptr, &context)), "WARP debug device");
        ComPtr<ID3D11InfoQueue> queue;
        Require(SUCCEEDED(device.As(&queue)), "D3D11 debug queue");
        {
            DX11PipelineState pipelines;
            std::string error;
            Require(pipelines.Initialize(device.Get(), error), error.c_str());
            DX11IBLBaker baker;
            Require(baker.Initialize(device.Get(), pipelines.ResolveFinalOutput(false).VS, error), error.c_str());

            D3D11_TEXTURE2D_DESC targetDesc{};
            targetDesc.Width = targetDesc.Height = 8;
            targetDesc.MipLevels = targetDesc.ArraySize = 1;
            targetDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
            targetDesc.SampleDesc.Count = 1;
            targetDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
            ComPtr<ID3D11Texture2D> target;
            ComPtr<ID3D11RenderTargetView> targetView;
            Require(SUCCEEDED(device->CreateTexture2D(&targetDesc, nullptr, &target)), "sentinel target");
            Require(SUCCEEDED(device->CreateRenderTargetView(target.Get(), nullptr, &targetView)), "sentinel RTV");
            ID3D11RenderTargetView* beforeTarget = targetView.Get();
            context->OMSetRenderTargets(1, &beforeTarget, nullptr);
            D3D11_VIEWPORT viewport{};
            viewport.Width = viewport.Height = 8.0f;
            viewport.MaxDepth = 1.0f;
            context->RSSetViewports(1, &viewport);
            context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
            const auto oldVS = pipelines.ResolveFinalOutput(false).VS;
            context->VSSetShader(oldVS, nullptr, 0);

            for (bool gradient : { false, true })
            {
                auto source = CreateEnvironment(*device.Get(), gradient);
                ID3D11ShaderResourceView* beforeSource = source.Get();
                context->PSSetShaderResources(0, 1, &beforeSource);
                DX11IrradianceCubemap output;
                Require(baker.BakeIrradiance(context.Get(), source.Get(), output, error, 64), error.c_str());
                CheckResult(*device.Get(), *context.Get(), output, gradient);
                auto* bakedTexture = output.Texture.Get();
                auto* bakedView = output.ShaderResourceView.Get();
                Require(!baker.BakeIrradiance(context.Get(), source.Get(), output, error, 0)
                    && output.Texture.Get() == bakedTexture
                    && output.ShaderResourceView.Get() == bakedView,
                    "failed bake preserves the previous output");
                ComPtr<ID3D11RenderTargetView> afterTarget;
                context->OMGetRenderTargets(1, &afterTarget, nullptr);
                Require(afterTarget.Get() == targetView.Get(), "render target restored");
                UINT count = 1;
                D3D11_VIEWPORT afterViewport{};
                context->RSGetViewports(&count, &afterViewport);
                Require(count == 1 && afterViewport.Width == 8.0f, "viewport restored");
                D3D11_PRIMITIVE_TOPOLOGY topology{};
                context->IAGetPrimitiveTopology(&topology);
                Require(topology == D3D11_PRIMITIVE_TOPOLOGY_LINELIST, "topology restored");
                ComPtr<ID3D11VertexShader> afterVS;
                context->VSGetShader(&afterVS, nullptr, nullptr);
                Require(afterVS.Get() == oldVS, "vertex shader restored");
                ComPtr<ID3D11ShaderResourceView> afterSource;
                context->PSGetShaderResources(0, 1, &afterSource);
                Require(afterSource.Get() == source.Get(), "source SRV restored");
                output = {};
            }
            context->ClearState();
        }
        UINT warnings = 0;
        for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i)
        {
            SIZE_T length = 0;
            queue->GetMessage(i, nullptr, &length);
            std::vector<uint8_t> bytes(length);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
            queue->GetMessage(i, message, &length);
            if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
            {
                ++warnings;
                std::cerr << "D3D11: " << message->pDescription << '\n';
            }
        }
        Require(warnings == 0, "D3D11 debug-layer warning/error");
        std::cout << "IBL irradiance WARP: constant HDR, six faces, direction, SRV and state restoration PASS\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
