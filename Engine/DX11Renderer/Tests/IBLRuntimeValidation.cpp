// Backend integration test; private access is limited to this translation unit.
#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/MeshData.h"
#include "DX11Renderer/Data/ShaderConstants.h"
#include "DX11Renderer/Scene/RenderScene.h"
#include <Windows.h>
#include <DirectXPackedVector.h>
#include <d3d11sdklayers.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#define private public
#include "DX11Renderer/DX11Renderer.h"
#undef private
#include "DX11Renderer/Internal/DX11RendererImpl.h"
#include "CNDebug/DebugLog.h"

namespace Canape::Debug
{
uint32_t IBLFailureLogs = 0;
void Write(MainTag, std::string_view tag, std::string message, const char*, int)
{
    if (tag == "IBL") ++IBLFailureLogs;
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

static CubemapResourceId MakeCube(DX11Renderer& renderer, Color color)
{
    constexpr uint32_t size = 4;
    std::vector<float> pixels(size_t(6) * size * size * 4);
    for (size_t i = 0; i < pixels.size(); i += 4)
    {
        pixels[i] = color[0]; pixels[i + 1] = color[1];
        pixels[i + 2] = color[2]; pixels[i + 3] = 1.0f;
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(pixels.data());
    const auto cube = renderer.CreateCubemap(size, 1, TextureFormat::RGBA32_FLOAT,
        std::span<const uint8_t>(bytes, pixels.size() * sizeof(float)));
    Require(renderer.IsCubemapValid(cube), "runtime source cubemap");
    return cube;
}

static CubemapResourceId MakeSpotCube(DX11Renderer& renderer)
{
    constexpr uint32_t size = 32;
    std::vector<float> pixels(size_t(6) * size * size * 4);
    for (uint32_t face = 0; face < 6; ++face)
        for (uint32_t y = 0; y < size; ++y)
            for (uint32_t x = 0; x < size; ++x)
            {
                const float u = 2.0f * (x + .5f) / size - 1.0f;
                const float v = 2.0f * (y + .5f) / size - 1.0f;
                float z = face == 4 ? 1.0f : face == 5 ? -1.0f
                    : face == 0 ? -u : face == 1 ? u : face == 2 ? v : -v;
                const float length = std::sqrt(1.0f + u*u + v*v);
                z /= length;
                const float spot = std::pow(std::clamp((-z - .97f) / .03f, 0.0f, 1.0f), 4.0f);
                const size_t index = ((size_t(face) * size + y) * size + x) * 4;
                pixels[index] = 0.05f + 20.0f * spot;
                pixels[index + 1] = pixels[index + 2] = 0.05f;
                pixels[index + 3] = 1.0f;
            }
    const auto* bytes = reinterpret_cast<const uint8_t*>(pixels.data());
    const auto cube = renderer.CreateCubemap(size, 1, TextureFormat::RGBA32_FLOAT,
        std::span<const uint8_t>(bytes, pixels.size() * sizeof(float)));
    Require(renderer.IsCubemapValid(cube), "spot cubemap");
    return cube;
}

static Color ReadSceneCenter(DX11Renderer::Impl& impl)
{
    D3D11_TEXTURE2D_DESC desc{};
    impl.SceneColorTexture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(impl.Device->CreateTexture2D(&desc, nullptr, &staging)), "scene staging");
    impl.Context->CopyResource(staging.Get(), impl.SceneColorTexture.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map scene color");
    const auto* pixel = reinterpret_cast<const DirectX::PackedVector::HALF*>(
        static_cast<const uint8_t*>(mapped.pData) + size_t(desc.Height / 2) * mapped.RowPitch
        + size_t(desc.Width / 2) * 8);
    Color color{};
    for (uint32_t c = 0; c < 3; ++c)
        color[c] = DirectX::PackedVector::XMConvertHalfToFloat(pixel[c]);
    impl.Context->Unmap(staging.Get(), 0);
    return color;
}

static Color Draw(DX11Renderer& renderer, const RenderScene& scene, bool expectIBL,
    TextureResourceId lightmap = {})
{
    auto& impl = *renderer.m_Impl;
    Require(renderer.BeginFrame(), "BeginFrame");
    renderer.DrawScene(scene);
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> depth;
    impl.Context->OMGetRenderTargets(1, &target, &depth);
    Require(target.Get() == impl.SceneColorRenderTargetView.Get()
        && depth.Get() == impl.DepthStencilView.Get(), "main targets restored after bake");
    UINT count = 1;
    D3D11_VIEWPORT viewport{};
    impl.Context->RSGetViewports(&count, &viewport);
    Require(count == 1 && viewport.Width == impl.Width && viewport.Height == impl.Height,
        "scene viewport restored after bake");
    const auto bindings = impl.GetSceneBindings();
    Require(bindings.IBLAvailable == expectIBL, "scene IBL availability");
    ID3D11ShaderResourceView* views[3]{};
    impl.Context->PSGetShaderResources(6, 3, views);
    for (uint32_t i = 0; i < 3; ++i)
    {
        ID3D11ShaderResourceView* expected = expectIBL
            ? (i == 0 ? bindings.IrradianceView : i == 1 ? bindings.PrefilteredView : bindings.BRDFView)
            : nullptr;
        Require(views[i] == expected, "main pass IBL slots t6-t8");
        if (views[i]) views[i]->Release();
    }
    ComPtr<ID3D11Buffer> boundIBL;
    impl.Context->PSGetConstantBuffers(ShaderConstants::IBLSlot, 1, &boundIBL);
    Require(boundIBL.Get() == impl.IBLBuffer.Get(), "IBL constants b10 bound");
    ComPtr<ID3D11SamplerState> boundBRDFSampler;
    impl.Context->PSGetSamplers(3, 1, &boundBRDFSampler);
    Require(boundBRDFSampler.Get() == impl.Pipelines.ResolveSkybox().Sampler,
        "BRDF clamp sampler s3 bound");
    if (!scene.items.empty() || !scene.skinnedItems.empty())
    {
        ComPtr<ID3D11ShaderResourceView> boundLightmap;
        impl.Context->PSGetShaderResources(ShaderConstants::LightmapTextureSlot, 1, &boundLightmap);
        const auto* expected = impl.Resources.GetTexture(lightmap);
        Require(boundLightmap.Get() == (expected ? expected->ShaderResourceView.Get() : nullptr),
            "lightmap t10 binding or clearing");
        ComPtr<ID3D11Buffer> materialBuffer;
        impl.Context->PSGetConstantBuffers(1, 1, &materialBuffer);
        Require(materialBuffer != nullptr, "material constants bound");
        D3D11_BUFFER_DESC desc{};
        materialBuffer->GetDesc(&desc);
        Require(desc.ByteWidth == sizeof(ShaderConstants::MaterialConstants), "material buffer size");
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Buffer> staging;
        Require(SUCCEEDED(impl.Device->CreateBuffer(&desc, nullptr, &staging)), "material staging");
        impl.Context->CopyResource(staging.Get(), materialBuffer.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "material readback");
        const auto constants = *static_cast<const ShaderConstants::MaterialConstants*>(mapped.pData);
        impl.Context->Unmap(staging.Get(), 0);
        Require(constants.HasLightmap == (expected ? 1u : 0u), "lightmap availability reaches GPU");
    }
    const Color center = ReadSceneCenter(impl);
    renderer.EndFrame();
    return center;
}

static void CheckColor(Color actual, Color expected)
{
    for (uint32_t c = 0; c < 3; ++c)
        Require(std::abs(actual[c] - expected[c]) < .03f, "skybox or SceneColor changed");
}

static Color DrawMaterial(DX11Renderer& renderer, RenderScene& scene,
    const MaterialDesc& desc, TextureResourceId ao = {}, bool expectIBL = true,
    TextureResourceId lightmap = {})
{
    const auto material = renderer.CreateMaterial(desc, {}, {}, {}, ao, {}, lightmap);
    Require(renderer.IsMaterialValid(material), "shading material");
    scene.items[0].material = material;
    const Color color = Draw(renderer, scene, expectIBL, lightmap);
    renderer.DestroyMaterial(material);
    return color;
}

static void ShadingChecks(DX11Renderer& renderer)
{
    auto& impl = *renderer.m_Impl;
    const std::array<StaticVertex, 4> vertices{{
        {{-1,-1,1}, {0,0,-1}, {0,0}}, {{-1,1,1}, {0,0,-1}, {0,1}},
        {{1,1,1}, {0,0,-1}, {1,1}}, {{1,-1,1}, {0,0,-1}, {1,0}} }};
    const std::array<uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    const auto mesh = renderer.CreateMesh(vertices, indices, {});
    Require(renderer.IsMeshValid(mesh), "IBL shading plane");
    RenderScene scene;
    scene.camera.view = Matrix4::LookTo({}, {0, 0, 1});
    scene.camera.projection = Matrix4::Perspective(.95f, 1, .1f, 100);
    MeshItem item;
    item.mesh = mesh;
    item.castShadow = false;
    scene.items.push_back(item);
    scene.ambientIntensity = 0.0f;
    const auto constantCube = MakeCube(renderer, {2.0f, 3.0f, 4.0f});
    scene.skybox.cubemap = constantCube;
    MaterialDesc diffuse;
    diffuse.roughness = .5f;
    const Color diffuseAndSpecular = DrawMaterial(renderer, scene, diffuse);
    const std::array<uint8_t, 4> lightmapPixel{ 255, 0, 255, 255 };
    const auto lightmap = renderer.CreateTexture(1, 1, TextureFormat::RGBA8_UNORM, lightmapPixel);
    Require(renderer.IsTextureValid(lightmap), "lightmap resource");
    Color bakedExpected = diffuseAndSpecular;
    bakedExpected[0] += .96f;
    bakedExpected[2] += .96f;
    CheckColor(DrawMaterial(renderer, scene, diffuse, {}, true, lightmap), bakedExpected);
    CheckColor(DrawMaterial(renderer, scene, diffuse), diffuseAndSpecular);
    renderer.DestroyTexture(lightmap);
    std::cout << "Lightmap: t10 and HasLightmap transmitted/cleared; diffuse added to existing IBL PASS\n";
    const std::array<uint8_t, 4> blackAO{0, 0, 0, 255};
    const auto ao = renderer.CreateTexture(1, 1, TextureFormat::RGBA8_UNORM, blackAO);
    Require(renderer.IsTextureValid(ao), "black AO map");
    const Color specularOnlyDielectric = DrawMaterial(renderer, scene, diffuse, ao);
    MaterialDesc metal = diffuse;
    metal.metallic = 1.0f;
    const Color specularOnlyMetal = DrawMaterial(renderer, scene, metal);
    const Color metalWithAO = DrawMaterial(renderer, scene, metal, ao);
    MaterialDesc blackMetal = metal;
    blackMetal.baseColor = {0, 0, 0, 1};
    const Color blackMetalColor = DrawMaterial(renderer, scene, blackMetal);
    std::cout << "IBL mesh diffuse+spec / dielectric spec / metal spec red: "
        << diffuseAndSpecular[0] << " / " << specularOnlyDielectric[0]
        << " / " << specularOnlyMetal[0] << '\n';
    Require(diffuseAndSpecular[0] > specularOnlyDielectric[0] + 1.0f
        && specularOnlyMetal[0] > .5f && blackMetalColor[0] < .1f,
        "diffuse/specular IBL or metallic energy split");
    for (uint32_t c = 0; c < 3; ++c)
        Require(std::abs(specularOnlyMetal[c] - metalWithAO[c]) < .04f,
            "AO incorrectly attenuated specular IBL");
    scene.ambientColor = {0.0f, 10.0f, 0.0f};
    scene.ambientIntensity = 10.0f;
    const Color noAmbientDuplication = DrawMaterial(renderer, scene, diffuse);
    for (uint32_t c = 0; c < 3; ++c)
        Require(std::abs(noAmbientDuplication[c] - diffuseAndSpecular[c]) < .04f,
            "constant ambient added on top of IBL");
    scene.ambientIntensity = 0.0f;
    scene.skybox.brightness = .5f;
    const Color halfBrightness = DrawMaterial(renderer, scene, diffuse);
    for (uint32_t c = 0; c < 3; ++c)
        Require(std::abs(halfBrightness[c] * 2.0f - diffuseAndSpecular[c]) < .05f,
            "IBL brightness applied incorrectly");
    scene.skybox.brightness = 1.0f;
    const Color frontView = DrawMaterial(renderer, scene, metal);
    scene.camera.cameraPosition = {2.0f, 0.0f, 0.0f};
    const Color grazingView = DrawMaterial(renderer, scene, metal);
    std::cout << "IBL mesh metal front/grazing red: "
        << frontView[0] << " / " << grazingView[0] << '\n';
    Require(std::abs(frontView[0] - grazingView[0]) > .03f,
        "BRDF LUT view-angle response absent");
    scene.camera.cameraPosition = {};

    const auto spotCube = MakeSpotCube(renderer);
    scene.skybox.cubemap = spotCube;
    impl.IBLSettings.PrefilterSamples = 128;
    impl.IBLSettings.PrefilterFaceSize = 32;
    impl.IBLSettings.IrradianceSamples = 128;
    metal.roughness = 0.0f;
    const Color sharp = DrawMaterial(renderer, scene, metal);
    metal.roughness = 1.0f;
    const Color broad = DrawMaterial(renderer, scene, metal);
    Require(sharp[0] > broad[0] + 1.0f, "roughness did not broaden mesh reflection");
    metal.roughness = 0.0f;
    const Color unrotated = DrawMaterial(renderer, scene, metal);
    scene.skybox.rotationRadians = DirectX::XM_PIDIV2;
    const Color rotated = DrawMaterial(renderer, scene, metal);
    std::cout << "IBL mesh spot sharp/broad/rotated red: "
        << sharp[0] << " / " << broad[0] << " / " << rotated[0] << '\n';
    Require(unrotated[0] > rotated[0] + .1f, "IBL rotation did not follow skybox");
    scene.skybox.rotationRadians = 0.0f;

    scene.skybox.cubemap = {};
    scene.ambientColor = {.2f, .4f, .6f};
    scene.ambientIntensity = .5f;
    diffuse.roughness = .5f;
    CheckColor(DrawMaterial(renderer, scene, diffuse, {}, false), {.1f, .2f, .3f});
    const Color fallbackAO = DrawMaterial(renderer, scene, diffuse, ao, false);
    Require(fallbackAO[0] < .02f && fallbackAO[1] < .02f && fallbackAO[2] < .02f,
        "ambient fallback AO");
    const auto failedCube = MakeCube(renderer, {0.0f, 0.0f, 0.0f});
    impl.IBLSettings.PrefilterFaceSize = 0; // Exercise the mesh fallback after a partial bake.
    scene.skybox.cubemap = failedCube;
    CheckColor(DrawMaterial(renderer, scene, diffuse, {}, false), {.1f, .2f, .3f});
    impl.IBLSettings.PrefilterFaceSize = 32;
    renderer.DestroyTexture(ao);
    renderer.DestroyCubemap(failedCube);
    renderer.DestroyCubemap(spotCube);
    renderer.DestroyCubemap(constantCube);
    renderer.DestroyMesh(mesh);
    std::cout << "IBL mesh shading: diffuse, specular, AO, roughness, view, rotation, brightness, fallback PASS\n";
}

static void LightmapShadingChecks(DX11Renderer& renderer)
{
    std::array<StaticVertex, 4> vertices{};
    const std::array<Vector3, 4> positions{
        Vector3{-1,-1,1}, Vector3{-1,1,1}, Vector3{1,1,1}, Vector3{1,-1,1} };
    std::array<SkinnedVertex, 4> skin{};
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        vertices[i].Position = skin[i].Position = positions[i];
        vertices[i].Normal = skin[i].Normal = {0, 0, -1};
        vertices[i].UV = skin[i].UV = {.25f, .5f};
        vertices[i].UV1 = skin[i].UV1 = {.75f, .5f};
        skin[i].Weights[0] = 1;
    }
    const std::array<uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    const auto mesh = renderer.CreateMesh(vertices, indices, {});
    const auto skinned = renderer.CreateMesh(skin, indices, {});
    Require(renderer.IsMeshValid(mesh) && renderer.IsMeshValid(skinned), "lightmap test meshes");
    // UV0 addresses the black texel; UV1 addresses the colored texel.
    const std::array<uint8_t, 8> lightingPixels{0, 0, 0, 255, 255, 128, 64, 255};
    const auto lightmap = renderer.CreateTexture(2, 1, TextureFormat::RGBA8_UNORM, lightingPixels);
    const std::array<uint8_t, 4> black{0, 0, 0, 255};
    const auto blackMap = renderer.CreateTexture(1, 1, TextureFormat::RGBA8_UNORM, black);
    const std::array<uint8_t, 8> basePixels{128, 255, 128, 255, 255, 0, 255, 255};
    const auto baseTexture = renderer.CreateTexture(2, 1, TextureFormat::RGBA8_UNORM, basePixels);
    // A fully metallic descriptor becomes nonmetallic after sampling this ORM B=0.
    const std::array<uint8_t, 4> ormPixels{255, 255, 0, 255};
    const auto orm = renderer.CreateTexture(1, 1, TextureFormat::RGBA8_UNORM, ormPixels);
    Require(renderer.IsTextureValid(lightmap) && renderer.IsTextureValid(blackMap)
        && renderer.IsTextureValid(baseTexture) && renderer.IsTextureValid(orm), "lightmap test maps");

    RenderScene scene;
    scene.camera.view = Matrix4::LookTo({}, {0, 0, 1});
    scene.camera.projection = Matrix4::Perspective(.95f, 1, .1f, 100);
    scene.ambientColor = {.2f, .4f, .6f};
    scene.ambientIntensity = .5f;
    RenderLight light;
    light.id = 1;
    light.type = LightType::Directional;
    light.direction = {0, 0, 1};
    light.intensity = .5f;
    scene.lights.push_back(light);
    scene.bonePalettes = {Matrix4::Identity};
    MaterialDesc tinted;
    tinted.baseColor = {.5f, .25f, .75f, 1};
    tinted.roughness = .5f;
    const Color sample{1, 128.0f / 255, 64.0f / 255};
    const Color diffuse{.96f * tinted.baseColor.X * sample[0],
        .96f * tinted.baseColor.Y * sample[1], .96f * tinted.baseColor.Z * sample[2]};

    for (bool useSkin : {false, true})
    {
        scene.items.clear();
        scene.skinnedItems.clear();
        if (useSkin) scene.skinnedItems.push_back({skinned, 0, {}, Matrix4::Identity, 0, 1, false});
        else scene.items.push_back({mesh, 0, {}, Matrix4::Identity, false});
        auto render = [&](const MaterialDesc& desc, TextureResourceId baked,
            TextureResourceId ao = {}, TextureResourceId base = {}, TextureResourceId packed = {})
        {
            const auto material = renderer.CreateMaterial(desc, base, packed, {}, ao, {}, baked);
            Require(renderer.IsMaterialValid(material), "lightmap shading material");
            if (useSkin) scene.skinnedItems[0].material = material;
            else scene.items[0].material = material;
            const Color color = Draw(renderer, scene, false, baked);
            renderer.DestroyMaterial(material);
            return color;
        };
        auto checkIncrement = [&](const MaterialDesc& desc, TextureResourceId baked, Color expected,
            TextureResourceId ao = {}, TextureResourceId base = {}, TextureResourceId packed = {})
        {
            const Color before = render(desc, {}, ao, base, packed);
            const Color after = render(desc, baked, ao, base, packed);
            for (size_t c = 0; c < 3; ++c)
                Require(std::abs((after[c] - before[c]) - expected[c]) < .02f,
                    "lightmap contribution differs from expected diffuse reflection");
            CheckColor(render(desc, {}, ao, base, packed), before);
        };
        checkIncrement(tinted, lightmap, diffuse); // UV1 and colored diffuse response, additive to direct/ambient.
        checkIncrement(tinted, blackMap, {0, 0, 0});
        checkIncrement(tinted, lightmap, diffuse, blackMap); // Do not apply baked AO twice.
        scene.skybox.brightness = 0;
        checkIncrement(tinted, lightmap, diffuse); // Static baked lighting is independent of skybox brightness.
        scene.skybox.brightness = 1;
        const float half = 128.0f / 255;
        checkIncrement(tinted, lightmap, {diffuse[0] * half, diffuse[1], diffuse[2] * half}, {}, baseTexture);
        MaterialDesc metal = tinted;
        metal.metallic = 1;
        checkIncrement(metal, lightmap, {0, 0, 0});
        checkIncrement(metal, lightmap, diffuse, {}, {}, orm);
        scene.fog.mode = FogMode::Linear;
        scene.fog.startDistance = scene.fog.endDistance = 0;
        scene.fog.color = {.1f, .2f, .3f};
        CheckColor(render(tinted, lightmap), {.1f, .2f, .3f});
        scene.fog.mode = FogMode::Disabled;
    }
    for (const auto texture : {lightmap, blackMap, baseTexture, orm}) renderer.DestroyTexture(texture);
    renderer.DestroyMesh(mesh);
    renderer.DestroyMesh(skinned);
    std::cout << "Lightmap WARP: static/skinned UV1, additive diffuse, base UV0, metal/ORM, AO, fog, defaults PASS\n";
}

int main()
{
    try
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance;
        wc.lpszClassName = L"CanapeIBLRuntimeValidation";
        RegisterClassW(&wc);
        HWND window = CreateWindowExW(0, wc.lpszClassName, L"IBL runtime WARP",
            WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, instance, nullptr);
        Require(window != nullptr, "hidden window");
        {
            DX11Renderer renderer;
            auto& impl = *renderer.m_Impl;
            impl.Window = window;
            impl.ShadowSettings.enabled = false;
            impl.IBLSettings.IrradianceSamples = 8;
            impl.IBLSettings.PrefilterSamples = 16;
            impl.IBLSettings.PrefilterFaceSize = 16;
            impl.IBLSettings.PrefilterMipLevels = 5;
            impl.IBLSettings.BRDFSamples = 16;
            impl.IBLSettings.BRDFSize = 32;
            DXGI_SWAP_CHAIN_DESC swap{};
            swap.BufferDesc.Width = swap.BufferDesc.Height = 64;
            swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swap.SampleDesc.Count = 1;
            swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swap.BufferCount = 2;
            swap.OutputWindow = window;
            swap.Windowed = TRUE;
            const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
            Require(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP,
                nullptr, D3D11_CREATE_DEVICE_DEBUG, &level, 1, D3D11_SDK_VERSION,
                &swap, &impl.SwapChain, &impl.Device, nullptr, &impl.Context)), "WARP debug device");
            ComPtr<ID3D11InfoQueue> queue;
            Require(SUCCEEDED(impl.Device.As(&queue)), "debug info queue");
            Require(impl.CreateTargets(64, 64) && impl.CreatePipeline() && impl.CreateSkyboxQuad(),
                "renderer targets and pipeline");
            impl.Initialized = true;

            const std::array<DX11Renderer::Vertex, 3> vertices{{
                {{-1.0f, -1.0f, 1.0f}}, {{-.5f, -1.0f, 1.0f}}, {{-1.0f, -.5f, 1.0f}} }};
            const std::array<uint32_t, 3> indices{0, 1, 2};
            const auto mesh = renderer.CreateMesh(vertices, indices);
            const auto material = renderer.CreateMaterial(MaterialDesc{});
            Require(renderer.IsMeshValid(mesh) && renderer.IsMaterialValid(material), "test mesh/material");
            RenderScene scene;
            scene.camera.view = Matrix4::LookTo({}, {0, 0, 1});
            scene.camera.projection = Matrix4::Perspective(.95f, 1, .1f, 100);
            MeshItem item;
            item.mesh = mesh;
            item.material = material;
            item.castShadow = false;
            scene.items.push_back(item);
            const Color empty = Draw(renderer, scene, false);
            Require(!impl.BRDFLUT.ShaderResourceView, "LUT remains lazy without skybox");

            const auto cubeA = MakeCube(renderer, {2.0f, 4.0f, 1.5f});
            scene.skybox.cubemap = cubeA;
            CheckColor(Draw(renderer, scene, true), {2.0f, 4.0f, 1.5f});
            Require(impl.EnvironmentIBL.SourceId == cubeA
                && impl.EnvironmentIBL.Irradiance.Texture
                && impl.EnvironmentIBL.Prefiltered.Texture
                && impl.EnvironmentIBL.PrefilterMipCount == 5
                && impl.BRDFLUT.Texture, "first use creates complete IBL set");
            ComPtr<ID3D11Texture2D> firstIrradiance = impl.EnvironmentIBL.Irradiance.Texture;
            ComPtr<ID3D11Texture2D> firstPrefilter = impl.EnvironmentIBL.Prefiltered.Texture;
            ComPtr<ID3D11Texture2D> sharedLUT = impl.BRDFLUT.Texture;
            CheckColor(Draw(renderer, scene, true), {2.0f, 4.0f, 1.5f});
            Require(impl.EnvironmentIBL.Irradiance.Texture == firstIrradiance
                && impl.EnvironmentIBL.Prefiltered.Texture == firstPrefilter
                && impl.BRDFLUT.Texture == sharedLUT, "same HDRI rebaked");
            scene.skybox.rotationRadians = .5f;
            scene.skybox.brightness = .5f;
            Draw(renderer, scene, true);
            Require(impl.EnvironmentIBL.Irradiance.Texture == firstIrradiance
                && impl.EnvironmentIBL.Prefiltered.Texture == firstPrefilter,
                "skybox rotation/brightness triggered rebake");
            scene.skybox.rotationRadians = 0.0f;
            scene.skybox.brightness = 1.0f;

            const auto cubeB = MakeCube(renderer, {4.0f, 2.0f, 1.25f});
            scene.skybox.cubemap = cubeB;
            CheckColor(Draw(renderer, scene, true), {4.0f, 2.0f, 1.25f});
            Require(impl.EnvironmentIBL.SourceId == cubeB
                && impl.EnvironmentIBL.Irradiance.Texture != firstIrradiance
                && impl.EnvironmentIBL.Prefiltered.Texture != firstPrefilter
                && impl.BRDFLUT.Texture == sharedLUT,
                "HDRI switch did not replace environment / reused LUT");
            scene.skybox.cubemap = cubeA;
            Draw(renderer, scene, true);
            Require(impl.EnvironmentIBL.SourceId == cubeA
                && impl.EnvironmentIBL.Irradiance.Texture != firstIrradiance,
                "one-environment cache did not rebake switched-back HDRI");
            firstIrradiance.Reset(); firstPrefilter.Reset();

            scene.skybox.cubemap = {};
            CheckColor(Draw(renderer, scene, false), empty);
            Require(!impl.EnvironmentIBL.SourceId.IsValid() && impl.BRDFLUT.Texture == sharedLUT,
                "empty skybox must clear environment but retain shared LUT");
            scene.skybox.cubemap = cubeB;
            Draw(renderer, scene, true);
            renderer.DestroyCubemap(cubeB);
            Require(!impl.EnvironmentIBL.SourceId.IsValid(), "destroyed cubemap retained IBL");
            CheckColor(Draw(renderer, scene, false), empty);

            const auto cubeFailure = MakeCube(renderer, {3.0f, 2.0f, 1.0f});
            impl.IBLSettings.PrefilterFaceSize = 0; // Test-only failure injection.
            scene.skybox.cubemap = cubeFailure;
            CheckColor(Draw(renderer, scene, false), {3.0f, 2.0f, 1.0f});
            Require(!impl.EnvironmentIBL.SourceId.IsValid()
                && impl.FailedIBLSourceId == cubeFailure && impl.BRDFLUT.Texture == sharedLUT,
                "failed bake exposed partial/stale IBL");
            const uint32_t failures = Debug::IBLFailureLogs;
            Require(failures > 0, "failed bake did not report an error");
            Draw(renderer, scene, false);
            Require(impl.FailedIBLSourceId == cubeFailure && Debug::IBLFailureLogs == failures,
                "failed source reattempted every frame");
            scene.skybox.cubemap = {};
            Draw(renderer, scene, false);
            impl.IBLSettings.PrefilterFaceSize = 16;
            scene.skybox.cubemap = cubeFailure;
            Draw(renderer, scene, true);
            Require(impl.EnvironmentIBL.SourceId == cubeFailure,
                "skybox reset did not allow retry after a failed bake");
            renderer.DestroyCubemap(cubeFailure);
            renderer.DestroyCubemap(cubeA);
            ShadingChecks(renderer);
            LightmapShadingChecks(renderer);
            sharedLUT.Reset();
            impl.Context->ClearState();
            for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i)
            {
                SIZE_T length = 0;
                queue->GetMessage(i, nullptr, &length);
                std::vector<uint8_t> bytes(length);
                auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
                queue->GetMessage(i, message, &length);
                Require(message->Severity > D3D11_MESSAGE_SEVERITY_WARNING,
                    message->pDescription);
            }
            renderer.Shutdown();
            Require(!impl.Device && !impl.Context && !impl.BRDFLUT.Texture
                && !impl.EnvironmentIBL.Irradiance.Texture && !impl.IBLBuffer,
                "renderer shutdown releases IBL");
        }
        DestroyWindow(window);
        std::cout << "IBL runtime WARP: first use, cache, switch, fallback, scene bindings, shutdown PASS\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
