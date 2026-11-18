#include "DX11Renderer/Pass/DX11IBLBaker.h"
#include "DX11Renderer/Pipeline/DX11PipelineState.h"
#include "CNDebug/DebugLog.h"

#include <DirectXPackedVector.h>
#include <d3d11sdklayers.h>
#include <algorithm>
#include <array>
#include <cmath>
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
using Color = std::array<float, 3>;

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static Color Direction(uint32_t face, float u, float v)
{
    Color d{};
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
    for (float& value : d) value /= length;
    return d;
}

enum class Environment { Constant, Gradient, Spot };

static ComPtr<ID3D11ShaderResourceView> MakeEnvironment(ID3D11Device& device, Environment kind)
{
    constexpr uint32_t faceSize = 64;
    const uint32_t mipCount = kind == Environment::Spot ? 7 : 1;
    std::vector<std::vector<float>> pixels(size_t(6) * mipCount);
    std::vector<D3D11_SUBRESOURCE_DATA> data(size_t(6) * mipCount);
    for (uint32_t face = 0; face < 6; ++face)
    {
        for (uint32_t mip = 0; mip < mipCount; ++mip)
        {
            const uint32_t size = faceSize >> mip;
            auto& current = pixels[size_t(face) * mipCount + mip];
            current.resize(size_t(size) * size * 4);
            for (uint32_t y = 0; y < size; ++y)
                for (uint32_t x = 0; x < size; ++x)
                {
                    const size_t index = (size_t(y) * size + x) * 4;
                    if (mip != 0)
                    {
                        const auto& previous = pixels[size_t(face) * mipCount + mip - 1];
                        const uint32_t previousSize = size * 2;
                        for (uint32_t c = 0; c < 4; ++c)
                        {
                            float sum = 0.0f;
                            for (uint32_t dy = 0; dy < 2; ++dy)
                                for (uint32_t dx = 0; dx < 2; ++dx)
                                    sum += previous[(size_t(2*y + dy) * previousSize + 2*x + dx) * 4 + c];
                            current[index + c] = sum * .25f;
                        }
                        continue;
                    }
                    const Color d = Direction(face, 2.0f * (x + .5f) / size - 1.0f,
                        2.0f * (y + .5f) / size - 1.0f);
                    const float cone = kind == Environment::Spot
                        ? std::pow(std::clamp((d[2] - .98f) / .02f, 0.0f, 1.0f), 4.0f) : 0.0f;
                    current[index] = 2.0f + (kind == Environment::Gradient ? .5f * d[0] : 0.0f)
                        + 20.0f * cone;
                    current[index + 1] = 4.0f + (kind == Environment::Gradient ? .5f * d[1] : 0.0f);
                    current[index + 2] = 1.5f + (kind == Environment::Gradient ? .5f * d[2] : 0.0f);
                    current[index + 3] = 1.0f;
                }
            data[size_t(face) * mipCount + mip] = { current.data(), size * 16, size * size * 16 };
        }
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = faceSize;
    desc.MipLevels = mipCount;
    desc.ArraySize = 6;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
    ComPtr<ID3D11Texture2D> texture;
    Require(SUCCEEDED(device.CreateTexture2D(&desc, data.data(), &texture)), "source cube texture");
    ComPtr<ID3D11ShaderResourceView> view;
    Require(SUCCEEDED(device.CreateShaderResourceView(texture.Get(), nullptr, &view)), "source cube SRV");
    return view;
}

static ComPtr<ID3D11Texture2D> Readback(ID3D11Device& device, ID3D11DeviceContext& context,
    const DX11PrefilteredCubemap& output, uint32_t size, uint32_t mips)
{
    D3D11_TEXTURE2D_DESC desc{};
    output.Texture->GetDesc(&desc);
    Require(desc.Width == size && desc.Height == size && desc.ArraySize == 6
        && desc.MipLevels == mips && desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT
        && (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)
        && !(desc.MiscFlags & D3D11_RESOURCE_MISC_GENERATE_MIPS), "prefilter texture layout");
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    output.ShaderResourceView->GetDesc(&view);
    Require(view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURECUBE
        && view.TextureCube.MipLevels == mips, "prefilter cube SRV");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device.CreateTexture2D(&desc, nullptr, &staging)), "prefilter staging");
    context.CopyResource(staging.Get(), output.Texture.Get());
    return staging;
}

static std::vector<Color> ReadFace(ID3D11DeviceContext& context, ID3D11Texture2D& staging,
    uint32_t face, uint32_t mip, uint32_t mipCount, uint32_t size)
{
    const uint32_t subresource = D3D11CalcSubresource(mip, face, mipCount);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(SUCCEEDED(context.Map(&staging, subresource, D3D11_MAP_READ, 0, &mapped)), "map prefilter face/mip");
    std::vector<Color> colors(size_t(size) * size);
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x)
        {
            const auto* half = reinterpret_cast<const DirectX::PackedVector::HALF*>(
                static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch + size_t(x) * 8);
            for (uint32_t c = 0; c < 3; ++c)
                colors[size_t(y) * size + x][c] = DirectX::PackedVector::XMConvertHalfToFloat(half[c]);
        }
    context.Unmap(&staging, subresource);
    return colors;
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
            const auto oldVS = pipelines.ResolveFinalOutput(false).VS;
            Require(baker.Initialize(device.Get(), oldVS, error), error.c_str());

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
            context->VSSetShader(oldVS, nullptr, 0);

            for (Environment kind : { Environment::Constant, Environment::Gradient, Environment::Spot })
            {
                auto source = MakeEnvironment(*device.Get(), kind);
                ID3D11ShaderResourceView* sourceView = source.Get();
                context->PSSetShaderResources(0, 1, &sourceView);
                DX11PrefilteredCubemap output;
                const uint32_t size = kind == Environment::Constant ? 128 : 32;
                const uint32_t sampleCount = kind == Environment::Constant ? 8 :
                    kind == Environment::Gradient ? 64 : 128;
                Require(baker.BakePrefilteredEnvironment(context.Get(), source.Get(), output,
                    error, sampleCount, size, 5), error.c_str());
                auto staging = Readback(*device.Get(), *context.Get(), output, size, 5);
                float spotCenters[5]{};
                float spotHalo[5]{};
                Color gradientCenters[5][6]{};
                for (uint32_t face = 0; face < 6; ++face)
                    for (uint32_t mip = 0; mip < 5; ++mip)
                    {
                        const uint32_t mipSize = size >> mip;
                        const auto colors = ReadFace(*context.Get(), *staging.Get(), face, mip, 5, mipSize);
                        for (uint32_t y = 0; y < mipSize; ++y)
                            for (uint32_t x = 0; x < mipSize; ++x)
                            {
                                const Color& color = colors[size_t(y) * mipSize + x];
                                for (float value : color) Require(std::isfinite(value), "prefilter NaN/Inf");
                                if (kind == Environment::Constant)
                                {
                                    const Color expected{2.0f, 4.0f, 1.5f};
                                    for (uint32_t c = 0; c < 3; ++c)
                                        Require(std::abs(color[c] - expected[c]) < .02f,
                                            "constant HDR not preserved across face/mip");
                                }
                                if (kind == Environment::Gradient && mip == 0
                                    && (x == 8 || x == 16 || x == 24)
                                    && (y == 8 || y == 16 || y == 24))
                                {
                                    const Color d = Direction(face, 2.0f * (x + .5f) / mipSize - 1.0f,
                                        2.0f * (y + .5f) / mipSize - 1.0f);
                                    const Color base{2.0f, 4.0f, 1.5f};
                                    for (uint32_t c = 0; c < 3; ++c)
                                        Require(std::abs(color[c] - (base[c] + .5f * d[c])) < .04f,
                                            "prefilter face direction");
                                }
                            }
                        if (kind == Environment::Gradient)
                            gradientCenters[mip][face] = colors[size_t(mipSize / 2) * mipSize + mipSize / 2];
                        if (kind == Environment::Spot && face == 4)
                        {
                            spotCenters[mip] = colors[size_t(mipSize / 2) * mipSize + mipSize / 2][0];
                            spotHalo[mip] = colors[size_t(mipSize / 2) * mipSize + mipSize * 3 / 4][0];
                        }
                    }
                if (kind == Environment::Gradient)
                    for (uint32_t mip = 0; mip < 5; ++mip)
                        Require(gradientCenters[mip][0][0] > gradientCenters[mip][1][0] + .02f
                            && gradientCenters[mip][2][1] > gradientCenters[mip][3][1] + .02f
                            && gradientCenters[mip][4][2] > gradientCenters[mip][5][2] + .02f,
                            "prefilter face/mip orientation");
                if (kind == Environment::Spot)
                {
                    std::cout << "spot center red at roughness 0/.25/.5/.75/1: ";
                    for (float value : spotCenters) std::cout << value << ' ';
                    std::cout << '\n';
                    std::cout << "spot halo red at roughness 0/.25/.5/.75/1: ";
                    for (float value : spotHalo) std::cout << value << ' ';
                    std::cout << '\n';
                    Require(spotCenters[0] > spotCenters[2] + 2.0f
                        && spotCenters[2] > spotCenters[4] + .5f
                        && spotCenters[4] > 2.0f
                        && spotHalo[2] > spotHalo[0] + .1f,
                        "GGX roughness does not broaden highlight");
                }
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
                auto* bakedTexture = output.Texture.Get();
                auto* bakedView = output.ShaderResourceView.Get();
                Require(!baker.BakePrefilteredEnvironment(context.Get(), source.Get(), output,
                    error, 0, size, 5) && output.Texture.Get() == bakedTexture
                    && output.ShaderResourceView.Get() == bakedView,
                    "failed prefilter changed previous output");
            }
            context->ClearState();
        }
        for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i)
        {
            SIZE_T length = 0;
            queue->GetMessage(i, nullptr, &length);
            std::vector<uint8_t> bytes(length);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
            queue->GetMessage(i, message, &length);
            if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
            {
                std::cerr << "D3D11: " << message->pDescription << '\n';
                throw std::runtime_error("D3D11 debug-layer warning/error");
            }
        }
        std::cout << "IBL prefilter WARP: 6 faces x 5 mips, constant HDR, direction, GGX blur, state PASS\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
