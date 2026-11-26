// Standalone HLSL validation. No renderer pipeline or vertex-format changes.
#include "DX11Renderer/Pipeline/DX11ShaderCompiler.h"
#include "CNDebug/DebugLog.h"
#include <d3d11.h>
#include <d3d11shader.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace Canape::Debug
{
    void Write(MainTag, std::string_view, std::string message, const char*, int)
    {
        std::cerr << message << '\n';
    }
}

using Microsoft::WRL::ComPtr;
using namespace DirectX;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void Check(HRESULT result)
{
    Require(SUCCEEDED(result), "D3D operation failed");
}

ComPtr<ID3DBlob> Compile(const wchar_t* path, const char* entry, const char* target,
    const D3D_SHADER_MACRO* macros = nullptr)
{
    ComPtr<ID3DBlob> code;
    std::string error;
    Require(Canape::CompileShaderFile(path, entry, target, macros,
        D3DCOMPILE_PACK_MATRIX_ROW_MAJOR | D3DCOMPILE_WARNINGS_ARE_ERRORS,
        code, error), "Shader compilation failed");
    return code;
}

ComPtr<ID3D11ShaderReflection> Reflect(ID3DBlob* code)
{
    ComPtr<ID3D11ShaderReflection> reflection;
    Check(D3DReflect(code->GetBufferPointer(), code->GetBufferSize(),
        IID_ID3D11ShaderReflection, &reflection));
    return reflection;
}

void CheckInterface(ID3DBlob* vs, ID3DBlob* ps)
{
    auto vertex = Reflect(vs);
    auto pixel = Reflect(ps);
    D3D11_SHADER_DESC vd{}, pd{};
    Check(vertex->GetDesc(&vd));
    Check(pixel->GetDesc(&pd));
    for (UINT i = 0; i < pd.InputParameters; ++i)
    {
        D3D11_SIGNATURE_PARAMETER_DESC input{};
        Check(pixel->GetInputParameterDesc(i, &input));
        // The rasterizer supplies SV_IsFrontFace directly to the pixel shader.
        if (input.SystemValueType == D3D_NAME_IS_FRONT_FACE) continue;
        bool found = false;
        for (UINT j = 0; j < vd.OutputParameters; ++j)
        {
            D3D11_SIGNATURE_PARAMETER_DESC output{};
            Check(vertex->GetOutputParameterDesc(j, &output));
            if (std::strcmp(input.SemanticName, output.SemanticName) == 0
                && input.SemanticIndex == output.SemanticIndex)
            {
                found = input.Register == output.Register
                    && input.ComponentType == output.ComponentType
                    && (output.Mask & input.Mask) == input.Mask;
                break;
            }
        }
        Require(found, "VS output / PS input mismatch");
    }
}

struct Vertex
{
    XMFLOAT3 Position{ 1.25f, -2.5f, 0.75f };
    XMFLOAT3 Normal{ 0.6f, 0.0f, 0.8f };
    XMFLOAT2 UV{ 0.3f, 0.7f };
    XMFLOAT4 Tangent{ 0.8f, 0.0f, -0.6f, -1.0f };
    uint16_t Indices[4]{};
    float Weights[4]{};
    XMFLOAT2 UV1{ 0.8f, 0.2f };
};
static_assert(sizeof(Vertex) == 80);

struct Output
{
    XMFLOAT4 Position;
    XMFLOAT3 WorldPosition;
    XMFLOAT3 Normal;
    XMFLOAT2 UV;
    XMFLOAT4 Tangent;
    XMFLOAT2 UV1;
};
static_assert(sizeof(Output) == 72);

int main()
{
    try
    {
        auto staticCode = Compile(L"Shaders/MeshStaticVS.hlsl", "VSMain", "vs_5_0");
        auto skinnedCode = Compile(L"Shaders/MeshSkinnedVS.hlsl", "VSMain", "vs_5_0");
        auto pixelCode = Compile(L"Shaders/MeshPS.hlsl", "PSMain", "ps_5_0");
        CheckInterface(staticCode.Get(), pixelCode.Get());
        CheckInterface(skinnedCode.Get(), pixelCode.Get());
        const D3D_SHADER_MACRO staticDefines[] = { { "STATIC_VERTEX", "1" }, { nullptr, nullptr } };
        auto legacyPosition = Compile(L"Shaders/Mesh.hlsl", "VSMain", "vs_5_0");
        auto legacyStatic = Compile(L"Shaders/Mesh.hlsl", "VSMain", "vs_5_0", staticDefines);
        auto legacyPixel = Compile(L"Shaders/Mesh.hlsl", "PSMain", "ps_5_0");
        CheckInterface(legacyPosition.Get(), legacyPixel.Get());
        CheckInterface(legacyStatic.Get(), legacyPixel.Get());
        auto skinReflection = Reflect(skinnedCode.Get());
        D3D11_SHADER_INPUT_BIND_DESC binding{};
        Check(skinReflection->GetResourceBindingDescByName("SkinningBuffer", &binding));
        Require(binding.BindPoint == 9, "SkinningBuffer must use b9");
        auto skinConstants = skinReflection->GetConstantBufferByName("SkinningBuffer");
        D3D11_SHADER_BUFFER_DESC bufferDesc{};
        Check(skinConstants->GetDesc(&bufferDesc));
        Require(bufferDesc.Size == 16384, "Palette byte size");
        D3D11_SHADER_TYPE_DESC matrixDesc{};
        Check(skinConstants->GetVariableByName("SkinMatrices")->GetType()->GetDesc(&matrixDesc));
        Require(matrixDesc.Class == D3D_SVC_MATRIX_ROWS && matrixDesc.Elements == 256,
            "Palette must contain 256 row-major matrices");

        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        Check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context));
        auto makeBuffer = [&](UINT size, UINT bind, const void* data)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = size;
            desc.BindFlags = bind;
            D3D11_SUBRESOURCE_DATA initial{ data, 0, 0 };
            ComPtr<ID3D11Buffer> buffer;
            Check(device->CreateBuffer(&desc, data ? &initial : nullptr, &buffer));
            return buffer;
        };

        ComPtr<ID3D11VertexShader> staticVS, skinnedVS;
        Check(device->CreateVertexShader(staticCode->GetBufferPointer(), staticCode->GetBufferSize(), nullptr, &staticVS));
        Check(device->CreateVertexShader(skinnedCode->GetBufferPointer(), skinnedCode->GetBufferSize(), nullptr, &skinnedVS));
        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 72, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDINDICES", 0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };
        ComPtr<ID3D11InputLayout> staticLayout, skinnedLayout;
        Check(device->CreateInputLayout(elements, 5, staticCode->GetBufferPointer(), staticCode->GetBufferSize(), &staticLayout));
        Check(device->CreateInputLayout(elements, 7, skinnedCode->GetBufferPointer(), skinnedCode->GetBufferSize(), &skinnedLayout));

        // Capture actual VS output without a rasterizer, material, or engine draw path.
        const char* gsSource =
            "#include \"Common/MeshTypes.hlsli\"\n"
            "[maxvertexcount(1)] void GSMain(point VertexOutput v[1], inout PointStream<VertexOutput> s) { s.Append(v[0]); }";
        ComPtr<ID3DBlob> gsCode, diagnostics;
        Check(D3DCompile(gsSource, std::strlen(gsSource), "Shaders/Capture.hlsl", nullptr,
            D3D_COMPILE_STANDARD_FILE_INCLUDE, "GSMain", "gs_5_0", 0, 0, &gsCode, &diagnostics));
        const D3D11_SO_DECLARATION_ENTRY declarations[] = {
            { 0, "SV_POSITION", 0, 0, 4, 0 }, { 0, "WORLDPOS", 0, 0, 3, 0 },
            { 0, "NORMAL", 0, 0, 3, 0 }, { 0, "TEXCOORD", 0, 0, 2, 0 },
            { 0, "TANGENT", 0, 0, 4, 0 },
            { 0, "TEXCOORD", 1, 0, 2, 0 },
        };
        UINT outputStride = sizeof(Output);
        ComPtr<ID3D11GeometryShader> capture;
        Check(device->CreateGeometryShaderWithStreamOutput(gsCode->GetBufferPointer(), gsCode->GetBufferSize(),
            declarations, 6, &outputStride, 1, D3D11_SO_NO_RASTERIZED_STREAM, nullptr, &capture));
        context->GSSetShader(capture.Get(), nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);
        auto outputBuffer = makeBuffer(sizeof(Output), D3D11_BIND_STREAM_OUTPUT, nullptr);
        D3D11_BUFFER_DESC stagingDesc{};
        stagingDesc.ByteWidth = sizeof(Output);
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        Check(device->CreateBuffer(&stagingDesc, nullptr, &staging));

        const auto world = XMMatrixScaling(2.0f, 3.0f, 0.5f) * XMMatrixRotationY(0.4f) * XMMatrixTranslation(4, -2, 3);
        const auto inverseTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
        const auto viewProjection = XMMatrixLookAtLH(XMVectorSet(0, 0, -10, 1), XMVectorZero(), XMVectorSet(0, 1, 0, 0))
            * XMMatrixPerspectiveFovLH(1.1f, 1.5f, 0.1f, 100.0f);
        XMFLOAT4X4 objects[2];
        XMStoreFloat4x4(&objects[0], world);
        XMStoreFloat4x4(&objects[1], inverseTranspose);
        struct ViewConstants { XMFLOAT4X4 View, Projection, ViewProjection; XMFLOAT4 Camera; } view{};
        XMStoreFloat4x4(&view.ViewProjection, viewProjection);
        auto objectBuffer = makeBuffer(sizeof(objects), D3D11_BIND_CONSTANT_BUFFER, objects);
        auto viewBuffer = makeBuffer(sizeof(view), D3D11_BIND_CONSTANT_BUFFER, &view);
        context->VSSetConstantBuffers(0, 1, objectBuffer.GetAddressOf());
        context->VSSetConstantBuffers(2, 1, viewBuffer.GetAddressOf());
        std::array<XMFLOAT4X4, 256> palette;
        for (auto& matrix : palette) XMStoreFloat4x4(&matrix, XMMatrixIdentity());
        auto paletteBuffer = makeBuffer(sizeof(palette), D3D11_BIND_CONSTANT_BUFFER, palette.data());
        context->VSSetConstantBuffers(9, 1, paletteBuffer.GetAddressOf());

        auto run = [&](const char* name, const Vertex& vertex, bool skinned, FXMMATRIX skin)
        {
            context->UpdateSubresource(paletteBuffer.Get(), 0, nullptr, palette.data(), 0, 0);
            auto vertices = makeBuffer(sizeof(Vertex), D3D11_BIND_VERTEX_BUFFER, &vertex);
            UINT stride = sizeof(Vertex), offset = 0;
            context->IASetVertexBuffers(0, 1, vertices.GetAddressOf(), &stride, &offset);
            context->IASetInputLayout(skinned ? skinnedLayout.Get() : staticLayout.Get());
            context->VSSetShader(skinned ? skinnedVS.Get() : staticVS.Get(), nullptr, 0);
            context->SOSetTargets(1, outputBuffer.GetAddressOf(), &offset);
            context->Draw(1, 0);
            context->SOSetTargets(0, nullptr, nullptr);
            context->CopyResource(staging.Get(), outputBuffer.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            Check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
            std::array<float, 18> actual{};
            std::memcpy(actual.data(), mapped.pData, sizeof(Output));
            context->Unmap(staging.Get(), 0);
            Output expected{};
            auto position = XMVector4Transform(XMVectorSetW(XMLoadFloat3(&vertex.Position), 1), skin);
            auto normal = XMVector3TransformNormal(XMLoadFloat3(&vertex.Normal), skin);
            auto tangent = XMVector3TransformNormal(XMLoadFloat4(&vertex.Tangent), skin);
            auto worldPosition = XMVector4Transform(XMVectorSetW(position, 1), world);
            XMStoreFloat4(&expected.Position, XMVector4Transform(worldPosition, viewProjection));
            XMStoreFloat3(&expected.WorldPosition, worldPosition);
            XMStoreFloat3(&expected.Normal, XMVector3TransformNormal(normal, inverseTranspose));
            XMStoreFloat4(&expected.Tangent, XMVectorSetW(XMVector3TransformNormal(tangent, world), vertex.Tangent.w));
            expected.UV = vertex.UV;
            expected.UV1 = vertex.UV1;
            std::array<float, 18> reference{};
            std::memcpy(reference.data(), &expected, sizeof(expected));
            for (size_t i = 0; i < actual.size(); ++i)
                Require(std::isfinite(actual[i]) && std::abs(actual[i] - reference[i]) < 1e-4f,
                    "VS output differs from CPU reference");
            std::cout << "PASS: " << name << '\n';
        };

        Vertex vertex;
        run("Static: World/ViewProjection, inverse-transpose, tangent handedness", vertex, false, XMMatrixIdentity());
        vertex.Indices[0] = 0; vertex.Indices[1] = 17; vertex.Indices[2] = 128; vertex.Indices[3] = 255;
        vertex.Weights[0] = 0.1f; vertex.Weights[1] = 0.2f; vertex.Weights[2] = 0.3f; vertex.Weights[3] = 0.4f;
        run("Identity palette, four influences including last entry", vertex, true, XMMatrixIdentity());
        const auto bone = XMMatrixRotationX(0.5f) * XMMatrixRotationZ(0.6f) * XMMatrixTranslation(3, 4, -2);
        XMStoreFloat4x4(&palette[37], bone);
        vertex.Indices[0] = 65535; vertex.Indices[1] = 65535; vertex.Indices[2] = 37; vertex.Indices[3] = 65535;
        vertex.Weights[0] = 0; vertex.Weights[1] = 0; vertex.Weights[2] = 1; vertex.Weights[3] = 0;
        run("Single bone weight 1, rotation/translation, unused invalid indices", vertex, true, bone);
        const auto secondBone = XMMatrixRotationY(-0.8f) * XMMatrixTranslation(-2, 1, 5);
        XMStoreFloat4x4(&palette[255], secondBone);
        vertex.Indices[0] = 37; vertex.Indices[1] = 255;
        vertex.Weights[0] = 1; vertex.Weights[1] = 3; vertex.Weights[2] = 0;
        run("Two bones, weights normalized to 0.25/0.75", vertex, true, bone * 0.25f + secondBone * 0.75f);
        vertex.Weights[0] = 0; vertex.Weights[1] = 0;
        run("Zero weights preserve local surface", vertex, true, XMMatrixIdentity());
        vertex.Weights[0] = -1; vertex.Weights[1] = 2;
        run("Negative weight clamped before normalization", vertex, true, secondBone);

        // Exercise the actual World Normal pixel shader on both rasterized face orientations.
        auto normalCode = Compile(L"Shaders/Debug/MeshDebug.hlsl", "PSDebugWorldNormal", "ps_5_0");
        CheckInterface(staticCode.Get(), normalCode.Get());
        CheckInterface(skinnedCode.Get(), normalCode.Get());
        ComPtr<ID3D11PixelShader> normalPS;
        Check(device->CreatePixelShader(normalCode->GetBufferPointer(), normalCode->GetBufferSize(),
            nullptr, &normalPS));
        context->GSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(normalPS.Get(), nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        XMFLOAT4X4 identity;
        XMStoreFloat4x4(&identity, XMMatrixIdentity());
        XMFLOAT4X4 identityObject[2]{ identity, identity };
        context->UpdateSubresource(objectBuffer.Get(), 0, nullptr, identityObject, 0, 0);
        XMStoreFloat4x4(&view.ViewProjection, XMMatrixIdentity());
        context->UpdateSubresource(viewBuffer.Get(), 0, nullptr, &view, 0, 0);
        for (auto& matrix : palette) matrix = identity;
        context->UpdateSubresource(paletteBuffer.Get(), 0, nullptr, palette.data(), 0, 0);

        struct MaterialConstants
        {
            XMFLOAT4 BaseColor{ 1, 1, 1, 1 };
            uint32_t AlphaMode = 0;
            float AlphaCutoff = 0.5f;
            uint32_t HasBaseColorTexture = 0;
            uint32_t HasNormalTexture = 0;
            float Metallic = 0;
            float Roughness = 1;
            uint32_t HasORMTexture = 0;
            uint32_t HasOcclusionTexture = 0;
            uint32_t HasLightmap = 0;
            uint32_t MaterialPadding[3]{};
        } material;
        static_assert(sizeof(MaterialConstants) == 64);
        auto materialBuffer = makeBuffer(sizeof(material), D3D11_BIND_CONSTANT_BUFFER, &material);
        context->PSSetConstantBuffers(1, 1, materialBuffer.GetAddressOf());

        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = textureDesc.Height = textureDesc.MipLevels = textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const XMFLOAT4 tangentNormal{ 0.7f, 0.3f, 0.9f, 1.0f };
        D3D11_SUBRESOURCE_DATA normalData{ &tangentNormal, sizeof(tangentNormal), 0 };
        ComPtr<ID3D11Texture2D> normalTexture;
        Check(device->CreateTexture2D(&textureDesc, &normalData, &normalTexture));
        ComPtr<ID3D11ShaderResourceView> normalView;
        Check(device->CreateShaderResourceView(normalTexture.Get(), nullptr, &normalView));
        context->PSSetShaderResources(3, 1, normalView.GetAddressOf());
        D3D11_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDesc.AddressU = samplerDesc.AddressV = samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
        ComPtr<ID3D11SamplerState> sampler;
        Check(device->CreateSamplerState(&samplerDesc, &sampler));
        context->PSSetSamplers(0, 1, sampler.GetAddressOf());

        textureDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> target;
        Check(device->CreateTexture2D(&textureDesc, nullptr, &target));
        ComPtr<ID3D11RenderTargetView> targetView;
        Check(device->CreateRenderTargetView(target.Get(), nullptr, &targetView));
        textureDesc.BindFlags = 0;
        textureDesc.Usage = D3D11_USAGE_STAGING;
        textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;
        Check(device->CreateTexture2D(&textureDesc, nullptr, &readback));
        context->OMSetRenderTargets(1, targetView.GetAddressOf(), nullptr);
        D3D11_VIEWPORT viewport{ 0, 0, 1, 1, 0, 1 };
        context->RSSetViewports(1, &viewport);
        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = D3D11_CULL_NONE;
        rasterizerDesc.DepthClipEnable = TRUE;
        ComPtr<ID3D11RasterizerState> rasterizer;
        Check(device->CreateRasterizerState(&rasterizerDesc, &rasterizer));
        context->RSSetState(rasterizer.Get());

        auto drawNormal = [&](bool skinned, bool mapped, bool reversed)
        {
            Vertex triangle[3]{};
            triangle[0].Position = { -1, -1, 0.5f };
            triangle[1].Position = { -1, 3, 0.5f };
            triangle[2].Position = { 3, -1, 0.5f };
            for (auto& corner : triangle)
            {
                corner.Normal = { 0, 0, 1 };
                corner.Tangent = { 1, 0, 0, -1 };
                corner.Weights[0] = 1;
            }
            if (reversed) std::swap(triangle[1], triangle[2]);
            auto triangleBuffer = makeBuffer(sizeof(triangle), D3D11_BIND_VERTEX_BUFFER, triangle);
            UINT triangleStride = sizeof(Vertex), triangleOffset = 0;
            context->IASetVertexBuffers(0, 1, triangleBuffer.GetAddressOf(), &triangleStride, &triangleOffset);
            context->IASetInputLayout(skinned ? skinnedLayout.Get() : staticLayout.Get());
            context->VSSetShader(skinned ? skinnedVS.Get() : staticVS.Get(), nullptr, 0);
            material.HasNormalTexture = mapped ? 1u : 0u;
            context->UpdateSubresource(materialBuffer.Get(), 0, nullptr, &material, 0, 0);
            const float clear[4]{ 2, 2, 2, 2 };
            context->ClearRenderTargetView(targetView.Get(), clear);
            context->Draw(3, 0);
            context->CopyResource(readback.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE pixels{};
            Check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &pixels));
            XMFLOAT4 color;
            std::memcpy(&color, pixels.pData, sizeof(color));
            context->Unmap(readback.Get(), 0);
            return color;
        };
        for (bool skinned : { false, true })
        {
            for (bool mapped : { false, true })
            {
                const XMFLOAT4 first = drawNormal(skinned, mapped, false);
                const XMFLOAT4 back = drawNormal(skinned, mapped, true);
                Require(std::abs(first.x + back.x - 1.0f) < 0.01f
                    && std::abs(first.y + back.y - 1.0f) < 0.01f
                    && std::abs(first.z + back.z - 1.0f) < 0.01f,
                    "World Normal front/back directions did not invert");
                Require(std::abs(first.z - back.z) > 0.5f,
                    "World Normal draw did not distinguish front and back faces");
                const XMFLOAT4& front = first.z > back.z ? first : back;
                if (mapped)
                    Require(front.x > 0.65f && front.y > 0.65f && front.z > 0.85f,
                        "Normal mapping or mirrored tangent handedness was lost");
                else
                    Require(std::abs(front.x - 0.5f) < 0.01f
                        && std::abs(front.y - 0.5f) < 0.01f
                        && std::abs(front.z - 1.0f) < 0.01f,
                        "Unmapped front-face normal changed");
            }
        }
        std::cout << "PASS: World Normal front/back, with/without normal map, static/skinned\n";
        std::cout << "PASS: common VS/PS signatures; b9 size and row-major palette; actual runtime include handler\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
