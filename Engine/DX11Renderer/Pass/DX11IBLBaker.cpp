#include "DX11Renderer/Pass/DX11IBLBaker.h"
#include "DX11Renderer/Pipeline/DX11ShaderCompiler.h"

#include <array>
#include <sstream>
#include <utility>
#include <vector>

namespace Canape
{
    namespace
    {
        using Microsoft::WRL::ComPtr;

        std::string Failure(const char* operation, HRESULT result)
        {
            std::ostringstream stream;
            stream << operation << " failed (HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(result) << ')';
            return stream.str();
        }

        struct alignas(16) FaceConstants
        {
            uint32_t FaceIndex = 0;
            uint32_t SampleCount = 0;
            float Roughness = 0.0f;
            float SourceResolution = 0.0f;
            uint32_t SourceMipCount = 0;
            uint32_t Padding[3]{};
        };
        static_assert(sizeof(FaceConstants) == 32);

        struct alignas(16) BRDFConstants
        {
            uint32_t SampleCount = 0;
            uint32_t Padding[3]{};
        };
        static_assert(sizeof(BRDFConstants) == 16);

        // Save only the immediate-context state touched by the bake pass.
        struct SavedState
        {
            explicit SavedState(ID3D11DeviceContext& context) : Context(context)
            {
                ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
                ID3D11DepthStencilView* dsv = nullptr;
                Context.OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, &dsv);
                for (size_t i = 0; i < Targets.size(); ++i) Targets[i].Attach(rtvs[i]);
                DepthTarget.Attach(dsv);
                ViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
                Context.RSGetViewports(&ViewportCount, Viewports.data());
                ID3D11RasterizerState* rasterizer = nullptr;
                Context.RSGetState(&rasterizer);
                Rasterizer.Attach(rasterizer);
                ID3D11BlendState* blend = nullptr;
                Context.OMGetBlendState(&blend, BlendFactor, &SampleMask);
                Blend.Attach(blend);
                ID3D11DepthStencilState* depth = nullptr;
                Context.OMGetDepthStencilState(&depth, &StencilRef);
                Depth.Attach(depth);
                ID3D11InputLayout* layout = nullptr;
                Context.IAGetInputLayout(&layout);
                Layout.Attach(layout);
                Context.IAGetPrimitiveTopology(&Topology);
                ID3D11VertexShader* vs = nullptr;
                ID3D11PixelShader* ps = nullptr;
                ID3D11GeometryShader* gs = nullptr;
                ID3D11HullShader* hs = nullptr;
                ID3D11DomainShader* ds = nullptr;
                Context.VSGetShader(&vs, nullptr, nullptr);
                Context.PSGetShader(&ps, nullptr, nullptr);
                Context.GSGetShader(&gs, nullptr, nullptr);
                Context.HSGetShader(&hs, nullptr, nullptr);
                Context.DSGetShader(&ds, nullptr, nullptr);
                VS.Attach(vs); PS.Attach(ps); GS.Attach(gs); HS.Attach(hs); DS.Attach(ds);
                ID3D11ShaderResourceView* source = nullptr;
                ID3D11SamplerState* sampler = nullptr;
                ID3D11Buffer* constants = nullptr;
                Context.PSGetShaderResources(0, 1, &source);
                Context.PSGetSamplers(0, 1, &sampler);
                Context.PSGetConstantBuffers(0, 1, &constants);
                Source.Attach(source);
                Sampler.Attach(sampler);
                Constants.Attach(constants);
            }

            ~SavedState()
            {
                ID3D11ShaderResourceView* nullView = nullptr;
                Context.PSSetShaderResources(0, 1, &nullView);
                ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
                for (size_t i = 0; i < Targets.size(); ++i) rtvs[i] = Targets[i].Get();
                Context.OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, rtvs, DepthTarget.Get());
                Context.RSSetViewports(ViewportCount, ViewportCount ? Viewports.data() : nullptr);
                Context.RSSetState(Rasterizer.Get());
                Context.OMSetBlendState(Blend.Get(), BlendFactor, SampleMask);
                Context.OMSetDepthStencilState(Depth.Get(), StencilRef);
                Context.IASetInputLayout(Layout.Get());
                Context.IASetPrimitiveTopology(Topology);
                Context.VSSetShader(VS.Get(), nullptr, 0);
                Context.PSSetShader(PS.Get(), nullptr, 0);
                Context.GSSetShader(GS.Get(), nullptr, 0);
                Context.HSSetShader(HS.Get(), nullptr, 0);
                Context.DSSetShader(DS.Get(), nullptr, 0);
                ID3D11ShaderResourceView* source = Source.Get();
                ID3D11SamplerState* sampler = Sampler.Get();
                ID3D11Buffer* constants = Constants.Get();
                Context.PSSetShaderResources(0, 1, &source);
                Context.PSSetSamplers(0, 1, &sampler);
                Context.PSSetConstantBuffers(0, 1, &constants);
            }

            SavedState(const SavedState&) = delete;
            SavedState& operator=(const SavedState&) = delete;

            ID3D11DeviceContext& Context;
            std::array<ComPtr<ID3D11RenderTargetView>, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> Targets;
            ComPtr<ID3D11DepthStencilView> DepthTarget;
            std::array<D3D11_VIEWPORT, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE> Viewports{};
            UINT ViewportCount = 0;
            ComPtr<ID3D11RasterizerState> Rasterizer;
            ComPtr<ID3D11BlendState> Blend;
            FLOAT BlendFactor[4]{};
            UINT SampleMask = UINT32_MAX;
            ComPtr<ID3D11DepthStencilState> Depth;
            UINT StencilRef = 0;
            ComPtr<ID3D11InputLayout> Layout;
            D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
            ComPtr<ID3D11VertexShader> VS;
            ComPtr<ID3D11PixelShader> PS;
            ComPtr<ID3D11GeometryShader> GS;
            ComPtr<ID3D11HullShader> HS;
            ComPtr<ID3D11DomainShader> DS;
            ComPtr<ID3D11ShaderResourceView> Source;
            ComPtr<ID3D11SamplerState> Sampler;
            ComPtr<ID3D11Buffer> Constants;
        };
    }

    bool DX11IBLBaker::Initialize(ID3D11Device* device, ID3D11VertexShader* fullscreenVS,
        std::string& error)
    {
        error.clear();
        if (!device || !fullscreenVS)
        {
            error = "IBL baker requires a device and fullscreen vertex shader";
            return false;
        }
        DX11IBLBaker pending;
        pending.Device = device;
        pending.FullscreenVS = fullscreenVS;
        ComPtr<ID3DBlob> bytecode;
        if (!CompileShaderFile(L"Shaders/IBLIrradiance.hlsl", "PSIrradiance", "ps_5_0",
            nullptr, D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, bytecode, error)) return false;
        HRESULT result = device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
            nullptr, &pending.IrradiancePS);
        if (FAILED(result)) { error = Failure("CreatePixelShader(irradiance)", result); return false; }
        bytecode.Reset();
        if (!CompileShaderFile(L"Shaders/IBLPrefilter.hlsl", "PSPrefilter", "ps_5_0",
            nullptr, D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, bytecode, error)) return false;
        result = device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
            nullptr, &pending.PrefilterPS);
        if (FAILED(result)) { error = Failure("CreatePixelShader(prefilter)", result); return false; }
        bytecode.Reset();
        if (!CompileShaderFile(L"Shaders/IBLBRDFIntegration.hlsl", "PSBRDFIntegration", "ps_5_0",
            nullptr, D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, bytecode, error)) return false;
        result = device->CreatePixelShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(),
            nullptr, &pending.BRDFPS);
        if (FAILED(result)) { error = Failure("CreatePixelShader(BRDF LUT)", result); return false; }
        D3D11_SAMPLER_DESC sampler{};
        sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler.MaxLOD = D3D11_FLOAT32_MAX;
        result = device->CreateSamplerState(&sampler, &pending.EnvironmentSampler);
        if (FAILED(result)) { error = Failure("CreateSamplerState(irradiance)", result); return false; }
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = sizeof(FaceConstants);
        buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        result = device->CreateBuffer(&buffer, nullptr, &pending.FaceBuffer);
        if (FAILED(result)) { error = Failure("CreateBuffer(irradiance face)", result); return false; }
        buffer.ByteWidth = sizeof(BRDFConstants);
        result = device->CreateBuffer(&buffer, nullptr, &pending.BRDFBuffer);
        if (FAILED(result)) { error = Failure("CreateBuffer(BRDF LUT)", result); return false; }
        D3D11_RASTERIZER_DESC rasterizer{};
        rasterizer.FillMode = D3D11_FILL_SOLID;
        rasterizer.CullMode = D3D11_CULL_NONE;
        rasterizer.DepthClipEnable = TRUE;
        result = device->CreateRasterizerState(&rasterizer, &pending.Rasterizer);
        if (FAILED(result)) { error = Failure("CreateRasterizerState(irradiance)", result); return false; }
        D3D11_DEPTH_STENCIL_DESC depth{};
        depth.DepthEnable = FALSE;
        depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        result = device->CreateDepthStencilState(&depth, &pending.NoDepth);
        if (FAILED(result)) { error = Failure("CreateDepthStencilState(irradiance)", result); return false; }
        *this = std::move(pending);
        return true;
    }

    void DX11IBLBaker::BindFullscreenPass(ID3D11DeviceContext* context,
        ID3D11PixelShader* pixelShader) const
    {
        context->RSSetState(Rasterizer.Get());
        context->OMSetBlendState(nullptr, nullptr, UINT32_MAX);
        context->OMSetDepthStencilState(NoDepth.Get(), 0);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(FullscreenVS.Get(), nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->HSSetShader(nullptr, nullptr, 0);
        context->DSSetShader(nullptr, nullptr, 0);
    }

    void DX11IBLBaker::BindCubePass(ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* environment, ID3D11PixelShader* pixelShader) const
    {
        BindFullscreenPass(context, pixelShader);
        ID3D11SamplerState* sampler = EnvironmentSampler.Get();
        ID3D11Buffer* constants = FaceBuffer.Get();
        // Detach any render target using the environment before binding its SRV.
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->PSSetShaderResources(0, 1, &environment);
        context->PSSetSamplers(0, 1, &sampler);
        context->PSSetConstantBuffers(0, 1, &constants);
    }

    bool DX11IBLBaker::BakeIrradiance(ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* environment, DX11IrradianceCubemap& output,
        std::string& error, uint32_t sampleCount) const
    {
        error.clear();
        if (!Device || !IrradiancePS || !context || !environment || sampleCount == 0 || sampleCount > 4096)
        {
            error = "IBL baker is uninitialized or received invalid input/sample count";
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC inputView{};
        environment->GetDesc(&inputView);
        if (inputView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURECUBE)
        {
            error = "Irradiance input must be a cubemap SRV";
            return false;
        }
        ComPtr<ID3D11Resource> inputResource;
        environment->GetResource(&inputResource);
        ComPtr<ID3D11Device> inputDevice;
        inputResource->GetDevice(&inputDevice);
        if (inputDevice.Get() != Device.Get())
        {
            error = "Irradiance input belongs to a different D3D11 device";
            return false;
        }
        ComPtr<ID3D11Device> contextDevice;
        context->GetDevice(&contextDevice);
        if (contextDevice.Get() != Device.Get())
        {
            error = "Irradiance context belongs to a different D3D11 device";
            return false;
        }
        UINT support = 0;
        HRESULT result = Device->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT, &support);
        if (FAILED(result) || !(support & D3D11_FORMAT_SUPPORT_TEXTURECUBE)
            || !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)
            || !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        {
            error = "FP16 irradiance cubemap render target/SRV is unsupported";
            return false;
        }
        DX11IrradianceCubemap pending;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = IrradianceFaceSize;
        desc.MipLevels = 1;
        desc.ArraySize = 6;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        result = Device->CreateTexture2D(&desc, nullptr, &pending.Texture);
        if (FAILED(result)) { error = Failure("CreateTexture2D(irradiance)", result); return false; }
        std::array<ComPtr<ID3D11RenderTargetView>, 6> faceViews;
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = desc.Format;
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv.Texture2DArray.MipSlice = 0;
        rtv.Texture2DArray.ArraySize = 1;
        for (UINT face = 0; face < 6; ++face)
        {
            rtv.Texture2DArray.FirstArraySlice = face;
            result = Device->CreateRenderTargetView(pending.Texture.Get(), &rtv, &faceViews[face]);
            if (FAILED(result)) { error = Failure("CreateRenderTargetView(irradiance face)", result); return false; }
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MipLevels = 1;
        result = Device->CreateShaderResourceView(pending.Texture.Get(), &srv, &pending.ShaderResourceView);
        if (FAILED(result)) { error = Failure("CreateShaderResourceView(irradiance)", result); return false; }

        SavedState restore(*context);
        D3D11_VIEWPORT viewport{};
        viewport.Width = viewport.Height = static_cast<float>(IrradianceFaceSize);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        BindCubePass(context, environment, IrradiancePS.Get());
        for (UINT face = 0; face < 6; ++face)
        {
            ID3D11RenderTargetView* target = faceViews[face].Get();
            context->OMSetRenderTargets(1, &target, nullptr);
            const FaceConstants data{face, sampleCount};
            context->UpdateSubresource(FaceBuffer.Get(), 0, nullptr, &data, 0, 0);
            context->Draw(3, 0);
        }
        output = std::move(pending);
        return true;
    }

    bool DX11IBLBaker::BakePrefilteredEnvironment(ID3D11DeviceContext* context,
        ID3D11ShaderResourceView* environment, DX11PrefilteredCubemap& output,
        std::string& error, uint32_t sampleCount, uint32_t faceSize, uint32_t mipLevels) const
    {
        error.clear();
        uint32_t maximumMips = 0;
        for (uint32_t size = faceSize; size != 0; size >>= 1) ++maximumMips;
        if (!Device || !PrefilterPS || !context || !environment || sampleCount == 0
            || sampleCount > 4096 || faceSize == 0 || faceSize > D3D11_REQ_TEXTURECUBE_DIMENSION
            || mipLevels == 0 || mipLevels > maximumMips)
        {
            error = "IBL prefilter requires valid device, cubemap, samples, face size and mip count";
            return false;
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC inputView{};
        environment->GetDesc(&inputView);
        if (inputView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURECUBE)
        {
            error = "Prefilter input must be a cubemap SRV";
            return false;
        }
        ComPtr<ID3D11Resource> inputResource;
        environment->GetResource(&inputResource);
        ComPtr<ID3D11Texture2D> inputTexture;
        if (FAILED(inputResource.As(&inputTexture)))
        {
            error = "Prefilter input must reference a Texture2D cubemap";
            return false;
        }
        ComPtr<ID3D11Device> inputDevice;
        inputResource->GetDevice(&inputDevice);
        ComPtr<ID3D11Device> contextDevice;
        context->GetDevice(&contextDevice);
        if (inputDevice.Get() != Device.Get() || contextDevice.Get() != Device.Get())
        {
            error = "Prefilter input and context must belong to the baker device";
            return false;
        }
        D3D11_TEXTURE2D_DESC inputDesc{};
        inputTexture->GetDesc(&inputDesc);
        const uint32_t firstMip = inputView.TextureCube.MostDetailedMip;
        if (firstMip >= inputDesc.MipLevels)
        {
            error = "Prefilter input SRV has no readable mip";
            return false;
        }
        const uint32_t remainingMips = inputDesc.MipLevels - firstMip;
        const uint32_t sourceMipCount = inputView.TextureCube.MipLevels == UINT(-1)
            ? remainingMips : (inputView.TextureCube.MipLevels < remainingMips
                ? inputView.TextureCube.MipLevels : remainingMips);
        if (sourceMipCount == 0)
        {
            error = "Prefilter input SRV has no readable mip";
            return false;
        }
        const float sourceResolution = static_cast<float>(
            (inputDesc.Width >> firstMip) > 0 ? (inputDesc.Width >> firstMip) : 1);
        UINT support = 0;
        HRESULT result = Device->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT, &support);
        if (FAILED(result) || !(support & D3D11_FORMAT_SUPPORT_TEXTURECUBE)
            || !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)
            || !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        {
            error = "FP16 prefiltered cubemap render target/SRV is unsupported";
            return false;
        }

        DX11PrefilteredCubemap pending;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = faceSize;
        desc.MipLevels = mipLevels;
        desc.ArraySize = 6;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
        result = Device->CreateTexture2D(&desc, nullptr, &pending.Texture);
        if (FAILED(result)) { error = Failure("CreateTexture2D(prefilter)", result); return false; }

        std::vector<ComPtr<ID3D11RenderTargetView>> faceViews(size_t(6) * mipLevels);
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = desc.Format;
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
        rtv.Texture2DArray.ArraySize = 1;
        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            rtv.Texture2DArray.MipSlice = mip;
            for (uint32_t face = 0; face < 6; ++face)
            {
                rtv.Texture2DArray.FirstArraySlice = face;
                result = Device->CreateRenderTargetView(pending.Texture.Get(), &rtv,
                    &faceViews[size_t(mip) * 6 + face]);
                if (FAILED(result)) { error = Failure("CreateRenderTargetView(prefilter face/mip)", result); return false; }
            }
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
        srv.Format = desc.Format;
        srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MipLevels = mipLevels;
        result = Device->CreateShaderResourceView(pending.Texture.Get(), &srv, &pending.ShaderResourceView);
        if (FAILED(result)) { error = Failure("CreateShaderResourceView(prefilter)", result); return false; }

        SavedState restore(*context);
        BindCubePass(context, environment, PrefilterPS.Get());
        D3D11_VIEWPORT viewport{};
        viewport.MaxDepth = 1.0f;
        for (uint32_t mip = 0; mip < mipLevels; ++mip)
        {
            const uint32_t mipSize = (faceSize >> mip) > 0 ? (faceSize >> mip) : 1;
            viewport.Width = viewport.Height = static_cast<float>(mipSize);
            context->RSSetViewports(1, &viewport);
            const float roughness = mipLevels > 1
                ? static_cast<float>(mip) / static_cast<float>(mipLevels - 1) : 0.0f;
            for (uint32_t face = 0; face < 6; ++face)
            {
                ID3D11RenderTargetView* target = faceViews[size_t(mip) * 6 + face].Get();
                context->OMSetRenderTargets(1, &target, nullptr);
                const FaceConstants data{face, sampleCount, roughness, sourceResolution, sourceMipCount};
                context->UpdateSubresource(FaceBuffer.Get(), 0, nullptr, &data, 0, 0);
                context->Draw(3, 0);
            }
        }
        output = std::move(pending);
        return true;
    }

    bool DX11IBLBaker::BakeBRDFLUT(ID3D11DeviceContext* context, DX11BRDFLUT& output,
        std::string& error, uint32_t sampleCount, uint32_t size) const
    {
        error.clear();
        if (!Device || !BRDFPS || !context || sampleCount == 0 || sampleCount > 4096
            || size == 0 || size > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
        {
            error = "BRDF LUT requires a valid device, context, sample count and size";
            return false;
        }
        ComPtr<ID3D11Device> contextDevice;
        context->GetDevice(&contextDevice);
        if (contextDevice.Get() != Device.Get())
        {
            error = "BRDF LUT context belongs to a different D3D11 device";
            return false;
        }
        UINT support = 0;
        HRESULT result = Device->CheckFormatSupport(DXGI_FORMAT_R16G16_FLOAT, &support);
        if (FAILED(result) || !(support & D3D11_FORMAT_SUPPORT_TEXTURE2D)
            || !(support & D3D11_FORMAT_SUPPORT_RENDER_TARGET)
            || !(support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE))
        {
            error = "RG16 Float BRDF LUT render target/SRV is unsupported";
            return false;
        }

        DX11BRDFLUT pending;
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = size;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R16G16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        result = Device->CreateTexture2D(&desc, nullptr, &pending.Texture);
        if (FAILED(result)) { error = Failure("CreateTexture2D(BRDF LUT)", result); return false; }
        ComPtr<ID3D11RenderTargetView> target;
        result = Device->CreateRenderTargetView(pending.Texture.Get(), nullptr, &target);
        if (FAILED(result)) { error = Failure("CreateRenderTargetView(BRDF LUT)", result); return false; }
        result = Device->CreateShaderResourceView(pending.Texture.Get(), nullptr,
            &pending.ShaderResourceView);
        if (FAILED(result)) { error = Failure("CreateShaderResourceView(BRDF LUT)", result); return false; }

        SavedState restore(*context);
        D3D11_VIEWPORT viewport{};
        viewport.Width = viewport.Height = static_cast<float>(size);
        viewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &viewport);
        BindFullscreenPass(context, BRDFPS.Get());
        ID3D11ShaderResourceView* nullView = nullptr;
        context->PSSetShaderResources(0, 1, &nullView);
        ID3D11RenderTargetView* rtv = target.Get();
        context->OMSetRenderTargets(1, &rtv, nullptr);
        const BRDFConstants data{sampleCount};
        context->UpdateSubresource(BRDFBuffer.Get(), 0, nullptr, &data, 0, 0);
        ID3D11Buffer* constants = BRDFBuffer.Get();
        context->PSSetConstantBuffers(0, 1, &constants);
        context->Draw(3, 0);
        output = std::move(pending);
        return true;
    }
}
