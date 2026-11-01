// Standalone backend integration test; intentionally not part of DX11Renderer.vcxproj.
// The private-access include is confined to this test translation unit. It permits
// GPU readback/failure injection without adding an engine-facing testing interface.
#define NOMINMAX
#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/MeshData.h"
#include "DX11Renderer/Data/RendererDebugSettings.h"
#include "DX11Renderer/Data/RendererDebugStats.h"
#include "DX11Renderer/Data/TextureData.h"
#include "DX11Renderer/Scene/RenderScene.h"
#include <array>
#include <cmath>
#include <cstdlib>
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
    Require(Backend::AdjacentOption(Backend::ShadowSlopeBiasOptions, 2.0f, true) == 3.0f
        && Backend::AdjacentOption(Backend::ShadowSlopeBiasOptions, 7.0f, true) == 8.0f
        && Backend::AdjacentOption(Backend::ShadowSlopeBiasOptions, 8.0f, true) == 0.0f,
        "Ctrl+F2 backend slope range reaches 3..8 and wraps");
    Require(Backend::AdjacentOption(Backend::PcfSpacingOptions, 1.0f, false) == 0.5f
        && Backend::AdjacentOption(Backend::PcfSpacingOptions, 1.4f, true) == 1.5f
        && Backend::AdjacentOption(Backend::PcfSpacingOptions, 1.5f, true) == 1.6f
        && Backend::AdjacentOption(Backend::PcfSpacingOptions, 1.5f, false) == 1.4f
        && Backend::AdjacentOption(Backend::BlockerRadiusOptions, 0.25f, true) == 0.4f
        && Backend::AdjacentOption(Backend::BlockerRadiusOptions, 0.4f, true) == 0.5f
        && Backend::AdjacentOption(Backend::BlockerRadiusOptions, 0.75f, false) == 0.5f,
        "PCSS diagnostic options step independently");
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
void TexelSnappingChecks()
{
    using namespace DirectX;
    Backend impl;
    impl.ShadowMap.Resolution = 4096;
    const Vector3 lightDirection{-0.4f, -1.0f, 0.6f};
    const Matrix4 cameraProjection = Matrix4::Perspective(0.95f, 1.5f, 0.1f, 100.0f);
    XMStoreFloat4x4(&impl.ViewData.Projection, cameraProjection.ToXM());
    Matrix4 cameraView;
    auto setCamera = [&](const Vector3& position, const Vector3& direction)
    {
        cameraView = Matrix4::LookTo(position, direction);
        XMStoreFloat4x4(&impl.ViewData.View, cameraView.ToXM());
    };
    auto build = [&]()
    {
        Backend::ShadowViewGpu shadow{};
        Backend::PcssConstants pcss{};
        Require(impl.BuildDirectionalShadowView(lightDirection, impl.ShadowSettings, shadow, pcss),
            "build snapped shadow view");
        Require(pcss.OrthoSize.x > 0.0f && pcss.OrthoSize.y > 0.0f,
            "PCSS receives the actual orthographic projection extent");
        return shadow;
    };
    auto width = [](const Backend::ShadowViewGpu& shadow)
    {
        const auto& m = shadow.LightViewProjection;
        return 2.0f / std::sqrt(m._11 * m._11 + m._21 * m._21 + m._31 * m._31);
    };
    auto checkFrustum = [&](const Backend::ShadowViewGpu& shadow)
    {
        XMVECTOR determinant;
        const XMMATRIX inverseCamera = XMMatrixInverse(&determinant,
            cameraView.ToXM() * cameraProjection.ToXM());
        const XMMATRIX lightViewProjection = XMLoadFloat4x4(&shadow.LightViewProjection);
        const float farFraction = (std::min(100.0f, impl.ShadowSettings.shadowDistance) - 0.1f) / 99.9f;
        for (float z : {0.0f, 1.0f})
            for (float y : {-1.0f, 1.0f})
                for (float x : {-1.0f, 1.0f})
                {
                    const XMVECTOR nearWorld = XMVector3TransformCoord(XMVectorSet(x, y, 0, 1), inverseCamera);
                    const XMVECTOR farWorld = XMVector3TransformCoord(XMVectorSet(x, y, 1, 1), inverseCamera);
                    const XMVECTOR world = z == 0.0f ? nearWorld
                        : XMVectorLerp(nearWorld, farWorld, farFraction);
                    const XMVECTOR clip = XMVector4Transform(world, lightViewProjection);
                    const float w = XMVectorGetW(clip);
                    Require(std::abs(XMVectorGetX(clip) / w) <= 1.0001f
                        && std::abs(XMVectorGetY(clip) / w) <= 1.0001f
                        && XMVectorGetZ(clip) / w >= -0.0001f
                        && XMVectorGetZ(clip) / w <= 1.0001f,
                        "snapped shadow contains all clipped frustum corners");
                }
    };

    setCamera({}, {0, 0, 1});
    Vector3 center;
    float radius;
    double fittedRadius;
    const float halfFovTangent = std::tan(0.95f * 0.5f);
    float previousWidth = 0.0f;
    for (float distance : {20.0f, 40.0f, 80.0f, 100.0f, 120.0f})
    {
        impl.ShadowSettings.shadowDistance = distance;
        Require(Backend::BuildFrustumBoundingSphere(impl.ViewData, distance, center, radius, fittedRadius),
            "shadow-distance frustum sphere");
        const float effectiveFar = std::min(distance, 100.0f);
        const double farX = effectiveFar * halfFovTangent * 1.5f;
        const double farY = effectiveFar * halfFovTangent;
        const double halfDepth = (effectiveFar - 0.1f) * 0.5;
        const double expectedRadius = std::sqrt(farX * farX + farY * farY + halfDepth * halfDepth);
        Require(std::abs(center.Z - (effectiveFar + 0.1f) * 0.5f) < 0.01f
            && std::abs(fittedRadius - expectedRadius) < 0.01,
            "shadow-distance corners and sphere match clipped perspective frustum");
        const auto shadow = build();
        const float currentWidth = width(shadow);
        std::cout << "shadow distance " << distance << ": projection width " << currentWidth
            << ", world units/texel " << currentWidth / impl.ShadowMap.Resolution << '\n';
        Require(distance <= 100.0f ? currentWidth > previousWidth
            : std::abs(currentWidth - previousWidth) < 0.05f,
            "shadow extent grows until the camera far plane");
        previousWidth = currentWidth;
        checkFrustum(shadow);
    }
    impl.ShadowSettings.shadowDistance = 40.0f;
    impl.ShadowMap.Resolution = 1024;
    Require(Backend::BuildFrustumBoundingSphere(impl.ViewData, 40.0f, center, radius, fittedRadius),
        "frustum sphere");
    const XMVECTOR forward = XMVector3Normalize(lightDirection.ToXM());
    const XMMATRIX lightAxes = XMMatrixLookToLH(XMVectorZero(), forward, XMVectorSet(0, 1, 0, 0));
    const XMVECTOR centerLS = XMVector3TransformNormal(center.ToXM(), lightAxes);
    const Vector3 alignedPosition = Vector3::FromXM(XMVector3TransformNormal(
        XMVectorSet(-XMVectorGetX(centerLS), -XMVectorGetY(centerLS), 0, 0),
        XMMatrixTranspose(lightAxes)));
    auto moveCamera = [&](float x, float y)
    {
        const Vector3 movement = Vector3::FromXM(XMVector3TransformNormal(
            XMVectorSet(x, y, 0, 0), XMMatrixTranspose(lightAxes)));
        setCamera(alignedPosition + movement, {0, 0, 1});
    };
    moveCamera(0, 0);
    const auto baseline = build();
    const float texelSize = width(baseline) / impl.ShadowMap.Resolution;
    checkFrustum(baseline);
    Require(Backend::BuildFrustumBoundingSphere(impl.ViewData, 40.0f, center, radius, fittedRadius),
        "aligned frustum sphere");
    const XMVECTOR caster = XMVectorSubtract(center.ToXM(), XMVectorScale(forward, radius + 40.0f));
    const XMVECTOR casterCamera = XMVector3TransformCoord(caster, cameraView.ToXM());
    const XMVECTOR casterClip = XMVector4Transform(XMVectorSetW(caster, 1.0f),
        XMLoadFloat4x4(&baseline.LightViewProjection));
    Require(XMVectorGetZ(casterCamera) < 0.1f
        && std::abs(XMVectorGetX(casterClip)) < 1.0f
        && std::abs(XMVectorGetY(casterClip)) < 1.0f
        && XMVectorGetZ(casterClip) > 0.0f && XMVectorGetZ(casterClip) < 1.0f,
        "off-camera caster remains inside the shadow volume");
    moveCamera(0.25f * texelSize, 0.25f * texelSize);
    const auto smallMove = build();
    Require(std::abs(smallMove.LightViewProjection._41 - baseline.LightViewProjection._41) < 1e-5f
        && std::abs(smallMove.LightViewProjection._42 - baseline.LightViewProjection._42) < 1e-5f,
        "sub-texel movement keeps shadow XY fixed");
    checkFrustum(smallMove);
    moveCamera(0.75f * texelSize, 0);
    const auto oneTexel = build();
    Require(std::abs(std::abs(oneTexel.LightViewProjection._41 - baseline.LightViewProjection._41)
        - 2.0f / impl.ShadowMap.Resolution) < 1e-5f
        && std::abs(oneTexel.LightViewProjection._42 - baseline.LightViewProjection._42) < 1e-5f,
        "shadow view moves by one texel at grid boundary");
    checkFrustum(oneTexel);
    setCamera(alignedPosition, {0.3f, -0.2f, 1.0f});
    const auto rotated = build();
    Require(std::abs(width(rotated) - width(baseline)) < 1e-4f,
        "camera rotation keeps shadow projection width fixed");
    checkFrustum(rotated);
    impl.ShadowMap.Resolution = 512;
    const auto lowerResolution = build();
    checkFrustum(lowerResolution);
    std::cout << "CPU: texel snapping, rotation-stable width, frustum XY containment PASS\n";
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
Backend::PcssConstants ReadPcssConstants(Backend &impl)
{
    D3D11_BUFFER_DESC desc{};
    impl.ShadowMap.PcssConstants->GetDesc(&desc);
    Require(desc.ByteWidth == sizeof(Backend::PcssConstants), "PCSS constant layout");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Buffer> staging;
    Require(SUCCEEDED(impl.Device->CreateBuffer(&desc, nullptr, &staging)), "PCSS constants staging");
    impl.Context->CopyResource(staging.Get(), impl.ShadowMap.PcssConstants.Get());
    D3D11_MAPPED_SUBRESOURCE map{};
    Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "PCSS constants map");
    const auto result = *static_cast<const Backend::PcssConstants *>(map.pData);
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
void SetShadowDebugView(DX11Renderer &renderer, DebugViewMode mode)
{
    RendererDebugSettings settings;
    settings.enabled = mode != DebugViewMode::Final;
    settings.viewMode = mode;
    renderer.SetDebugSettings(settings);
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
        // Keep the WARP readback allocation bounded and use the test scene's depth range.
        impl.ShadowSettings.resolution = 4096;
        impl.ShadowSettings.farPlane = 200.0f;
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
        Require(impl.ShadowMap.Resolution == impl.ShadowSettings.resolution, "default resolution");
        auto shadowPipeline = impl.Pipelines.ResolveShadow(VertexFormat::Position, AlphaMode::Opaque);
        auto mainPipeline = impl.Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque);
        Require(shadowPipeline.Rasterizer && mainPipeline.Rasterizer
                    && shadowPipeline.Rasterizer != mainPipeline.Rasterizer,
                "shadow pass uses its own rasterizer state");
        D3D11_RASTERIZER_DESC shadowRasterizer{}, mainRasterizer{};
        shadowPipeline.Rasterizer->GetDesc(&shadowRasterizer);
        mainPipeline.Rasterizer->GetDesc(&mainRasterizer);
        Require(shadowRasterizer.CullMode == D3D11_CULL_BACK
                    && shadowRasterizer.SlopeScaledDepthBias == DefaultShadowSlopeBias
                    && mainRasterizer.SlopeScaledDepthBias == 0.0f
                    && shadowRasterizer.DepthBias == mainRasterizer.DepthBias
                    && shadowRasterizer.DepthBiasClamp == mainRasterizer.DepthBiasClamp,
                "default shadow culling and slope bias preserve constant bias and clamp");
        Require(ReadShadowConstants(impl).Views[0].DepthBias == DefaultShadowReceiverBias,
                "default shadow receiver bias");
        RendererDebugSettings tuning{};
        tuning.shadowCullMode = ShadowCullMode::Front;
        tuning.shadowSlopeBias = 0.5f;
        tuning.shadowReceiverBias = 0.0001f;
        tuning.shadowPcf = false;
        renderer.SetDebugSettings(tuning);
        auto tunedShadow = impl.Pipelines.ResolveShadow(VertexFormat::Position, AlphaMode::Opaque);
        tunedShadow.Rasterizer->GetDesc(&shadowRasterizer);
        Require(shadowRasterizer.CullMode == D3D11_CULL_FRONT && shadowRasterizer.SlopeScaledDepthBias == 0.5f,
                "runtime front cull and slope bias");
        Frame(renderer, scene);
        Require(ReadShadowConstants(impl).Views[0].DepthBias == tuning.shadowReceiverBias
                    && ReadShadowConstants(impl).PcfEnabled == 0,
                "runtime receiver bias and PCF off");
        tuning.shadowCullMode = ShadowCullMode::Back;
        tuning.shadowSlopeBias = 2.0f;
        tuning.shadowReceiverBias = 0.001f;
        renderer.SetDebugSettings(tuning);
        tunedShadow = impl.Pipelines.ResolveShadow(VertexFormat::Position, AlphaMode::Opaque);
        tunedShadow.Rasterizer->GetDesc(&shadowRasterizer);
        Require(shadowRasterizer.CullMode == D3D11_CULL_BACK && shadowRasterizer.SlopeScaledDepthBias == 2.0f,
                "runtime back cull and slope bias");
        Frame(renderer, scene);
        Require(ReadShadowConstants(impl).Views[0].DepthBias == tuning.shadowReceiverBias,
                "runtime receiver bias update");
        tuning.shadowSlopeBias = 8.0f;
        renderer.SetDebugSettings(tuning);
        impl.Pipelines.ResolveShadow(VertexFormat::Position, AlphaMode::Opaque).Rasterizer
            ->GetDesc(&shadowRasterizer);
        Require(shadowRasterizer.SlopeScaledDepthBias == 8.0f,
            "extended slope bias applies to shadow rasterizer");
        renderer.SetDebugSettings({});
        Frame(renderer, scene);
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
        SetShadowDebugView(renderer, DebugViewMode::ShadowVisibility);
        auto visibility = Frame(renderer, scene);
        Save(visibility, "visibility");
        Require(PixelAt(visibility, scene, shadowPoint)[0] < 10 && PixelAt(visibility, scene, litPoint)[0] > 245,
                "visibility black and white");
        SetShadowDebugView(renderer, DebugViewMode::ShadowDepth);
        Frame(renderer, scene);
        ComPtr<ID3D11Texture2D> back;
        impl.SwapChain->GetBuffer(0, IID_PPV_ARGS(&back));
        auto depthImage = ReadImage(impl, back.Get());
        Save(depthImage, "shadow-depth");
        back.Reset(); // Release the test readback reference before exercising ResizeBuffers.
        SetShadowDebugView(renderer, DebugViewMode::Final);
        // Alpha-masked caster has a transparent left half and opaque right half.
        const std::array<StaticVertex, 4> maskVertices{{{{-.8f, 2, -.8f}, {0, 1, 0}, {0, 0}},
                                                        {{.8f, 2, -.8f}, {0, 1, 0}, {1, 0}},
                                                        {{.8f, 2, .8f}, {0, 1, 0}, {1, 1}},
                                                        {{-.8f, 2, .8f}, {0, 1, 0}, {0, 1}}}};
        const std::array<uint32_t, 6> quadIndices{0, 2, 1, 0, 3, 2};
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
        SetShadowDebugView(renderer, DebugViewMode::Final);
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
        ID3D11Texture2D* shadowTexture = impl.ShadowMap.Texture.Get();
        RendererDebugSettings pcfSettings{};
        pcfSettings.shadowPcf = false;
        renderer.SetDebugSettings(pcfSettings);
        Frame(renderer, scene);
        D3D11_SAMPLER_DESC sampler{};
        impl.ShadowMap.PointSampler->GetDesc(&sampler);
        Require(sampler.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT, "single comparison setting");
        Require(impl.ShadowMap.Texture.Get() == shadowTexture, "PCF off keeps shadow texture");
        pcfSettings.shadowPcf = true;
        renderer.SetDebugSettings(pcfSettings);
        Frame(renderer, scene);
        impl.ShadowMap.PcfSampler->GetDesc(&sampler);
        Require(sampler.Filter == D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, "hardware PCF setting");
        Require(impl.ShadowMap.Texture.Get() == shadowTexture, "PCF on keeps shadow texture");
        impl.ShadowSettings.resolution = 512;
        pcfSettings.enabled = true;
        pcfSettings.viewMode = DebugViewMode::ShadowVisibility;
        pcfSettings.shadowPcf = false;
        renderer.SetDebugSettings(pcfSettings);
        auto pointVisibility = Frame(renderer, scene);
        pcfSettings.shadowPcf = true;
        renderer.SetDebugSettings(pcfSettings);
        impl.ShadowSettings.pcss = false;
        auto pcfVisibility = Frame(renderer, scene);
        impl.ShadowSettings.pcss = true;
        impl.ShadowSettings.lightAngularRadius = 0.01f;
        auto narrowPcssVisibility = Frame(renderer, scene);
        impl.ShadowSettings.lightAngularRadius = 0.3f;
        auto widePcssVisibility = Frame(renderer, scene);
        const auto defaultPcss = ReadPcssConstants(impl);
        Require(defaultPcss.PcfTapSpacingScale == 1.5f
            && defaultPcss.BlockerSearchRadiusWorld == 0.4f
            && std::abs(defaultPcss.MaxFilterRadiusWorld * defaultPcss.PcfTapSpacingScale
                - defaultPcss.BlockerSearchRadiusWorld) < 1e-6f
            && defaultPcss.PcfSampleCount == 64
            && defaultPcss.BlockerSampleCount == 25
            && defaultPcss.FilterMode == 2,
            "configured PCSS defaults reach the GPU");
        impl.ShadowSettings.pcfTapSpacingScale = 1.0f;
        auto denseFootprintVisibility = Frame(renderer, scene);
        Require(ReadPcssConstants(impl).PcfTapSpacingScale == 1.0f
            && denseFootprintVisibility.rgba != widePcssVisibility.rgba,
            "tap spacing changes PCF footprint without shader recompilation");
        impl.ShadowSettings.pcfTapSpacingScale = 1.5f;
        impl.ShadowSettings.blockerSearchRadiusWorld = 0.25f;
        Frame(renderer, scene);
        Require(ReadPcssConstants(impl).BlockerSearchRadiusWorld == 0.25f
            && ReadPcssConstants(impl).PcfTapSpacingScale == 1.5f,
            "blocker search radius remains independent of final PCF spacing");
        impl.ShadowSettings.blockerSearchRadiusWorld = 0.4f;
        Require(Frame(renderer, scene).rgba == widePcssVisibility.rgba,
            "restoring PCSS diagnostic defaults restores original visibility");
        Save(widePcssVisibility, "pcss-wide-visibility");
        Require(!renderer.SetPcssPcfSampleCount(24) && renderer.GetPcssPcfSampleCount() == 64,
            "unsupported PCSS sample count rejected");
        const auto debugShader = impl.Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque,
            DebugViewMode::ShadowVisibility).PS;
        Require(renderer.SetPcssPcfSampleCount(25), "legacy 25 mode remains selectable");
        auto legacy25Visibility = Frame(renderer, scene);
        Require(ReadPcssConstants(impl).PcfSampleCount == 25
            && ReadPcssConstants(impl).FilterMode == 3, "legacy 25-tap mode remains available");
        impl.ShadowSettings.legacyPcf25 = false;
        auto vogel25Visibility = Frame(renderer, scene);
        Require(ReadPcssConstants(impl).FilterMode == 2, "Vogel 25 mode selectable");
        Require(vogel25Visibility.rgba != legacy25Visibility.rgba,
            "legacy and Vogel 25 patterns are distinguishable");
        Image vogel64Visibility{}, vogel128Visibility{};
        for (uint32_t count : { 8u, 16u, 25u, 32u, 64u, 128u })
        {
            Require(renderer.SetPcssPcfSampleCount(count)
                && renderer.GetPcssPcfSampleCount() == count, "PCSS sample count setter");
            auto image = Frame(renderer, scene);
            const auto gpu = ReadPcssConstants(impl);
            Require(gpu.PcfSampleCount == count && gpu.FilterMode == 2,
                "PCSS sample count uploaded without changing the filter mode");
            Require(impl.Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque,
                DebugViewMode::ShadowVisibility).PS == debugShader,
                "PCSS sample count changes without recompiling the pixel shader");
            if (count == 64) vogel64Visibility = std::move(image);
            if (count == 128) vogel128Visibility = std::move(image);
        }
        Save(vogel64Visibility, "pcss-vogel-64-visibility");
        Save(vogel128Visibility, "pcss-vogel-128-visibility");
        Require(vogel64Visibility.rgba != vogel128Visibility.rgba,
            "64 and 128 taps produce distinct filtered images");
        pcfSettings.viewMode = DebugViewMode::PcssFilterRadius;
        renderer.SetDebugSettings(pcfSettings);
        auto radius128 = Frame(renderer, scene);
        impl.ShadowSettings.pcfTapSpacingScale = 1.0f;
        auto radiusHalfSpacing = Frame(renderer, scene);
        Require(radiusHalfSpacing.rgba != radius128.rgba,
            "radius debug view shows effective PCF tap spacing");
        impl.ShadowSettings.pcfTapSpacingScale = 1.5f;
        Require(Frame(renderer, scene).rgba == radius128.rgba,
            "restoring tap spacing restores radius debug view");
        Require(renderer.SetPcssPcfSampleCount(8), "restore 8 taps for radius comparison");
        auto radius8 = Frame(renderer, scene);
        Require(radius8.rgba == radius128.rgba,
            "PCSS radius debug view is independent of final PCF sample count");
        Image radiusBlocker128{};
        for (uint32_t count : { 8u, 16u, 25u, 32u, 64u, 128u })
        {
            impl.ShadowSettings.blockerSampleCount = count;
            auto image = Frame(renderer, scene);
            const auto gpu = ReadPcssConstants(impl);
            Require(gpu.BlockerSampleCount == count && gpu.PcfSampleCount == 8
                && gpu.FilterMode == 2,
                "blocker sample count uploads independently of final PCF count");
            if (count == 128) radiusBlocker128 = std::move(image);
        }
        Require(radiusBlocker128.rgba != radius8.rgba,
            "blocker count changes the radius debug view without shader recompilation");
        impl.ShadowSettings.blockerSampleCount = 25;
        Require(Frame(renderer, scene).rgba == radius8.rgba,
            "restoring 25 blocker samples restores the original radius view");
        impl.ShadowSettings.minFilterRadiusWorld = impl.ShadowSettings.maxFilterRadiusWorld;
        impl.ShadowSettings.pcfTapSpacingScale = 3.0f;
        auto clampedRadius3 = Frame(renderer, scene);
        impl.ShadowSettings.pcfTapSpacingScale = 4.0f;
        Require(Frame(renderer, scene).rgba == clampedRadius3.rgba,
            "final tap radius stays at the blocker search radius when spacing grows");
        impl.ShadowSettings.blockerSearchRadiusWorld = 0.25f;
        Require(Frame(renderer, scene).rgba != clampedRadius3.rgba,
            "lower blocker search radius also limits the final tap radius");
        impl.ShadowSettings.blockerSearchRadiusWorld = 0.4f;
        impl.ShadowSettings.minFilterRadiusWorld = 0.0f;
        impl.ShadowSettings.pcfTapSpacingScale = 1.5f;
        Require(Frame(renderer, scene).rgba == radius128.rgba,
            "restoring radius limits restores the debug view");
        Require(PixelAt(radius128, scene, shadowPoint)[0] > 0,
            "PCSS radius debug view displays the shadow filter footprint");
        Save(radius128, "pcss-radius-texels");
        pcfSettings.viewMode = DebugViewMode::ShadowDepth;
        renderer.SetDebugSettings(pcfSettings);
        impl.PcssRadiusViewOverride = true;
        Require(Frame(renderer, scene).rgba == radius128.rgba,
            "renderer-local radius shortcut overrides the engine debug selection");
        impl.PcssRadiusViewOverride = false;
        pcfSettings.viewMode = DebugViewMode::ShadowVisibility;
        renderer.SetDebugSettings(pcfSettings);
        Require(renderer.SetPcssPcfSampleCount(64), "restore 64-tap default");
        impl.ShadowSettings.legacyPcf25 = true;
        auto partialPixels = [](const Image &image) {
            size_t count = 0;
            for (size_t i = 0; i < image.rgba.size(); i += 4)
                if (image.rgba[i] > 2 && image.rgba[i] < 253 && image.rgba[i] == image.rgba[i + 1] &&
                    image.rgba[i] == image.rgba[i + 2])
                    ++count;
            return count;
        };
        const auto hardPartial = partialPixels(pointVisibility), pcfPartial = partialPixels(pcfVisibility);
        const auto narrowPcssPartial = partialPixels(narrowPcssVisibility);
        const auto widePcssPartial = partialPixels(widePcssVisibility);
        std::cout << "Wide PCSS partial pixels: legacy 25 " << partialPixels(legacy25Visibility)
                  << ", Vogel 25 " << partialPixels(vogel25Visibility)
                  << ", Vogel 64 " << partialPixels(vogel64Visibility)
                  << ", Vogel 128 " << partialPixels(vogel128Visibility) << '\n';
        std::cout << "Visibility partial pixels: hard comparison " << hardPartial << ", 3x3 PCF " << pcfPartial
                  << ", narrow PCSS " << narrowPcssPartial << ", wide PCSS " << widePcssPartial << '\n';
        Require(hardPartial == 0 && pcfPartial > 0, "PCF produces filtered visibility");
        Require(widePcssPartial > narrowPcssPartial && widePcssPartial > pcfPartial,
                "larger light angular radius widens the soft shadow boundary");
        impl.ShadowSettings.lightAngularRadius = 0.15f;
        auto contactVisibility = Frame(renderer, scene);
        auto elevatedScene = scene;
        elevatedScene.items[1].world = Matrix4::FromXM(DirectX::XMMatrixTranslation(0, 2, 0));
        auto elevatedVisibility = Frame(renderer, elevatedScene);
        Save(elevatedVisibility, "pcss-elevated-visibility");
        const auto contactPartial = partialPixels(contactVisibility);
        const auto elevatedPartial = partialPixels(elevatedVisibility);
        std::cout << "PCSS partial pixels: contact " << contactPartial
                  << ", elevated caster " << elevatedPartial << '\n';
        Require(elevatedPartial > contactPartial,
                "separating caster from receiver widens the penumbra");
        impl.ShadowSettings.farPlane = 400.0f;
        auto deeperProjectionVisibility = Frame(renderer, elevatedScene);
        size_t depthRangeDifference = 0;
        for (size_t i = 0; i < elevatedVisibility.rgba.size(); i += 4)
            depthRangeDifference += std::abs(int(elevatedVisibility.rgba[i])
                - int(deeperProjectionVisibility.rgba[i]));
        std::cout << "PCSS mean visibility difference after Far change: "
                  << float(depthRangeDifference) / (elevatedVisibility.width * elevatedVisibility.height) << '\n';
        Require(depthRangeDifference < elevatedVisibility.width * elevatedVisibility.height * 2,
                "changing shadow depth range preserves world-space softness");
        impl.ShadowSettings.farPlane = 200.0f;
        impl.ShadowSettings.resolution = 1024;
        auto higherResolutionVisibility = Frame(renderer, elevatedScene);
        size_t resolutionDifference = 0;
        for (size_t i = 0; i < elevatedVisibility.rgba.size(); i += 4)
            resolutionDifference += std::abs(int(elevatedVisibility.rgba[i])
                - int(higherResolutionVisibility.rgba[i]));
        std::cout << "PCSS mean visibility difference after resolution change: "
                  << float(resolutionDifference) / (elevatedVisibility.width * elevatedVisibility.height) << '\n';
        Require(resolutionDifference < elevatedVisibility.width * elevatedVisibility.height * 5,
                "changing shadow resolution preserves approximate world-space softness");
        impl.ShadowSettings.lightAngularRadius = 0.02f;
        impl.ShadowSettings.resolution = 4096;
        SetShadowDebugView(renderer, DebugViewMode::Final);
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
        Require(impl.ShadowMap.Resolution == 4096, "shadow resolution independent of camera resize");
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
        TexelSnappingChecks();
        if (!std::getenv("CANAPE_SHADOW_SNAP_ONLY")) ShadowChecks();
        return 0;
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
