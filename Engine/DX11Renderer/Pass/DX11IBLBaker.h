#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>

namespace Canape
{
    struct DX11IrradianceCubemap
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
    };

    struct DX11PrefilteredCubemap
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
    };

    struct DX11BRDFLUT
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> Texture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> ShaderResourceView;
    };

    // GPU preprocessing only. The caller owns the result and chooses when to bake.
    // Reuses the fullscreen VS from DX11PipelineState (FinalOutput) and retains it.
    class DX11IBLBaker
    {
    public:
        static constexpr uint32_t IrradianceFaceSize = 32;
        static constexpr uint32_t PrefilterFaceSize = 128;
        static constexpr uint32_t PrefilterMipLevels = 5;
        static constexpr uint32_t BRDFLUTSize = 256;

        bool Initialize(ID3D11Device* device, ID3D11VertexShader* fullscreenVS, std::string& error);
        bool BakeIrradiance(ID3D11DeviceContext* context, ID3D11ShaderResourceView* environment,
            DX11IrradianceCubemap& output, std::string& error, uint32_t sampleCount = 256) const;
        bool BakePrefilteredEnvironment(ID3D11DeviceContext* context, ID3D11ShaderResourceView* environment,
            DX11PrefilteredCubemap& output, std::string& error, uint32_t sampleCount = 256,
            uint32_t faceSize = PrefilterFaceSize, uint32_t mipLevels = PrefilterMipLevels) const;
        bool BakeBRDFLUT(ID3D11DeviceContext* context, DX11BRDFLUT& output,
            std::string& error, uint32_t sampleCount = 256, uint32_t size = BRDFLUTSize) const;

    private:
        void BindFullscreenPass(ID3D11DeviceContext* context, ID3D11PixelShader* pixelShader) const;
        void BindCubePass(ID3D11DeviceContext* context, ID3D11ShaderResourceView* environment,
            ID3D11PixelShader* pixelShader) const;

        Microsoft::WRL::ComPtr<ID3D11Device> Device;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> FullscreenVS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> IrradiancePS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PrefilterPS;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> BRDFPS;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> EnvironmentSampler;
        Microsoft::WRL::ComPtr<ID3D11Buffer> FaceBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer> BRDFBuffer;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> Rasterizer;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> NoDepth;
    };
}
