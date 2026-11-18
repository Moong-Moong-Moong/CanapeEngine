#include "DX11Renderer/Pass/DX11IBLBaker.h"
#include "DX11Renderer/Pipeline/DX11PipelineState.h"
#include "CNDebug/DebugLog.h"

#include <DirectXPackedVector.h>
#include <d3d11sdklayers.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <numbers>
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
using AB = std::array<float, 2>;

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static double RadicalInverse(uint32_t bits)
{
    bits = (bits << 16) | (bits >> 16);
    bits = ((bits & 0x55555555u) << 1) | ((bits & 0xAAAAAAAAu) >> 1);
    bits = ((bits & 0x33333333u) << 2) | ((bits & 0xCCCCCCCCu) >> 2);
    bits = ((bits & 0x0F0F0F0Fu) << 4) | ((bits & 0xF0F0F0F0u) >> 4);
    bits = ((bits & 0x00FF00FFu) << 8) | ((bits & 0xFF00FF00u) >> 8);
    return double(bits) / 4294967296.0;
}

// Double-precision reference evaluates the original BRDF * NdotL / PDF.
// The shader uses its algebraically reduced GVis form instead.
static std::array<double, 2> Reference(double nDotV, double roughness, uint32_t samples)
{
    constexpr double pi = std::numbers::pi_v<double>;
    const double vx = std::sqrt(1.0 - nDotV * nDotV);
    const double alpha = roughness * roughness;
    const double alphaSquared = alpha * alpha;
    const double k = alpha * 0.5;
    const double geometryV = nDotV / (nDotV * (1.0 - k) + k);
    double f0Zero = 0.0;
    double f0One = 0.0;
    for (uint32_t i = 0; i < samples; ++i)
    {
        const double phi = 2.0 * pi * double(i) / samples;
        const double xi = RadicalInverse(i);
        const double cosTheta = std::sqrt((1.0 - xi) / (1.0 + (alphaSquared - 1.0) * xi));
        const double sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
        // For N=(0,0,1), the shared shader basis is T=(0,-1,0), B=(1,0,0).
        const double hx = std::sin(phi) * sinTheta;
        const double hy = -std::cos(phi) * sinTheta;
        const double hz = cosTheta;
        const double vDotH = vx * hx + nDotV * hz;
        if (vDotH <= 0.0) continue;
        const double lx = 2.0 * vDotH * hx - vx;
        const double ly = 2.0 * vDotH * hy;
        const double lz = 2.0 * vDotH * hz - nDotV;
        const double length = std::sqrt(lx * lx + ly * ly + lz * lz);
        const double nDotL = lz / length;
        if (nDotL <= 0.0 || hz <= 0.0) continue;
        const double term = hz * hz * (alphaSquared - 1.0) + 1.0;
        const double distribution = alphaSquared / (pi * term * term);
        const double pdfL = distribution * hz / (4.0 * vDotH);
        const double geometryL = nDotL / (nDotL * (1.0 - k) + k);
        const double geometry = geometryV * geometryL;
        const double brdf = distribution * geometry / (4.0 * nDotV * nDotL);
        const double sample = brdf * nDotL / pdfL;
        const double fc = std::pow(1.0 - vDotH, 5.0);
        f0Zero += fc * sample;
        f0One += sample;
    }
    return { (f0One - f0Zero) / samples, f0Zero / samples };
}

static std::vector<AB> Readback(ID3D11Device& device, ID3D11DeviceContext& context,
    const DX11BRDFLUT& output, uint32_t size)
{
    D3D11_TEXTURE2D_DESC desc{};
    output.Texture->GetDesc(&desc);
    Require(desc.Width == size && desc.Height == size && desc.ArraySize == 1
        && desc.MipLevels == 1 && desc.Format == DXGI_FORMAT_R16G16_FLOAT
        && (desc.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE))
            == (D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE), "BRDF LUT texture layout");
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    output.ShaderResourceView->GetDesc(&view);
    Require(view.ViewDimension == D3D11_SRV_DIMENSION_TEXTURE2D
        && view.Texture2D.MipLevels == 1, "BRDF LUT SRV layout");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(device.CreateTexture2D(&desc, nullptr, &staging)), "BRDF LUT staging");
    context.CopyResource(staging.Get(), output.Texture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(SUCCEEDED(context.Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map BRDF LUT");
    std::vector<AB> pixels(size_t(size) * size);
    for (uint32_t y = 0; y < size; ++y)
        for (uint32_t x = 0; x < size; ++x)
        {
            const auto* half = reinterpret_cast<const DirectX::PackedVector::HALF*>(
                static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch + size_t(x) * 4);
            AB& value = pixels[size_t(y) * size + x];
            value[0] = DirectX::PackedVector::XMConvertHalfToFloat(half[0]);
            value[1] = DirectX::PackedVector::XMConvertHalfToFloat(half[1]);
            Require(std::isfinite(value[0]) && std::isfinite(value[1])
                && value[0] >= 0.0f && value[1] >= 0.0f, "BRDF LUT invalid A/B");
        }
    context.Unmap(staging.Get(), 0);
    return pixels;
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
            const auto pipeline = pipelines.ResolveFinalOutput(false);
            DX11IBLBaker baker;
            Require(baker.Initialize(device.Get(), pipeline.VS, error), error.c_str());

            D3D11_TEXTURE2D_DESC targetDesc{};
            targetDesc.Width = targetDesc.Height = 8;
            targetDesc.MipLevels = targetDesc.ArraySize = 1;
            targetDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
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
            context->VSSetShader(pipeline.VS, nullptr, 0);
            context->PSSetShader(pipeline.PS, nullptr, 0);
            D3D11_BUFFER_DESC constantDesc{};
            constantDesc.ByteWidth = 16;
            constantDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            ComPtr<ID3D11Buffer> oldConstants;
            Require(SUCCEEDED(device->CreateBuffer(&constantDesc, nullptr, &oldConstants)),
                "sentinel constants");
            ID3D11Buffer* beforeConstants = oldConstants.Get();
            context->PSSetConstantBuffers(0, 1, &beforeConstants);

            constexpr uint32_t testSize = 32;
            constexpr uint32_t testSamples = 256;
            DX11BRDFLUT testOutput;
            Require(baker.BakeBRDFLUT(context.Get(), testOutput, error, testSamples, testSize), error.c_str());
            const auto pixels = Readback(*device.Get(), *context.Get(), testOutput, testSize);
            const auto at = [&](uint32_t x, uint32_t y) -> const AB& { return pixels[size_t(y) * testSize + x]; };
            for (uint32_t y : {8u, 16u, 24u})
                for (uint32_t x : {8u, 16u, 24u})
                {
                    const double nDotV = (x + 0.5) / testSize;
                    const double roughness = (y + 0.5) / testSize;
                    const auto reference = Reference(nDotV, roughness, testSamples);
                    const AB gpu = at(x, y);
                    Require(std::abs(gpu[1] - reference[1]) < .025
                        && std::abs((gpu[0] + gpu[1]) - (reference[0] + reference[1])) < .025
                        && std::abs((.04 * gpu[0] + gpu[1])
                            - (.04 * reference[0] + reference[1])) < .025,
                        "BRDF LUT differs from original BRDF/PDF CPU reference");
                }
            Require(at(24, 0)[0] > at(8, 0)[0]
                && at(8, 0)[1] > at(24, 0)[1]
                && at(24, 0)[0] > at(24, 31)[0], "BRDF LUT axes or boundary behavior");

            // Keep the previous LUT bound while baking a new one: no SRV/RTV hazard.
            ID3D11ShaderResourceView* beforeSource = testOutput.ShaderResourceView.Get();
            context->PSSetShaderResources(0, 1, &beforeSource);
            DX11BRDFLUT defaultOutput;
            Require(baker.BakeBRDFLUT(context.Get(), defaultOutput, error, 8), error.c_str());
            const auto defaultPixels = Readback(*device.Get(), *context.Get(), defaultOutput,
                DX11IBLBaker::BRDFLUTSize);
            Require(defaultPixels.size() == 256u * 256u, "default BRDF LUT size");
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
            ComPtr<ID3D11PixelShader> afterPS;
            context->VSGetShader(&afterVS, nullptr, nullptr);
            context->PSGetShader(&afterPS, nullptr, nullptr);
            Require(afterVS.Get() == pipeline.VS && afterPS.Get() == pipeline.PS, "shaders restored");
            ComPtr<ID3D11ShaderResourceView> afterSource;
            context->PSGetShaderResources(0, 1, &afterSource);
            Require(afterSource.Get() == testOutput.ShaderResourceView.Get(), "SRV restored");
            ComPtr<ID3D11Buffer> afterConstants;
            context->PSGetConstantBuffers(0, 1, &afterConstants);
            Require(afterConstants.Get() == oldConstants.Get(), "constant buffer restored");
            auto* previousTexture = defaultOutput.Texture.Get();
            auto* previousView = defaultOutput.ShaderResourceView.Get();
            Require(!baker.BakeBRDFLUT(context.Get(), defaultOutput, error, 0)
                && defaultOutput.Texture.Get() == previousTexture
                && defaultOutput.ShaderResourceView.Get() == previousView,
                "failed BRDF bake changed previous output");
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
        std::cout << "IBL BRDF LUT WARP: RG16F, 256x256, CPU integral, boundaries, state PASS\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
