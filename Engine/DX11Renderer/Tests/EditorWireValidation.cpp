// Real line rendering, camera transforms, occlusion and unchanged depth on WARP at 1x/4x.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <memory>
#include <sstream>
#include <functional>
#include <d3d11sdklayers.h>
#include "DX11Renderer/Pipeline/DX11PipelineState.h"
#define private public
#include "DX11Renderer/DX11Renderer.h"
#undef private
#include "DX11Renderer/Internal/DX11RendererImpl.h"
#include "DX11Renderer/Pass/DX11EditorWirePass.h"
#include "CNDebug/DebugLog.h"

namespace Canape::Debug
{
    void Write(MainTag, std::string_view, std::string message, const char*, int) { std::cerr << message << '\n'; }
}
using namespace Canape;
using Microsoft::WRL::ComPtr;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void SceneSubmissionChecks(ID3D11Device* device, ID3D11DeviceContext* context, UINT samples)
{
    context->ClearState();
    DX11Renderer renderer;
    auto& impl = *renderer.m_Impl;
    impl.Device = device;
    impl.Context = context;
    impl.SelectedSceneSampleDesc = { samples, 0 };
    Require(impl.CreateSceneTargets(128, 128) && impl.CreatePipeline(), impl.LastError.c_str());
    Require(impl.Pipelines.EditorWireAvailable(), "scene wire pipeline available");
    impl.FrameActive = true;
    impl.ShadowSettings.enabled = false;
    auto read = [&](ID3D11Texture2D* texture)
    {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        Require(desc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT, "HDR readback format");
        const bool multisampled = desc.SampleDesc.Count > 1;
        desc.SampleDesc = { 1, 0 };
        desc.BindFlags = desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> resolved;
        if (multisampled)
        {
            Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &resolved)), "scene resolve texture");
            context->ResolveSubresource(resolved.Get(), 0, texture, 0, desc.Format);
        }
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> staging;
        Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "scene readback texture");
        context->CopyResource(staging.Get(), multisampled ? resolved.Get() : texture);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "scene readback map");
        std::vector<unsigned char> bytes(size_t(desc.Width) * desc.Height * 8);
        for (UINT y = 0; y < desc.Height; ++y)
            std::memcpy(bytes.data() + size_t(y) * desc.Width * 8,
                static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch, desc.Width * 8);
        context->Unmap(staging.Get(), 0);
        return bytes;
    };
    auto clearMain = [&]
    {
        const float black[4]{};
        context->ClearRenderTargetView(impl.SceneColorRenderTargetView.Get(), black);
        context->ClearDepthStencilView(impl.DepthStencilView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    };
    RenderScene scene;
    scene.camera.cameraPosition = { 0, 1, 0 };
    scene.mirrorPlaneHeight = 0.0f;
    clearMain();
    renderer.DrawScene(scene);
    const auto baseline = read(impl.SceneRenderTexture.Get());
    ComPtr<ID3D11Resource> mirrorResource;
    ComPtr<ID3D11Texture2D> mirror;
    impl.Mirror.ShaderView->GetResource(&mirrorResource);
    Require(SUCCEEDED(mirrorResource.As(&mirror)), "mirror resource");
    const auto mirrorBaseline = read(mirror.Get());
    const EditorWireSphere wire{ { 0, 0, 0.5f }, 0.35f, { 1, 0, 0, 1 } };
    renderer.SetRenderView(scene.camera);
    renderer.SetMirrorPlaneHeight(0);
    renderer.AddEditorWireSphere(wire);
    renderer.RequestPick(64, 64);
    clearMain();
    renderer.DrawScene();
    Require(read(impl.SceneRenderTexture.Get()) != baseline, "queued wire reaches the main scene pass");
    Require(read(mirror.Get()) == mirrorBaseline, "wire is excluded from mirror rendering");
    Require(impl.PickTable.empty() && !impl.OutlineMaskReady, "wire is excluded from picking and outlines");
    Require(impl.QueuedScene.editorWireSpheres.empty(), "DrawScene consumes the editor queue");
    renderer.SetRenderView(scene.camera);
    clearMain();
    renderer.DrawScene();
    Require(read(impl.SceneRenderTexture.Get()) == baseline, "next queued scene contains no stale wires");
    renderer.Shutdown();
    std::cout << "PASS scene submission " << samples << "x: queued main draw, mirror/pick/outline exclusion, queue reset\n";
}

void Run(UINT samples)
{
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
        D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context)), "WARP device");
    ComPtr<ID3D11InfoQueue> messages;
    Require(SUCCEEDED(device.As(&messages)), "debug messages");
    DX11PipelineState pipelines;
    std::string error;
    Require(pipelines.Initialize(device.Get(), error), error.c_str());
    Require(pipelines.EditorWireAvailable(), "wire pipeline available");

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 128;
    desc.MipLevels = desc.ArraySize = 1;
    desc.SampleDesc.Count = samples;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> color, depth, resolved, staging;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11DepthStencilView> dsv;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &color)), "color texture");
    Require(SUCCEEDED(device->CreateRenderTargetView(color.Get(), nullptr, &target)), "color RTV");
    desc.Format = DXGI_FORMAT_D32_FLOAT;
    desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &depth)), "depth texture");
    Require(SUCCEEDED(device->CreateDepthStencilView(depth.Get(), nullptr, &dsv)), "depth DSV");
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = 0;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &resolved)), "resolve texture");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    Require(SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging)), "readback texture");

    D3D11_BUFFER_DESC cb{};
    cb.Usage = D3D11_USAGE_DEFAULT;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.ByteWidth = sizeof(ShaderConstants::EditorWireConstants);
    ComPtr<ID3D11Buffer> constants, viewBuffer;
    Require(SUCCEEDED(device->CreateBuffer(&cb, nullptr, &constants)), "wire constants");
    cb.ByteWidth = sizeof(ShaderConstants::ViewConstants);
    Require(SUCCEEDED(device->CreateBuffer(&cb, nullptr, &viewBuffer)), "view constants");
    ShaderConstants::ViewConstants view{};
    DirectX::XMStoreFloat4x4(&view.ViewProjection, DirectX::XMMatrixIdentity());
    ConstantBuffers::Upload(*context.Get(), viewBuffer.Get(), view);
    const MainTargets targets{ target.Get(), dsv.Get(), 128, 128 };
    const EditorWireSphere sphere{ { 0.0f, 0.0f, 0.5f }, 0.35f, { 1, 0, 0, 1 } };
    auto draw = [&](float depthValue, std::span<const EditorWireSphere> spheres)
    {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        const float black[4]{};
        context->ClearRenderTargetView(target.Get(), black);
        context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH, depthValue, 0);
        EditorWirePass::Execute(*context.Get(), pipelines, constants.Get(), viewBuffer.Get(), targets, spheres, nullptr);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        if (samples > 1) context->ResolveSubresource(resolved.Get(), 0, color.Get(), 0, DXGI_FORMAT_R8G8B8A8_UNORM);
        context->CopyResource(staging.Get(), samples > 1 ? resolved.Get() : color.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "color map");
        unsigned count = 0;
        double sumX = 0;
        for (unsigned y = 0; y < 128; ++y)
        {
            const auto* row = static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch;
            for (unsigned x = 0; x < 128; ++x)
                if (row[x * 4] > 0) { ++count; sumX += x; }
        }
        context->Unmap(staging.Get(), 0);
        return std::pair{ count, count ? sumX / count : 0.0 };
    };
    const std::array spheres{ sphere };
    const auto visible = draw(1.0f, spheres);
    Require(visible.first > 150, "three visible rings produce pixels");
    Require(draw(0.05f, spheres).first == 0, "foreground scene depth hides all rings");
    const auto partial = draw(0.3f, spheres);
    Require(partial.first > 0 && partial.first < visible.first, "scene depth clips rear ring segments");
    if (samples == 1)
    {
        D3D11_TEXTURE2D_DESC depthRead{};
        depth->GetDesc(&depthRead);
        depthRead.Usage = D3D11_USAGE_STAGING;
        depthRead.BindFlags = 0;
        depthRead.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> depthStaging;
        Require(SUCCEEDED(device->CreateTexture2D(&depthRead, nullptr, &depthStaging)), "depth staging");
        context->CopyResource(depthStaging.Get(), depth.Get());
        D3D11_MAPPED_SUBRESOURCE mapped{};
        Require(SUCCEEDED(context->Map(depthStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "depth map");
        for (unsigned y = 0; y < 128; ++y)
        {
            const auto* row = reinterpret_cast<const float*>(static_cast<const unsigned char*>(mapped.pData) + y * mapped.RowPitch);
            for (unsigned x = 0; x < 128; ++x) Require(row[x] == 0.3f, "wire pass must not write scene depth");
        }
        context->Unmap(depthStaging.Get(), 0);
    }
    DirectX::XMStoreFloat4x4(&view.ViewProjection, DirectX::XMMatrixTranslation(0.3f, 0, 0));
    ConstantBuffers::Upload(*context.Get(), viewBuffer.Get(), view);
    Require(draw(1, spheres).second > visible.second + 15, "wire pass uses supplied camera transform");
    DirectX::XMStoreFloat4x4(&view.ViewProjection, DirectX::XMMatrixIdentity());
    ConstantBuffers::Upload(*context.Get(), viewBuffer.Get(), view);
    auto nearPlane = sphere;
    nearPlane.center.Z = 0.05f;
    Require(draw(1, std::span{ &nearPlane, 1 }).first > 0, "near-plane crossing remains visible");
    auto offscreen = sphere;
    offscreen.center.X = 3;
    Require(draw(1, std::span{ &offscreen, 1 }).first == 0, "offscreen rings are clipped");
    auto invalid = sphere;
    for (float radius : { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        invalid.radius = radius;
        Require(draw(1, std::span{ &invalid, 1 }).first == 0, "invalid range rejected");
    }
    Require(draw(1, {}).first == 0, "empty next submission leaves no old rings");
    SceneSubmissionChecks(device.Get(), context.Get(), samples);
    context->ClearState();
    for (UINT64 index = 0; index < messages->GetNumStoredMessages(); ++index)
    {
        SIZE_T size = 0;
        messages->GetMessage(index, nullptr, &size);
        std::vector<unsigned char> storage(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
        messages->GetMessage(index, message, &size);
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
        {
            std::cerr << message->pDescription << '\n';
            Require(false, "D3D11 debug warning/error");
        }
    }
    std::cout << "PASS WARP " << samples << "x: rings, occlusion, camera, clipping, invalid ranges, empty submission, clean debug layer\n";
}

int main()
{
    try { Run(1); Run(4); return 0; }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
