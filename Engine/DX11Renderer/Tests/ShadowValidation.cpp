// Standalone backend integration test; intentionally not part of DX11Renderer.vcxproj.
// The private-access include is confined to this test translation unit. It permits
// GPU readback/failure injection without adding an engine-facing testing interface.
#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/MeshData.h"
#include "DX11Renderer/Data/RendererDebugSettings.h"
#include "DX11Renderer/Data/RendererDebugStats.h"
#include "DX11Renderer/Data/TextureData.h"
#include "DX11Renderer/Scene/RenderScene.h"
#include <array>
#include <cstring>
#include <d3d11sdklayers.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#define private public
#include "DX11Renderer/DX11Renderer.h"
#undef private
#include "DX11Renderer/DX11Renderer.cpp"
namespace Canape::Debug
{
void Write(MainTag, std::string_view tag, std::string message, const char *, int)
{
    if (tag == "Error" || tag == "Shadow")
        std::cerr << message << '\n';
}
} // namespace Canape::Debug
using namespace Canape;
using Backend = DX11Renderer::Impl;
void Require(bool value, const char *message)
{
    if (!value)
        throw std::runtime_error(message);
}
void CpuChecks()
{
    auto lights = CreateTestLights();
    auto packed = Backend::BuildLightConstants(lights);
    Require(packed.DirectionalCount == 1 && packed.LocalCount == 1, "test counts");
    Require(std::abs(packed.DirectionalLights[0].Direction.Length() - 1.0f) < 1e-6f, "normalized direction");
    Require(std::abs(packed.LocalLights[0].ColorIntensity.X - 2.0f) < 1e-6f, "color * intensity");
    std::vector<RenderLight> overflow(100, lights[0]);
    overflow.insert(overflow.end(), 100, lights[1]);
    packed = Backend::BuildLightConstants(overflow);
    Require(packed.DirectionalCount == 4 && packed.LocalCount == 64, "capacity clamp");
    for (const auto &light : packed.DirectionalLights)
        Require(light.ShadowViewCount == 0 && light.ShadowViewOffset == UINT32_MAX, "directional shadow sentinel");
    for (const auto &light : packed.LocalLights)
        Require(light.ShadowViewCount == 0 && light.ShadowViewOffset == UINT32_MAX, "point shadow sentinel");
    packed = Backend::BuildLightConstants({});
    Require(packed.DirectionalCount == 0 && packed.LocalCount == 0, "empty counts");
    for (const auto &light : packed.LocalLights)
        Require(light.ColorIntensity.LengthSquared() == 0, "unused point cleared");
    for (const auto &light : packed.DirectionalLights)
        Require(light.ColorIntensity.LengthSquared() == 0, "unused directional cleared");
    std::array<RenderLight, 6> invalid{lights[0], lights[0], lights[1], lights[1], lights[1], lights[1]};
    invalid[0].direction = {};
    invalid[1].direction.X = std::numeric_limits<float>::infinity();
    invalid[2].range = 0;
    invalid[3].position.Y = std::numeric_limits<float>::quiet_NaN();
    invalid[4].intensity = -1;
    invalid[5].type = LightType::Spot;
    packed = Backend::BuildLightConstants(invalid);
    Require(packed.DirectionalCount == 0 && packed.LocalCount == 0, "invalid lights rejected");
    std::cout << "CPU: test source, normalization, intensity, capacities, empty reset, shadow sentinels, invalid input "
                 "PASS\n";
}
Backend::LightConstants ReadLights(Backend &impl)
{
    D3D11_BUFFER_DESC desc{};
    impl.LightBuffer->GetDesc(&desc);
    Require(desc.ByteWidth == 4240, "GPU layout byte size");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    Require(SUCCEEDED(impl.Device->CreateBuffer(&desc, nullptr, &staging)), "staging light buffer");
    impl.Context->CopyResource(staging.Get(), impl.LightBuffer.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "map light buffer");
    const auto result = *static_cast<const Backend::LightConstants *>(mapped.pData);
    impl.Context->Unmap(staging.Get(), 0);
    return result;
}
struct Image
{
    UINT width, height;
    std::vector<unsigned char> rgba;
};
Image ReadImage(Backend &impl, ID3D11Texture2D *texture)
{
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(impl.Device->CreateTexture2D(&desc, nullptr, &staging)), "image staging");
    impl.Context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE map{};
    Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "image map");
    Image out{desc.Width, desc.Height, std::vector<unsigned char>(desc.Width * desc.Height * 4)};
    for (UINT row = 0; row < desc.Height; ++row)
        std::memcpy(out.rgba.data() + row * desc.Width * 4, static_cast<const char *>(map.pData) + row * map.RowPitch,
                    desc.Width * 4);
    impl.Context->Unmap(staging.Get(), 0);
    return out;
}
std::vector<float> ReadShadowDepth(Backend &impl)
{
    D3D11_TEXTURE2D_DESC desc{};
    impl.ShadowMap.Texture->GetDesc(&desc);
    Require(desc.Format == DXGI_FORMAT_R32_TYPELESS && desc.SampleDesc.Count == 1, "shadow texture format");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(impl.Device->CreateTexture2D(&desc, nullptr, &staging)), "depth staging");
    impl.Context->CopyResource(staging.Get(), impl.ShadowMap.Texture.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "depth map");
    std::vector<float> out(desc.Width * desc.Height);
    for (UINT row = 0; row < desc.Height; ++row)
        std::memcpy(out.data() + row * desc.Width, static_cast<const char *>(map.pData) + row * map.RowPitch,
                    desc.Width * sizeof(float));
    impl.Context->Unmap(staging.Get(), 0);
    return out;
}
Backend::ShadowConstants ReadShadowConstants(Backend &impl)
{
    D3D11_BUFFER_DESC desc{};
    impl.ShadowMap.Constants->GetDesc(&desc);
    Require(desc.ByteWidth == 5136, "shadow constant layout");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    Require(SUCCEEDED(impl.Device->CreateBuffer(&desc, nullptr, &staging)), "shadow constants staging");
    impl.Context->CopyResource(staging.Get(), impl.ShadowMap.Constants.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "shadow constants map");
    const auto result = *static_cast<const Backend::ShadowConstants *>(map.pData);
    impl.Context->Unmap(staging.Get(), 0);
    return result;
}
Vector3 Project(const Vector3 &position, const Matrix4 &matrix)
{
    return Vector3::FromXM(DirectX::XMVector3TransformCoord(position.ToXM(), matrix.ToXM()));
}
std::array<int, 3> PixelAt(const Image &image, const RenderScene &scene, Vector3 point)
{
    const auto ndc = Project(point, scene.camera.view * scene.camera.projection);
    const int x = std::clamp(int((ndc.X * 0.5f + 0.5f) * image.width), 0, int(image.width) - 1);
    const int y = std::clamp(int((-ndc.Y * 0.5f + 0.5f) * image.height), 0, int(image.height) - 1);
    const size_t offset = (y * image.width + x) * 4;
    return {image.rgba[offset], image.rgba[offset + 1], image.rgba[offset + 2]};
}
float DepthAt(const std::vector<float> &depth, Backend &impl, const Backend::ShadowConstants &constants, Vector3 point)
{
    const auto ndc = Project(point, Matrix4::FromXM(DirectX::XMLoadFloat4x4(&constants.Views[0].LightViewProjection)));
    const int resolution = int(impl.ShadowMap.Resolution);
    const int x = std::clamp(int((ndc.X * 0.5f + 0.5f) * resolution), 0, resolution - 1);
    const int y = std::clamp(int((-ndc.Y * 0.5f + 0.5f) * resolution), 0, resolution - 1);
    return depth[y * resolution + x];
}
void Save(const Image &image, const char *name)
{
    std::ofstream stream(std::string("Intermediate/ShadowValidation/") + name + ".ppm", std::ios::binary);
    stream << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    for (size_t offset = 0; offset < image.rgba.size(); offset += 4)
        stream.write(reinterpret_cast<const char *>(image.rgba.data() + offset), 3);
}
Image Frame(DX11Renderer &renderer, const RenderScene &scene, const std::vector<RenderLight> *lights = nullptr)
{
    auto &impl = *renderer.m_Impl;
    Require(renderer.BeginFrame(), "frame begin");
    Require(!impl.ShadowActive, "BeginFrame clears shadow state");
    if (lights)
    {
        renderer.SetCamera(scene.camera.view.Data(), scene.camera.projection.Data());
        impl.SceneData.AmbientColor = scene.ambientColor;
        impl.SceneData.AmbientIntensity = scene.ambientIntensity;
        impl.SceneDirty = true;
        impl.UploadLights(*lights, &scene);
        impl.BindSharedConstants();
        for (const auto &item : scene.items)
            renderer.DrawMesh(item.mesh, item.world.Data(), item.submeshIndex, item.material);
    }
    else
        renderer.DrawScene(scene);
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11RenderTargetView> rtv;
    impl.Context->OMGetRenderTargets(1, &rtv, &dsv);
    Require(dsv.Get() == impl.DepthStencilView.Get() && rtv.Get() == impl.SceneColorRenderTargetView.Get(),
            "main targets restored");
    UINT count = 1;
    D3D11_VIEWPORT viewport{};
    impl.Context->RSGetViewports(&count, &viewport);
    Require(viewport.Width == 256 && viewport.Height == 256, "camera viewport restored");
    auto image = ReadImage(impl, impl.SceneColorTexture.Get());
    renderer.EndFrame();
    return image;
}
void CheckMessages(ID3D11InfoQueue *queue)
{
    unsigned warnings = 0;
    for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i)
    {
        SIZE_T size = 0;
        queue->GetMessage(i, nullptr, &size);
        std::vector<unsigned char> bytes(size);
        auto *message = reinterpret_cast<D3D11_MESSAGE *>(bytes.data());
        queue->GetMessage(i, message, &size);
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
        {
            ++warnings;
            std::cerr << "D3D11: " << message->pDescription << '\n';
        }
    }
    Require(warnings == 0, "D3D11 debug-layer warning/error");
    std::cout << "D3D11 debug layer: zero warnings/errors (including SRV/DSV hazards)\n";
}
void ShadowChecks()
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"CanapeShadowValidation";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"Shadow validation", WS_POPUP, 0, 0, 256, 256, nullptr, nullptr,
                                  instance, nullptr);
    Require(window != nullptr, "hidden window");
    {
        DX11Renderer renderer;
        auto &impl = *renderer.m_Impl;
        impl.Window = window;
        DXGI_SWAP_CHAIN_DESC swap{};
        swap.BufferDesc.Width = swap.BufferDesc.Height = 256;
        swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap.SampleDesc.Count = 1;
        swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap.BufferCount = 2;
        swap.OutputWindow = window;
        swap.Windowed = TRUE;
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        Require(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                                        D3D11_CREATE_DEVICE_DEBUG, &level, 1, D3D11_SDK_VERSION, &swap,
                                                        &impl.SwapChain, &impl.Device, nullptr, &impl.Context)),
                "WARP debug device");
        ComPtr<ID3D11InfoQueue> queue;
        Require(SUCCEEDED(impl.Device.As(&queue)), "debug info queue");
        Require(impl.CreateTargets(256, 256) && impl.CreatePipeline(), "targets and pipeline");
        if (!impl.Pipelines.ShadowsAvailable())
            CheckMessages(queue.Get());
        Require(impl.Pipelines.ShadowsAvailable(), "shadow pipeline compiled");
        // The receiver uses a nonzero startIndex and baseVertex to exercise the shared SubMesh path.
        const std::array<StaticVertex, 8> groundVertices{{{{0, 0, 0}, {0, 1, 0}, {0, 0}},
                                                          {{0, 0, 0}, {0, 1, 0}, {1, 0}},
                                                          {{0, 0, 0}, {0, 1, 0}, {1, 1}},
                                                          {{0, 0, 0}, {0, 1, 0}, {0, 1}},
                                                          {{-8, 0, -8}, {0, 1, 0}, {0, 0}},
                                                          {{8, 0, -8}, {0, 1, 0}, {1, 0}},
                                                          {{8, 0, 8}, {0, 1, 0}, {1, 1}},
                                                          {{-8, 0, 8}, {0, 1, 0}, {0, 1}}}};
        const std::array<uint32_t, 12> groundIndices{0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3};
        const std::array<SubMesh, 1> parts{{{6, 6, 4, 0}}};
        const auto ground = renderer.CreateMesh(groundVertices, groundIndices, parts);
        const std::array<DX11Renderer::Vertex, 8> cubeVertices{{{{-.8f, 0, -.8f}},
                                                                {{.8f, 0, -.8f}},
                                                                {{.8f, 2, -.8f}},
                                                                {{-.8f, 2, -.8f}},
                                                                {{-.8f, 0, .8f}},
                                                                {{.8f, 0, .8f}},
                                                                {{.8f, 2, .8f}},
                                                                {{-.8f, 2, .8f}}}};
        const std::array<uint32_t, 36> cubeIndices{0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 1, 5, 0, 5, 4,
                                                   3, 7, 6, 3, 6, 2, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5};
        const auto cube = renderer.CreateMesh(cubeVertices, cubeIndices);
        Require(renderer.IsMeshValid(ground) && renderer.IsMeshValid(cube), "scene meshes");
        RenderScene scene;
        scene.camera.view = Matrix4::LookTo({-12, 15, -18}, {12, -15, 18});
        scene.camera.projection = Matrix4::Perspective(0.95f, 1, .1f, 100);
        scene.camera.cameraPosition = {-12, 15, -18};
        scene.items = {RenderItem{ground}, RenderItem{cube}};
        impl.ShadowSettings.enabled = false;
        auto unshadowed = Frame(renderer, scene);
        Save(unshadowed, "unshadowed");
        const auto cameraDepthWithoutShadows = ReadImage(impl, impl.DepthTexture.Get()).rgba;
        impl.ShadowSettings.enabled = true;
        auto shadowed = Frame(renderer, scene);
        Save(shadowed, "shadowed");
        Require(ReadImage(impl, impl.DepthTexture.Get()).rgba == cameraDepthWithoutShadows,
                "shadow pass preserves the camera depth buffer");
        Require(impl.ShadowActive, "public DrawScene shadow active");
        Require(impl.ShadowMap.Resolution == 2048, "default resolution");
        auto constants = ReadShadowConstants(impl);
        auto packed = ReadLights(impl);
        Require(constants.ViewCount == 1 && packed.DirectionalLights[0].ShadowViewCount == 1 &&
                    packed.DirectionalLights[0].ShadowViewOffset == 0,
                "valid shadow metadata");
        auto depth = ReadShadowDepth(impl);
        const float minDepth = *std::min_element(depth.begin(), depth.end());
        Require(minDepth > 0 && minDepth < 1, "actual mesh depth written");
        const Vector3 shadowPoint{-1.15f, 0, 1.4f};
        const Vector3 litPoint{3, 0, 1.4f};
        auto before = PixelAt(unshadowed, scene, shadowPoint);
        auto after = PixelAt(shadowed, scene, shadowPoint);
        auto litBefore = PixelAt(unshadowed, scene, litPoint);
        auto litAfter = PixelAt(shadowed, scene, litPoint);
        std::cout << "Receiver RGB (same scene): shadow off " << before[0] << ',' << before[1] << ',' << before[2]
                  << " -> on " << after[0] << ',' << after[1] << ',' << after[2] << "; minimum shadow depth "
                  << minDepth << '\n';
        Require(before[0] - after[0] > 50, "visible cast shadow on separate receiver");
        Require(std::abs(litBefore[0] - litAfter[0]) <= 2, "unoccluded receiver stays lit / no acne");
        impl.ShadowSettings.debugView = Backend::ShadowDebugMode::Visibility;
        auto visibility = Frame(renderer, scene);
        Save(visibility, "visibility");
        Require(PixelAt(visibility, scene, shadowPoint)[0] < 10 && PixelAt(visibility, scene, litPoint)[0] > 245,
                "visibility black and white");
        impl.ShadowSettings.debugView = Backend::ShadowDebugMode::MapDepth;
        Frame(renderer, scene);
        ComPtr<ID3D11Texture2D> back;
        impl.SwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
        auto depthImage = ReadImage(impl, back.Get());
        Save(depthImage, "shadow-depth");
        back.Reset(); // Release the test readback reference before exercising ResizeBuffers.
        impl.ShadowSettings.debugView = Backend::ShadowDebugMode::None;
        // Alpha-masked caster has a transparent left half and opaque right half.
        const std::array<StaticVertex, 4> maskVertices{{{{-.8f, 2, -.8f}, {0, 1, 0}, {0, 0}},
                                                        {{.8f, 2, -.8f}, {0, 1, 0}, {1, 0}},
                                                        {{.8f, 2, .8f}, {0, 1, 0}, {1, 1}},
                                                        {{-.8f, 2, .8f}, {0, 1, 0}, {0, 1}}}};
        const std::array<uint32_t, 6> quadIndices{0, 1, 2, 0, 2, 3};
        const auto maskMesh = renderer.CreateMesh(maskVertices, quadIndices, {});
        const std::array<uint8_t, 8> texels{255, 255, 255, 0, 255, 255, 255, 255};
        const auto maskTexture = renderer.CreateTexture(2, 1, TextureFormat::RGBA8_SRGB, texels);
        MaterialDesc masked;
        masked.alphaMode = AlphaMode::Masked;
        const auto maskMaterial = renderer.CreateMaterial(masked, maskTexture);
        RenderScene maskScene = scene;
        maskScene.items[1] = RenderItem{maskMesh, 0, maskMaterial};
        Frame(renderer, maskScene);
        constants = ReadShadowConstants(impl);
        depth = ReadShadowDepth(impl);
        const float openDepth = DepthAt(depth, impl, constants, {-.4f, 2, 0});
        const float closedDepth = DepthAt(depth, impl, constants, {.4f, 2, 0});
        std::cout << "Masked caster depths: transparent half " << openDepth << ", opaque half " << closedDepth << '\n';
        Require(openDepth - closedDepth > .008f, "masked alpha clipping in depth pass");
        masked.alphaMode = AlphaMode::Transparent;
        const auto transparentMaterial = renderer.CreateMaterial(masked, maskTexture);
        maskScene.items[1].material = transparentMaterial;
        Frame(renderer, maskScene);
        depth = ReadShadowDepth(impl);
        Require(DepthAt(depth, impl, ReadShadowConstants(impl), {.4f, 2, 0}) - closedDepth > .008f,
                "transparent caster excluded");
        maskScene.items[1].material = maskMaterial;
        maskScene.items[1].castShadow = false;
        Frame(renderer, maskScene);
        depth = ReadShadowDepth(impl);
        Require(DepthAt(depth, impl, ReadShadowConstants(impl), {.4f, 2, 0}) - closedDepth > .008f,
                "castShadow false excluded");
        // Both passes must use the caster's translated world transform.
        auto movedScene = scene;
        movedScene.items[1].world = Matrix4::FromXM(DirectX::XMMatrixTranslation(3, 0, 0));
        auto moved = Frame(renderer, movedScene);
        Require(PixelAt(moved, scene, shadowPoint)[0] - after[0] > 50, "old receiver shadow moves away");
        const Vector3 movedShadowPoint = shadowPoint + Vector3{3, 0, 0};
        auto movedOn = PixelAt(moved, scene, movedShadowPoint);
        impl.ShadowSettings.enabled = false;
        auto movedOff = Frame(renderer, movedScene);
        impl.ShadowSettings.enabled = true;
        Require(PixelAt(movedOff, scene, movedShadowPoint)[0] - movedOn[0] > 50,
                "translated caster shadows translated receiver");
        impl.ShadowSettings.width = 4;
        impl.ShadowSettings.height = 4;
        impl.ShadowSettings.debugView = Backend::ShadowDebugMode::Visibility;
        auto outside = Frame(renderer, scene);
        Require(PixelAt(outside, scene, {6, 0, 0})[0] > 245, "outside light frustum stays visible");
        impl.ShadowSettings.width = 80;
        impl.ShadowSettings.height = 80;
        impl.ShadowSettings.debugView = Backend::ShadowDebugMode::None;
        auto directional = CreateTestLights()[0];
        directional.color = {1, 0, 0};
        directional.intensity = .8f;
        auto second = directional;
        second.castShadow = false;
        second.color = {0, 0, 1};
        auto point = CreateTestLights()[1];
        point.color = {0, 1, 0};
        point.position = {-1.15f, 3, 1.4f};
        point.range = 20;
        point.intensity = .25f;
        std::vector<RenderLight> multiple{directional, second, point};
        auto mixed = Frame(renderer, scene, &multiple);
        auto mixedShadow = PixelAt(mixed, scene, shadowPoint);
        impl.ShadowSettings.enabled = false;
        auto mixedNoShadow = Frame(renderer, scene, &multiple);
        auto mixedLit = PixelAt(mixedNoShadow, scene, shadowPoint);
        impl.ShadowSettings.enabled = true;
        Require(mixedLit[0] - mixedShadow[0] > 50 && std::abs(mixedLit[1] - mixedShadow[1]) <= 2 &&
                    std::abs(mixedLit[2] - mixedShadow[2]) <= 2,
                "only selected directional is shadowed; other directional, point, ambient unchanged");
        std::vector<RenderLight> none;
        Frame(renderer, scene, &none);
        Require(!impl.ShadowActive && ReadShadowConstants(impl).ViewCount == 0, "no lights fallback");
        std::vector<RenderLight> pointOnly{point};
        Frame(renderer, scene, &pointOnly);
        Require(!impl.ShadowActive && ReadShadowConstants(impl).ViewCount == 0, "point only fallback");
        auto down = directional;
        down.direction = {0, -1, 0};
        std::vector<RenderLight> vertical{down};
        Frame(renderer, scene, &vertical);
        Require(impl.ShadowActive, "parallel up handled");
        auto noCaster = directional;
        noCaster.castShadow = false;
        std::vector<RenderLight> noShadowFlag{noCaster, directional};
        Frame(renderer, scene, &noShadowFlag);
        Require(!impl.ShadowActive, "first active light castShadow respected");
        impl.ShadowSettings.resolution = 0;
        auto fallback = Frame(renderer, scene);
        Require(!impl.ShadowActive, "invalid resolution fallback");
        Require(PixelAt(fallback, scene, shadowPoint) == before, "failed shadow resource keeps main lighting");
        impl.ShadowSettings.resolution = 1024;
        Frame(renderer, scene);
        Require(impl.ShadowActive && impl.ShadowMap.Resolution == 1024, "resource recreation after setting change");
        impl.ShadowSettings.pcf = false;
        Frame(renderer, scene);
        D3D11_SAMPLER_DESC sampler{};
        impl.ShadowMap.Sampler->GetDesc(&sampler);
        Require(sampler.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT, "single comparison setting");
        impl.ShadowSettings.pcf = true;
        Frame(renderer, scene);
        impl.ShadowMap.Sampler->GetDesc(&sampler);
        Require(sampler.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, "hardware PCF setting");
        impl.ShadowSettings.resolution = 512;
        impl.ShadowSettings.debugView = Backend::ShadowDebugMode::Visibility;
        impl.ShadowSettings.pcf = false;
        auto pointVisibility = Frame(renderer, scene);
        impl.ShadowSettings.pcf = true;
        auto pcfVisibility = Frame(renderer, scene);
        auto partialPixels = [](const Image &image) {
            size_t count = 0;
            for (size_t i = 0; i < image.rgba.size(); i += 4)
                if (image.rgba[i] > 2 && image.rgba[i] < 253 && image.rgba[i] == image.rgba[i + 1] &&
                    image.rgba[i] == image.rgba[i + 2])
                    ++count;
            return count;
        };
        const auto hardPartial = partialPixels(pointVisibility), pcfPartial = partialPixels(pcfVisibility);
        std::cout << "Visibility partial pixels: hard comparison " << hardPartial << ", hardware PCF " << pcfPartial
                  << '\n';
        Require(hardPartial == 0 && pcfPartial > 0, "PCF produces filtered visibility");
        impl.ShadowSettings.resolution = 2048;
        impl.ShadowSettings.debugView = Backend::ShadowDebugMode::None;
        impl.ShadowSettings.nearPlane = 20;
        impl.ShadowSettings.farPlane = 10;
        Frame(renderer, scene);
        Require(!impl.ShadowActive, "invalid frustum fallback");
        impl.ShadowSettings.nearPlane = .1f;
        impl.ShadowSettings.farPlane = 200;
        RendererDebugSettings debug;
        debug.enabled = true;
        for (auto mode :
             {DebugViewMode::CameraDepth, DebugViewMode::WorldNormal, DebugViewMode::UV, DebugViewMode::MipLOD})
        {
            debug.viewMode = mode;
            debug.wireframe = false;
            renderer.SetDebugSettings(debug);
            Frame(renderer, scene);
            debug.wireframe = true;
            renderer.SetDebugSettings(debug);
            Frame(renderer, scene);
        }
        renderer.SetDebugSettings({});
        Frame(renderer, RenderScene{});
        Require(ReadShadowConstants(impl).ViewCount == 1, "empty scene clears shadow map");
        depth = ReadShadowDepth(impl);
        Require(*std::min_element(depth.begin(), depth.end()) == 1.0f, "empty map reset");
        // Camera target resize must not resize or alias the fixed-resolution shadow texture.
        Require(SetWindowPos(window, nullptr, 0, 0, 192, 128, SWP_NOMOVE | SWP_NOZORDER), "test window resize");
        Require(renderer.BeginFrame(), "resized frame");
        Require(renderer.GetWidth() == 192 && renderer.GetHeight() == 128, "resized camera targets");
        renderer.DrawScene(scene);
        Require(impl.ShadowMap.Resolution == 2048, "shadow resolution independent of camera resize");
        renderer.EndFrame();
        Require(SetWindowPos(window, nullptr, 0, 0, 256, 256, SWP_NOMOVE | SWP_NOZORDER), "restore window size");
        Frame(renderer, scene);
        CheckMessages(queue.Get());
        std::cout << "Shadow GPU integration: geometry, light view, receiver, alpha mask, transparent exclusion, "
                     "flags, fallback, recreation, debug modes PASS\n";
    }
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, instance);
}
int main()
{
    try
    {
        CpuChecks();
        ShadowChecks();
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
