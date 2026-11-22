#pragma once

struct ID3D11Device;
struct ID3D11DeviceContext;

struct ID3D11Buffer;
struct ID3D11Texture2D;
struct ID3D11ShaderResourceView;
struct ID3D11UnorderedAccessView;
struct ID3D11SamplerState;

struct ID3D11RasterizerState;
struct ID3D11DepthStencilState;
struct ID3D11BlendState;

struct ID3D11VertexShader;
struct ID3D11PixelShader;
struct ID3D11ComputeShader;

namespace Microsoft::WRL
{
    template<typename T>
    class ComPtr;
}

namespace Canape
{
    template<typename T>
    using RefPtr = Microsoft::WRL::ComPtr<T>;

    using DeviceRef = RefPtr<ID3D11Device>;
    using DeviceContextRef = RefPtr<ID3D11DeviceContext>;

    using BufferRef = RefPtr<ID3D11Buffer>;
    using Texture2DRef = RefPtr<ID3D11Texture2D>;
    using ReadViewRef = RefPtr<ID3D11ShaderResourceView>;
    using WriteViewRef = RefPtr<ID3D11UnorderedAccessView>;
    using SamplerStateRef = RefPtr<ID3D11SamplerState>;

    using RasterizerStateRef = RefPtr<ID3D11RasterizerState>;
    using DepthStencilStateRef = RefPtr<ID3D11DepthStencilState>;
    using BlendStateRef = RefPtr<ID3D11BlendState>;

    using VertexShaderRef = RefPtr<ID3D11VertexShader>;
    using PixelShaderRef = RefPtr<ID3D11PixelShader>;
    using ComputeShaderRef = RefPtr<ID3D11ComputeShader>;
}
