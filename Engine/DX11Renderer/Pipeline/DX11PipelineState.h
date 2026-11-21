#pragma once

#include "DX11Renderer/Data/MeshData.h"
#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/RendererDebugSettings.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace Canape
{
    enum class TransparentFace { Both, Back, Front };

    // Borrowed pointers owned by DX11PipelineState. Resolve and bind within the current draw;
    // do not retain selections across initialization or renderer shutdown.
    struct DX11PipelineSelection
    {
        ID3D11VertexShader* VS = nullptr;
        ID3D11PixelShader* PS = nullptr;
        ID3D11InputLayout* InputLayout = nullptr;
        ID3D11RasterizerState* Rasterizer = nullptr;
        ID3D11BlendState* Blend = nullptr;
        ID3D11DepthStencilState* Depth = nullptr;
        ID3D11SamplerState* Sampler = nullptr;
    };

    // The three fixed vertex formats and three existing alpha modes share these objects.
    // No Device/Context ownership, resource registry, or frame execution lives here.
    class DX11PipelineState
    {
    public:
        DX11PipelineState() = default;
        DX11PipelineState(const DX11PipelineState&) = delete;
        DX11PipelineState& operator=(const DX11PipelineState&) = delete;
        DX11PipelineState(DX11PipelineState&&) = default;
        DX11PipelineState& operator=(DX11PipelineState&&) = default;

        bool Initialize(ID3D11Device* device, std::string& error);
        DX11PipelineSelection Resolve(VertexFormat vertexFormat, AlphaMode alphaMode,
            DebugViewMode debugView = DebugViewMode::Final, bool wireframe = false,
            TransparentFace face = TransparentFace::Both) const;
        DX11PipelineSelection ResolveFinalOutput(bool cameraDepth, bool multisampledDepth = false) const;
        DX11PipelineSelection ResolveShadow(VertexFormat vertexFormat, AlphaMode alphaMode) const;
        bool SetShadowRasterizer(ID3D11Device* device, ShadowCullMode cullMode, float slopeBias, std::string& error);
        // Sampler is for PS s2; the skybox pass binds it separately from the generic s0 path.
        DX11PipelineSelection ResolveSkybox() const;
        // Scissor-enabled, depth-off, premultiplied-alpha pipeline for UIVertex geometry.
        // multisampledDepth: pick the pixel shader that reads the camera depth as Texture2DMS.
        DX11PipelineSelection ResolveUI(bool multisampledDepth = false) const;
        bool ShadowsAvailable() const { return ShadowVertexShader && StaticShadowVertexShader && SkinnedShadowVertexShader && MaskedShadowPixelShader && ShadowRasterizerState; }
        bool UIAvailable() const { return UIVertexShader && UIPixelShader && UIInputLayout && UIRasterizerState && UIBlendState && UISampler && UIPixelShaderMS; }

    private:
        bool InitializeShadows(ID3D11Device* device, std::string& error);
        bool InitializeUI(ID3D11Device* device, std::string& error);
        Microsoft::WRL::ComPtr<ID3D11VertexShader> UIVertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> UIPixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> UIInputLayout;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> UIRasterizerState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> UIBlendState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> UISampler;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> UIPixelShaderMS;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> ShadowVertexShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> StaticShadowVertexShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> SkinnedShadowVertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> MaskedShadowPixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> ShadowInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> StaticShadowInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> SkinnedShadowInputLayout;
#if CANAPE_RENDER_DEBUG
        Microsoft::WRL::ComPtr<ID3D11PixelShader> ShadowVisibilityPixelShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PcssFilterRadiusPixelShader;
#endif
        Microsoft::WRL::ComPtr<ID3D11VertexShader> VertexShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> StaticVertexShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> SkinnedVertexShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> FinalOutputVertexShader;
        Microsoft::WRL::ComPtr<ID3D11VertexShader> SkyboxVertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> PixelShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> SkyboxPixelShader;
#if CANAPE_RENDER_DEBUG
        Microsoft::WRL::ComPtr<ID3D11PixelShader> WorldNormalPixelShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> UVPixelShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> MipLODPixelShader;
#endif
        Microsoft::WRL::ComPtr<ID3D11PixelShader> FinalOutputPixelShader;
#if CANAPE_RENDER_DEBUG
        Microsoft::WRL::ComPtr<ID3D11PixelShader> CameraDepthPixelShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader> CameraDepthMSAAPixelShader;
#endif
        Microsoft::WRL::ComPtr<ID3D11InputLayout> InputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> StaticInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> SkinnedInputLayout;
        Microsoft::WRL::ComPtr<ID3D11InputLayout> SkyboxInputLayout;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> RasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> TransparentBackRasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> TransparentFrontRasterizerState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> ShadowRasterizerState;
#if CANAPE_RENDER_DEBUG
        Microsoft::WRL::ComPtr<ID3D11RasterizerState> WireframeRasterizerState;
#endif
        Microsoft::WRL::ComPtr<ID3D11BlendState> OpaqueBlendState;
        Microsoft::WRL::ComPtr<ID3D11BlendState> TransparentBlendState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> OpaqueDepthState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> TransparentDepthState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> FinalOutputDepthState;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> SkyboxDepthState;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> LinearSampler;
        Microsoft::WRL::ComPtr<ID3D11SamplerState> SkyboxSampler;
    };
}
