// Real renderer integration checks using WARP, pixel readback and the D3D11 debug layer.
#include "DX11Renderer/Data/UIDrawList.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <iostream>
#include <stdexcept>
#include <vector>
#define private public
#include "DX11Renderer/DX11Renderer.h"
#undef private
#include "DX11Renderer/Internal/DX11RendererImpl.h"
#include "DX11Renderer/Pass/DX11OutlinePass.h"
#include "CNDebug/DebugLog.h"

namespace Canape::Debug
{
    void Write(MainTag, std::string_view, std::string message, const char*, int)
    {
        std::cerr << message << '\n';
    }
}

using namespace Canape;
using Backend = DX11Renderer::Impl;
using Microsoft::WRL::ComPtr;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::vector<uint32_t> Read(Backend& impl, ID3D11Texture2D* texture)
{
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    Require(desc.SampleDesc.Count == 1, "readback expects 1x");
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> staging;
    Require(SUCCEEDED(impl.Device->CreateTexture2D(&desc, nullptr, &staging)), "readback texture");
    impl.Context->CopyResource(staging.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)), "readback map");
    std::vector<uint32_t> result(size_t(desc.Width) * desc.Height);
    for (uint32_t y = 0; y < desc.Height; ++y)
        std::memcpy(result.data() + size_t(y) * desc.Width,
            static_cast<const unsigned char*>(mapped.pData) + size_t(y) * mapped.RowPitch, desc.Width * 4);
    impl.Context->Unmap(staging.Get(), 0);
    return result;
}

bool Yellow(uint32_t rgba)
{
    // Linear (1, .8, .1) encoded once by the SDR sRGB RTV, after tone mapping.
    const int r = rgba & 255, g = (rgba >> 8) & 255, b = (rgba >> 16) & 255;
    return r == 255 && std::abs(g - 231) <= 1 && std::abs(b - 89) <= 1;
}

// GPU extraction preserves every categorical sample; no ResolveSubresource for IDs/depth.
// Depth is copied to an SRV-capable typeless mirror so this also works in Shipping.
class SampleReader
{
    ComPtr<ID3D11PixelShader> Shaders[2][2];
public:
    std::vector<uint32_t> ReadAll(Backend& impl, ID3D11Texture2D* texture, bool depth = false)
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        const bool msaa = desc.SampleDesc.Count > 1;
        auto& shader = Shaders[depth][msaa];
        if (!shader)
        {
            const char* source = R"(
#ifdef READ_DEPTH
#define VALUE float
#else
#define VALUE uint
#endif
#ifdef READ_MSAA
Texture2DMS<VALUE> Source : register(t0);
#else
Texture2D<VALUE> Source : register(t0);
#endif
uint PSMain(float4 position : SV_POSITION) : SV_TARGET
{
    uint w, h, count;
#ifdef READ_MSAA
    Source.GetDimensions(w, h, count);
    uint x = uint(position.x);
    VALUE value = Source.Load(int2(x / count, uint(position.y)), x % count);
#else
    VALUE value = Source.Load(int3(int2(position.xy), 0));
#endif
#ifdef READ_DEPTH
    return asuint(value);
#else
    return value;
#endif
})";
            const D3D_SHADER_MACRO defines[] = { {depth ? "READ_DEPTH" : "READ_IDS", "1"},
                {msaa ? "READ_MSAA" : "READ_SINGLE", "1"}, {nullptr, nullptr} };
            ComPtr<ID3DBlob> code, error;
            const HRESULT hr = D3DCompile(source, std::strlen(source), nullptr, defines, nullptr,
                "PSMain", "ps_5_0", D3DCOMPILE_WARNINGS_ARE_ERRORS, 0, &code, &error);
            if (FAILED(hr) && error) std::cerr << static_cast<const char*>(error->GetBufferPointer());
            Require(SUCCEEDED(hr), "sample extraction shader");
            Require(SUCCEEDED(impl.Device->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
                nullptr, &shader)), "sample extraction PS");
        }
        impl.Context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11ShaderResourceView* unbound = nullptr;
        impl.Context->PSSetShaderResources(0, 1, &unbound);
        ComPtr<ID3D11Texture2D> depthCopy;
        auto sourceDesc = desc;
        if (depth)
        {
            sourceDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
            sourceDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            Require(SUCCEEDED(impl.Device->CreateTexture2D(&sourceDesc, nullptr, &depthCopy)), "depth mirror");
            impl.Context->CopyResource(depthCopy.Get(), texture);
            texture = depthCopy.Get();
        }
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = depth ? DXGI_FORMAT_R24_UNORM_X8_TYPELESS : DXGI_FORMAT_R32_UINT;
        srvDesc.ViewDimension = msaa ? D3D11_SRV_DIMENSION_TEXTURE2DMS : D3D11_SRV_DIMENSION_TEXTURE2D;
        if (!msaa) srvDesc.Texture2D.MipLevels = 1;
        ComPtr<ID3D11ShaderResourceView> view;
        Require(SUCCEEDED(impl.Device->CreateShaderResourceView(texture, &srvDesc, &view)), "sample source SRV");
        D3D11_TEXTURE2D_DESC outputDesc{};
        outputDesc.Width = desc.Width * desc.SampleDesc.Count;
        outputDesc.Height = desc.Height;
        outputDesc.MipLevels = outputDesc.ArraySize = outputDesc.SampleDesc.Count = 1;
        outputDesc.Format = DXGI_FORMAT_R32_UINT;
        outputDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> output;
        ComPtr<ID3D11RenderTargetView> target;
        Require(SUCCEEDED(impl.Device->CreateTexture2D(&outputDesc, nullptr, &output)), "extraction output");
        Require(SUCCEEDED(impl.Device->CreateRenderTargetView(output.Get(), nullptr, &target)), "extraction RTV");
        auto pipeline = impl.Pipelines.ResolveFinalOutput(false);
        pipeline.PS = shader.Get();
        RenderBindings::BindPipeline(*impl.Context.Get(), pipeline);
        RenderBindings::BindMainTargets(*impl.Context.Get(), {target.Get(), nullptr, outputDesc.Width, outputDesc.Height});
        ID3D11ShaderResourceView* input = view.Get();
        impl.Context->PSSetShaderResources(0, 1, &input);
        impl.Context->Draw(3, 0);
        impl.Context->PSSetShaderResources(0, 1, &unbound);
        impl.Context->OMSetRenderTargets(0, nullptr, nullptr);
        return Read(impl, output.Get());
    }

    std::vector<uint32_t> Mask(Backend& impl)
    {
        const auto samples = ReadAll(impl, impl.OutlineMaskTexture.Get());
        const auto count = impl.SelectedSceneSampleDesc.Count;
        std::vector<uint32_t> result(samples.size() / count);
        for (size_t i = 0; i < result.size(); ++i)
            for (UINT s = 0; s < count; ++s)
                if (samples[i * count + s]) { result[i] = samples[i * count + s]; break; }
        return result;
    }

    void CheckCoverage(Backend& impl)
    {
        const auto mask = ReadAll(impl, impl.OutlineMaskTexture.Get());
        const auto depth = ReadAll(impl, impl.DepthTexture.Get(), true);
        Require(mask.size() == depth.size(), "matching sample readback sizes");
        for (size_t i = 0; i < mask.size(); ++i)
            Require((mask[i] != 0) == (depth[i] != 0x3F800000u), "main/mask coverage matches at every sample");
    }
};

uint32_t CheckComposite(Backend& impl, SampleReader& reader, const std::vector<uint32_t>& baseline,
    const std::vector<uint32_t>& outlined, const PreparedScene& prepared,
    bool requireMixedIds = false, bool requirePartial = true)
{
    if (!impl.OutlineMaskReady)
    {
        Require(baseline == outlined && prepared.OutlineDraws.empty(), "zero-opacity/disabled scene skips composite without stale styles");
        return 0;
    }
    const auto mask = reader.ReadAll(impl, impl.OutlineMaskTexture.Get());
    const UINT count = impl.SelectedSceneSampleDesc.Count;
    auto at = [&](int x, int y, UINT s)
    {
        return x < 0 || y < 0 || x >= 64 || y >= 64 ? 0u : mask[(size_t(y) * 64 + x) * count + s];
    };
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    auto encoded = [](double v)
    {
        return int(std::lround(255 * (v <= .0031308 ? 12.92 * v : 1.055 * std::pow(v, 1 / 2.4) - .055)));
    };
    UINT partialGeometry = 0, partialEdges = 0, mixedIds = 0, mixedEdgeStyles = 0;
    for (int y = 0; y < 64; ++y)
    {
        for (int x = 0; x < 64; ++x)
        {
            UINT covered = 0, edgeSamples = 0;
            uint32_t firstId = 0;
            uint32_t firstEdgeId = 0;
            bool mixed = false, mixedEdge = false;
            double opacitySum = 0;
            std::array<double, 3> weightedColor{};
            for (UINT s = 0; s < count; ++s)
            {
                const uint32_t center = at(x, y, s);
                Require(center < prepared.OutlineStyles.size(), "IDs stay categorical, no numeric resolve");
                if (center)
                {
                    ++covered;
                    if (firstId && firstId != center) mixed = true;
                    firstId = center;
                }
                uint32_t chosenId = 0;
                int nearest = INT_MAX;
                const int radius = int(prepared.OutlineMaxThickness);
                for (int dy = -radius; dy <= radius; ++dy)
                {
                    for (int dx = -radius; dx <= radius; ++dx)
                    {
                        const auto neighbor = at(x + dx, y + dy, s);
                        if (!neighbor || neighbor == center) continue;
                        const auto& style = prepared.OutlineStyles.at(neighbor);
                        const int distance = dx * dx + dy * dy;
                        if (style.Opacity <= 0 || distance > int(style.Thickness * style.Thickness)) continue;
                        if (distance < nearest || (distance == nearest && neighbor < chosenId))
                        {
                            chosenId = neighbor;
                            nearest = distance;
                        }
                    }
                }
                if (chosenId)
                {
                    ++edgeSamples;
                    if (firstEdgeId && firstEdgeId != chosenId) mixedEdge = true;
                    firstEdgeId = chosenId;
                    const auto& style = prepared.OutlineStyles[chosenId];
                    opacitySum += style.Opacity;
                    weightedColor[0] += double(style.Color.X) * style.Opacity;
                    weightedColor[1] += double(style.Color.Y) * style.Opacity;
                    weightedColor[2] += double(style.Color.Z) * style.Opacity;
                }
            }
            partialGeometry += covered > 0 && covered < count;
            partialEdges += edgeSamples > 0 && edgeSamples < count;
            mixedIds += mixed;
            mixedEdgeStyles += mixedEdge;
            const size_t pixel = size_t(y) * 64 + x;
            if (!edgeSamples)
                Require(baseline[pixel] == outlined[pixel], "non-edge/interior pixels preserve final scene");
            const double alpha = opacitySum / count;
            for (unsigned channel = 0; channel < 3; ++channel)
            {
                const int background = (baseline[pixel] >> (channel * 8)) & 255;
                const int actual = (outlined[pixel] >> (channel * 8)) & 255;
                const int expected = encoded(linear(double(background) / 255) * (1 - alpha) + weightedColor[channel] / count);
                if (std::abs(actual - expected) > 2)
                {
                    std::cerr << "coverage pixel=" << x << ',' << y << " samples=" << edgeSamples << '/' << count
                        << " channel=" << channel << " actual=" << actual << " expected=" << expected << '\n';
                    Require(false, "per-sample edge coverage blended after tone mapping");
                }
            }
        }
    }
    if (count > 1 && requirePartial)
    {
        Require(partialEdges > 0, "subpixel edge generates partial outline coverage");
        Require(requireMixedIds ? mixedIds > 0 : partialGeometry > 0, "test exercises mixed samples inside a pixel");
    }
    std::cout << "PASS: CPU per-object style/opacity reference; partial geometry=" << partialGeometry
        << ", partial edges=" << partialEdges << ", mixed IDs=" << mixedIds << ", mixed edge styles=" << mixedEdgeStyles << '\n';
    return mixedEdgeStyles;
}

MeshResourceId Quad(DX11Renderer& renderer, float left, float top, float right, float bottom,
    float depth = .5f, bool skinned = false, bool slanted = false)
{
    std::array<Vector3, 4> positions{
        Vector3{left / 32 - 1, 1 - top / 32, depth}, Vector3{right / 32 - 1, 1 - top / 32, depth},
        Vector3{right / 32 - 1, 1 - bottom / 32, depth}, Vector3{left / 32 - 1, 1 - bottom / 32, depth} };
    if (slanted)
    {
        const std::array<Vector2, 4> corners{Vector2{15.2f,11.4f}, Vector2{49.7f,18.3f},
            Vector2{45.1f,52.7f}, Vector2{10.3f,41.6f}};
        for (size_t i = 0; i < 4; ++i)
            positions[i] = {corners[i].X / 32 - 1, 1 - corners[i].Y / 32, depth};
    }
    const std::array<uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    const std::array<SubMesh, 2> parts{SubMesh{0, 3, 0, 0}, SubMesh{3, 3, 0, 1}};
    const std::array<Vector2, 4> uvs{Vector2{0, 0}, Vector2{1, 0}, Vector2{1, 1}, Vector2{0, 1}};
    std::array<StaticVertex, 4> vertices{};
    std::array<SkinnedVertex, 4> skin{};
    for (size_t i = 0; i < 4; ++i)
    {
        vertices[i].Position = skin[i].Position = positions[i];
        vertices[i].Normal = skin[i].Normal = {0, 0, -1};
        vertices[i].UV = skin[i].UV = uvs[i];
        skin[i].Weights[0] = 1;
    }
    return skinned ? renderer.CreateMesh(skin, indices, parts) : renderer.CreateMesh(vertices, indices, parts);
}

void Add(RenderScene& scene, MeshResourceId mesh, MaterialResourceId material,
    uint64_t objectId, bool outline = true)
{
    for (uint32_t part = 0; part < 2; ++part)
    {
        MeshItem item{mesh, part, material};
        item.objectId = objectId;
        item.outlineEnabled = outline;
        scene.items.push_back(item);
    }
}

void CheckMessages(ID3D11InfoQueue* queue)
{
    unsigned warnings = 0;
    for (UINT64 i = 0; i < queue->GetNumStoredMessages(); ++i)
    {
        SIZE_T size = 0;
        queue->GetMessage(i, nullptr, &size);
        std::vector<unsigned char> bytes(size);
        auto* message = reinterpret_cast<D3D11_MESSAGE*>(bytes.data());
        queue->GetMessage(i, message, &size);
        if (message->Severity <= D3D11_MESSAGE_SEVERITY_WARNING)
        {
            ++warnings;
            std::cerr << message->pDescription << '\n';
        }
    }
    Require(warnings == 0, "D3D11 debug-layer warning/error");
}

void Run(UINT sampleCount, D3D_DRIVER_TYPE driver)
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"CanapeOutlineValidation";
    RegisterClassW(&wc);
    HWND window = CreateWindowExW(0, wc.lpszClassName, L"Outline validation", WS_POPUP,
        0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    Require(window != nullptr, "hidden validation window");
    DX11Renderer renderer;
    auto& impl = *renderer.m_Impl;
    impl.Window = window;
    DXGI_SWAP_CHAIN_DESC swap{};
    swap.BufferDesc.Width = swap.BufferDesc.Height = 64;
    swap.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap.SampleDesc.Count = 1;
    swap.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap.BufferCount = 2;
    swap.OutputWindow = window;
    swap.Windowed = TRUE;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    Require(SUCCEEDED(D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr,
        D3D11_CREATE_DEVICE_DEBUG, &level, 1, D3D11_SDK_VERSION, &swap,
        &impl.SwapChain, &impl.Device, nullptr, &impl.Context)), "requested debug device");
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIAdapter1> adapter1;
    DXGI_ADAPTER_DESC1 adapterDesc{};
    Require(SUCCEEDED(impl.Device.As(&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))
        && SUCCEEDED(adapter.As(&adapter1)) && SUCCEEDED(adapter1->GetDesc1(&adapterDesc)), "adapter description");
    std::wcout << L"Adapter: " << adapterDesc.Description << L'\n';
    if (driver == D3D_DRIVER_TYPE_HARDWARE)
        Require((adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0, "physical hardware adapter required");
    ComPtr<ID3D11InfoQueue> messages;
    Require(SUCCEEDED(impl.Device.As(&messages)), "D3D11 info queue");
    impl.InitializeSceneSampleDesc();
    Require(impl.SelectedSceneSampleDesc.Count == 4, "default scene restores 4x MSAA");
    if (sampleCount == 1) impl.SelectedSceneSampleDesc = {1, 0};
    Require(impl.CreateTargets(64, 64) && impl.CreatePipeline() && impl.CreateSkyboxQuad(),
        impl.LastError.c_str());
    impl.Initialized = true;
    impl.ShadowSettings.enabled = false;
    renderer.SetOverlay([] {});
    SampleReader reader;
    RenderScene scene;
    scene.ambientIntensity = .1f;
    auto frame = [&](const RenderScene& submitted)
    {
        Require(renderer.BeginFrame(), "BeginFrame");
        renderer.DrawScene(submitted);
        renderer.EndFrame();
        return Read(impl, impl.DisplayTexture.Get());
    };
    frame(scene);
    Require(!impl.OutlineMaskTexture && !impl.OutlineMaskReady, "empty frame avoids mask allocation and draws");
    const auto quad = Quad(renderer, 16, 16, 48, 48);
    const auto adjacent = Quad(renderer, 48, 16, 60, 48);
    const auto occluder = Quad(renderer, 16, 16, 48, 48, .25f);
    MaterialDesc desc;
    const auto opaque = renderer.CreateMaterial(desc);
    desc.baseColor = { .5f, .5f, .5f, 1 };
    const auto otherMaterial = renderer.CreateMaterial(desc);
    Add(scene, quad, opaque, 1);
    scene.items[1].material = otherMaterial;
    auto pixels = frame(scene);
    auto mask = reader.Mask(impl);
    Require(mask[20 * 64 + 40] == 1 && mask[40 * 64 + 20] == 1, "submeshes share mask ID");
    Require(Yellow(pixels[32 * 64 + 14]) && Yellow(pixels[32 * 64 + 15])
        && !Yellow(pixels[32 * 64 + 13]), "outer outline is exactly two output pixels");
    Require(!Yellow(pixels[32 * 64 + 16]) && !Yellow(pixels[32 * 64 + 32]),
        "silhouette stays outside; material/triangle seam stays invisible");
    std::cout << "PASS: multi-submesh region, material independence, no internal seam, 2px linear SDR color\n";

    // These IDs collide under a 32-bit truncation.
    Add(scene, adjacent, opaque, (uint64_t(1) << 32) | 1);
    std::string error;
    auto prepared = ScenePreparation::PrepareScene(scene, impl.Resources, error);
    Require(prepared.OutlineDraws.size() == 4 && prepared.Draws[0].OutlineMaskId == prepared.Draws[1].OutlineMaskId
        && prepared.Draws[2].OutlineMaskId != prepared.Draws[0].OutlineMaskId, "full 64-bit ID remapping");
    pixels = frame(scene);
    mask = reader.Mask(impl);
    Require(mask[32 * 64 + 47] != mask[32 * 64 + 48] && mask[32 * 64 + 48] != 0,
        "touching objects retain distinct nonzero IDs");
    Require(Yellow(pixels[32 * 64 + 47]) && Yellow(pixels[32 * 64 + 48]), "contact boundary is visible");
    std::cout << "PASS: high-bit ID collision, distinct touching objects and contact boundary\n";

    scene.items.resize(2);
    Add(scene, occluder, opaque, 3, false);
    frame(scene);
    mask = reader.Mask(impl);
    Require(std::all_of(mask.begin(), mask.end(), [](uint32_t id) { return id == 0; }), "fully occluded target has no mask");
    scene.items.resize(2);
    const auto partial = Quad(renderer, 32, 16, 48, 48, .25f);
    Add(scene, partial, opaque, 3, false);
    frame(scene);
    mask = reader.Mask(impl);
    Require(mask[32 * 64 + 24] != 0 && mask[32 * 64 + 40] == 0, "only visible part enters mask");
    auto before = reader.ReadAll(impl, impl.DepthTexture.Get(), true);
    prepared = ScenePreparation::PrepareScene(scene, impl.Resources, error);
    auto draw = impl.GetDrawContext();
    draw.BonePalettes = prepared.BonePalettes;
    OutlinePass::DrawMask(prepared, draw, impl.OutlineMaskTarget.Get(), impl.DepthStencilView.Get(),
        64, 64, impl.OutlineMaskBuffer.Get());
    Require(before == reader.ReadAll(impl, impl.DepthTexture.Get(), true), "mask pass preserves scene depth bits");
    std::cout << "PASS: fully/partly occluded targets and unchanged scene depth\n";

    scene.items.clear();
    const std::array<uint8_t, 8> alpha{255,255,255,0, 255,255,255,255};
    auto texture = renderer.CreateTexture(2, 1, TextureFormat::RGBA8_UNORM, alpha);
    desc.alphaMode = AlphaMode::Masked;
    desc.baseColor = {1, 1, 1, .8f};
    desc.alphaCutoff = .5f;
    const auto masked = renderer.CreateMaterial(desc, texture);
    Add(scene, quad, masked, 7);
    frame(scene);
    mask = reader.Mask(impl);
    Require(mask[32 * 64 + 24] == 0 && mask[32 * 64 + 40] != 0, "masked alpha texture cutout");
    reader.CheckCoverage(impl);
    desc.alphaCutoff = .9f; // Above base alpha, even fully opaque texels must clip.
    scene.items.clear();
    Add(scene, quad, renderer.CreateMaterial(desc, texture), 7);
    frame(scene);
    mask = reader.Mask(impl);
    Require(std::all_of(mask.begin(), mask.end(), [](uint32_t id) { return id == 0; }), "base alpha and cutoff both applied");
    std::cout << "PASS: Masked texture/base alpha/cutoff matches main-pass coverage pixel for pixel\n";

    const auto skinned = Quad(renderer, 16, 16, 48, 48, .5f, true);
    scene.items.clear();
    scene.bonePalettes = { Matrix4::Translation({.25f, 0, 0}) };
    for (uint32_t part = 0; part < 2; ++part)
    {
        SkinnedMeshItem item{skinned, part, masked, Matrix4::Identity, 0, 1};
        item.objectId = 9;
        item.outlineEnabled = true;
        scene.skinnedItems.push_back(item);
    }
    frame(scene);
    mask = reader.Mask(impl);
    Require(mask[32 * 64 + 44] != 0 && mask[32 * 64 + 50] != 0 && mask[32 * 64 + 20] == 0,
        "skinned palette transform and Masked coverage");
    reader.CheckCoverage(impl);
    std::cout << "PASS: skinned submeshes, bone translation and alpha-tested coverage\n";

    auto compare = [&](const RenderScene& selected, bool mixed = false, bool checkCoverage = true, bool requirePartial = true)
    {
        auto unselected = selected;
        for (auto& item : unselected.items) item.outlineEnabled = false;
        for (auto& item : unselected.skinnedItems) item.outlineEnabled = false;
        const auto baseline = frame(unselected);
        const auto outlined = frame(selected);
        auto snapshot = ScenePreparation::PrepareScene(selected, impl.Resources, error);
        const auto mixedEdgeStyles = CheckComposite(impl, reader, baseline, outlined, snapshot, mixed, requirePartial);
        if (checkCoverage) reader.CheckCoverage(impl);
        return mixedEdgeStyles;
    };
    RenderScene subpixel;
    subpixel.ambientIntensity = .1f;
    const auto slanted = Quad(renderer, 0, 0, 0, 0, .5f, false, true);
    Add(subpixel, slanted, opaque, 1);
    subpixel.items[1].material = otherMaterial;
    compare(subpixel);
    // Separate IDs occupy different samples of the SAME pixel along the shared edge.
    subpixel.items.clear();
    Add(subpixel, Quad(renderer, 16, 16, 32.25f, 48), opaque, 1);
    Add(subpixel, Quad(renderer, 32.25f, 16, 48, 48), otherMaterial, (uint64_t(1) << 32) | 1);
    compare(subpixel, true);
    subpixel.items.clear();
    Add(subpixel, slanted, opaque, 1);
    Add(subpixel, Quad(renderer, 32.25f, 0, 64, 64, .25f), opaque, 2, false);
    compare(subpixel, false, false);
    const auto visibility = reader.ReadAll(impl, impl.OutlineMaskTexture.Get());
    const auto occlusionDepth = reader.ReadAll(impl, impl.DepthTexture.Get(), true);
    for (size_t i = 0; i < visibility.size(); ++i)
    {
        float z;
        std::memcpy(&z, &occlusionDepth[i], sizeof(z));
        if (z < .4f) Require(visibility[i] == 0, "occluded samples never enter mask");
        if (z > .4f && z < .6f) Require(visibility[i] != 0, "unoccluded target samples remain visible");
    }
    subpixel.items.clear();
    subpixel.bonePalettes = {Matrix4::Translation({.03125f, .015625f, 0})};
    const auto slantedSkin = Quad(renderer, 0, 0, 0, 0, .5f, true, true);
    for (uint32_t part = 0; part < 2; ++part)
    {
        SkinnedMeshItem item{slantedSkin, part, masked, Matrix4::Identity, 0, 1};
        item.objectId = 17;
        item.outlineEnabled = true;
        item.outlineStyle.Color = {.1f, .7f, .3f};
        item.outlineStyle.Thickness = 3;
        item.outlineStyle.Opacity = .4f;
        subpixel.skinnedItems.push_back(item);
    }
    compare(subpixel);
    std::cout << "PASS: slanted/subpixel silhouette, same-pixel contact IDs, sample occlusion, skinned Masked edges\n";

    RenderScene styled;
    styled.ambientIntensity = .1f;
    Add(styled, Quad(renderer, 16, 16, 32.25f, 48), opaque, 100);
    Add(styled, Quad(renderer, 32.25f, 16, 48, 48), opaque, 101);
    for (size_t i = 0; i < styled.items.size(); ++i)
    {
        styled.items[i].outlineStyle.Color = i < 2 ? Vector3{1,0,0} : Vector3{0,0,1};
        styled.items[i].outlineStyle.Thickness = i < 2 ? 2 : 5;
        styled.items[i].outlineStyle.Opacity = i < 2 ? .25f : .75f;
    }
    auto styledPrepared = ScenePreparation::PrepareScene(styled, impl.Resources, error);
    Require(styledPrepared.OutlineStyles.size() == 3 && styledPrepared.OutlineStyles[0].Opacity == 0
        && styledPrepared.Draws[0].OutlineMaskId == styledPrepared.Draws[1].OutlineMaskId
        && styledPrepared.OutlineStyles[1].Color.X == 1 && styledPrepared.OutlineStyles[2].Thickness == 5
        && styledPrepared.OutlineStyles[2].Opacity == .75f && styledPrepared.OutlineMaxThickness == 5,
        "one sanitized style per component ID, shared by all its submeshes");
    const auto mixedStylePixels = compare(styled, true);
    if (sampleCount > 1) Require(mixedStylePixels > 0, "same output pixel combines different colors AND opacities");
    auto staticAndSkin = styled;
    staticAndSkin.items.resize(2);
    staticAndSkin.bonePalettes = {Matrix4::Identity};
    const auto styledSkin = Quad(renderer, 32.25f, 16, 48, 48, .5f, true);
    for (uint32_t part = 0; part < 2; ++part)
    {
        SkinnedMeshItem item{styledSkin, part, opaque, Matrix4::Identity, 0, 1};
        item.objectId = 101;
        item.outlineEnabled = true;
        item.outlineStyle = styled.items[2].outlineStyle;
        staticAndSkin.skinnedItems.push_back(item);
    }
    compare(staticAndSkin, true);
    D3D11_BUFFER_DESC styleDesc{};
    impl.OutlineStyleBuffer->GetDesc(&styleDesc);
    Require(styleDesc.StructureByteStride == 32 && (styleDesc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED),
        "GPU style layout matches CPU/HLSL");
    ComPtr<ID3D11Buffer> reusedStyles = impl.OutlineStyleBuffer;
    for (auto& item : styled.items)
    {
        item.outlineStyle.Color = {0,1,0};
        item.outlineStyle.Thickness = 1;
        item.outlineStyle.Opacity = .5f;
    }
    compare(styled, true);
    Require(impl.OutlineStyleBuffer.Get() == reusedStyles.Get() && impl.OutlineMaxThickness == 1,
        "runtime color/thickness/opacity update reuses allocation and shrinks search radius");
    styled.items.resize(2);
    for (float opacity : {0.0f, .25f, .5f, 1.0f})
    {
        for (auto& item : styled.items) item.outlineStyle.Opacity = opacity;
        compare(styled, false, opacity > 0, false);
        if (opacity == 0) Require(!impl.OutlineMaskReady && impl.OutlineStyleCount == 0,
            "fully transparent outlines skip mask/composite");
    }
    for (auto& item : styled.items) item.outlineEnabled = false;
    compare(styled, false, false, false);
    std::cout << "PASS: per-object color/width/opacity, MSAA opacity-weighted colors, dynamic changes, fades and zero work at opacity 0\n";

    RenderScene overlap;
    Add(overlap, Quad(renderer, 16, 16, 32, 48), opaque, 300);
    Add(overlap, Quad(renderer, 33, 16, 48, 48), opaque, 301);
    for (size_t i = 0; i < overlap.items.size(); ++i)
    {
        overlap.items[i].outlineStyle.Color = i < 2 ? Vector3{1,0,0} : Vector3{0,0,1};
        overlap.items[i].outlineStyle.Thickness = 6;
    }
    compare(overlap, false, true, false);
    pixels = Read(impl, impl.DisplayTexture.Get());
    Require((pixels[32 * 64 + 32] & 0xFFFFFF) == 0x0000FF, "equal-distance overlap uses lower ID (red)");
    overlap.items[0].mesh = overlap.items[1].mesh = Quad(renderer, 16, 16, 28, 48);
    overlap.items[2].mesh = overlap.items[3].mesh = Quad(renderer, 35, 16, 48, 48);
    compare(overlap, false, true, false);
    pixels = Read(impl, impl.DisplayTexture.Get());
    Require((pixels[32 * 64 + 32] & 0xFFFFFF) == 0xFF0000, "closer boundary wins over lower ID (blue)");
    for (size_t i = 2; i < overlap.items.size(); ++i) overlap.items[i].outlineStyle.Opacity = 0;
    compare(overlap, false, false, false);
    pixels = Read(impl, impl.DisplayTexture.Get());
    Require((pixels[32 * 64 + 32] & 0xFFFFFF) == 0x0000FF, "invisible nearer candidate cannot hide visible farther outline");
    std::cout << "PASS: overlapping outlines select nearest eligible style, deterministic ties and ignore opacity-zero candidates\n";

    RenderScene many;
    const auto tiny = Quad(renderer, 1, 1, 5, 5);
    for (uint32_t object = 0; object < 26; ++object)
    {
        Add(many, tiny, opaque, 1000 + object);
        for (size_t i = many.items.size() - 2; i < many.items.size(); ++i)
        {
            many.items[i].world = Matrix4::Translation({float(object % 8 * 7) / 32, -float(object / 8 * 7) / 32, 0});
            many.items[i].outlineStyle.Color = {float(object % 3) / 2, .3f, .8f};
            many.items[i].outlineStyle.Thickness = 1 + object % 3;
            many.items[i].outlineStyle.Opacity = .2f + float(object % 4) / 4;
        }
    }
    compare(many, false, true, false);
    Require(impl.OutlineStyleCapacity >= 27 && impl.OutlineStyleCount == 27, "style buffer grows for additional objects");
    const auto grownCapacity = impl.OutlineStyleCapacity;
    compare(overlap, false, false, false);
    Require(impl.OutlineStyleCapacity == grownCapacity && impl.OutlineStyleCount == 2,
        "shrinking style table reuses capacity with bounded current style count");
    auto noOutline = overlap;
    for (auto& item : noOutline.items) item.outlineEnabled = false;
    const auto baselineSnapshot = frame(noOutline);
    const auto preparedSnapshot = ScenePreparation::PrepareScene(overlap, impl.Resources, error);
    Require(renderer.BeginFrame(), "snapshot frame");
    renderer.DrawScene(overlap);
    overlap = {}; // EndFrame cannot borrow CPU styles from the submitted scene.
    renderer.EndFrame();
    CheckComposite(impl, reader, baselineSnapshot, Read(impl, impl.DisplayTexture.Get()), preparedSnapshot, false, false);
    ComPtr<ID3D11ShaderResourceView> boundStyles;
    impl.Context->PSGetShaderResources(ShaderConstants::OutlineStylesSlot, 1, &boundStyles);
    Require(!boundStyles, "style SRV unbound after composite");
    std::cout << "PASS: style buffer growth/shrink, current-frame ownership, SRV unbinding\n";

    scene.skinnedItems.clear();
    desc.alphaMode = AlphaMode::Transparent;
    Add(scene, quad, renderer.CreateMaterial(desc), 5);
    prepared = ScenePreparation::PrepareScene(scene, impl.Resources, error);
    Require(prepared.OutlineDraws.empty(), "Transparent excluded from outline queue");
    frame(scene);
    Require(!impl.OutlineMaskReady, "no stale mask after Transparent-only frame");
    scene.items.clear();
    Add(scene, quad, opaque, 1);
#if CANAPE_RENDER_DEBUG
    RendererDebugSettings debug{};
    debug.enabled = true;
    debug.viewMode = DebugViewMode::WorldNormal;
    renderer.SetDebugSettings(debug);
    pixels = frame(scene);
    Require(!impl.OutlineMaskReady && !Yellow(pixels[32 * 64 + 15]), "non-Final debug suppresses mask and composite");
    debug.viewMode = DebugViewMode::Final;
    renderer.SetDebugSettings(debug);
#endif
    pixels = frame(scene);
    Require(Yellow(pixels[32 * 64 + 15]), "Final view restores outline");
#if CANAPE_RENDER_DEBUG
    const auto outlinedCalls = impl.CompletedStats.drawCalls;
    for (auto& item : scene.items) item.outlineEnabled = false;
    pixels = frame(scene);
    Require(!impl.OutlineMaskReady && !Yellow(pixels[32 * 64 + 15]), "disabled outline does not reuse cached mask");
    Require(impl.CompletedStats.drawCalls + 3 == outlinedCalls, "disabled frame skips two mask draws and composition");
    for (auto& item : scene.items) item.outlineEnabled = true;
#endif
    renderer.SetSceneSize(128, 96);
    pixels = frame(scene);
    D3D11_TEXTURE2D_DESC maskDesc{};
    impl.OutlineMaskTexture->GetDesc(&maskDesc);
    Require(maskDesc.Width == 128 && maskDesc.Height == 96 && maskDesc.Format == DXGI_FORMAT_R32_UINT,
        "viewport resize recreates mask at scene resolution");
    Require(maskDesc.SampleDesc.Count == impl.SelectedSceneSampleDesc.Count
        && maskDesc.SampleDesc.Quality == impl.SelectedSceneSampleDesc.Quality, "mask matches actual scene samples/quality");
    Require(Yellow(pixels[48 * 128 + 30]) && !Yellow(pixels[48 * 128 + 29]), "resized output still has two-pixel outline");
    renderer.SetSceneSize(64, 64);
    pixels = frame(scene);
    Require(Yellow(pixels[32 * 64 + 15]), "resize back preserves outline");
    std::cout << "PASS: Transparent exclusion, empty-frame reset, debug Final gating, viewport resize\n";

    // Both simulation hooks must finish before the mask, without affecting its final output.
    unsigned callbackStage = 0;
    renderer.SetAfterOpaqueCallback([&]
    {
        Require(callbackStage == 0 && !impl.OutlineMaskReady, "opaque hook precedes mask");
        callbackStage = 1;
    });
    renderer.SetAfterTransparentCallback([&]
    {
        Require(callbackStage == 1 && !impl.OutlineMaskReady, "transparent hook follows opaque, before mask");
        callbackStage = 2;
    });
    pixels = frame(scene);
    Require(callbackStage == 2 && Yellow(pixels[32 * 64 + 15]), "both hooks and outline execute together");
    renderer.SetAfterOpaqueCallback({});
    renderer.SetAfterTransparentCallback({});

    // The shared scene depth must remain available to world UI in Debug and Shipping.
    UIDrawList worldUI;
    worldUI.Vertices = {{{14,30},{0,0},0xFF0000FF}, {{24,30},{0,0},0xFF0000FF},
        {{24,34},{0,0},0xFF0000FF}, {{14,34},{0,0},0xFF0000FF}};
    worldUI.Indices = {0,1,2,0,2,3};
    worldUI.Commands.push_back({{}, {}, 0, 6, 0});
    UIDrawTransform worldTransform;
    worldTransform.ClipFromCanvas[0] = 2.0f / 64;
    worldTransform.ClipFromCanvas[5] = -2.0f / 64;
    worldTransform.ClipFromCanvas[10] = 1;
    worldTransform.ClipFromCanvas[12] = -1;
    worldTransform.ClipFromCanvas[13] = 1;
    worldTransform.ClipFromCanvas[14] = .75f;
    worldTransform.ClipFromCanvas[15] = 1;
    worldUI.Transforms.push_back(worldTransform);
    const auto beforeUI = pixels;
    auto worldUIFrame = [&]
    {
        Require(renderer.BeginFrame(), "world UI frame");
        renderer.DrawScene(scene);
        renderer.SetUIDrawList(&worldUI);
        renderer.EndFrame();
        return Read(impl, impl.DisplayTexture.Get());
    };
    pixels = worldUIFrame();
    Require((pixels[32 * 64 + 15] & 0xFFFFFF) == 0x0000FF, "world UI covers finished outline outside geometry");
    Require(pixels[32 * 64 + 20] == beforeUI[32 * 64 + 20], "occluded world UI uses scene depth after mask");
    worldUI.Transforms[0].ClipFromCanvas[14] = .25f;
    pixels = worldUIFrame();
    Require((pixels[32 * 64 + 20] & 0xFFFFFF) == 0x0000FF, "foreground world UI remains visible");

    bool headlessOverlay = false;
    renderer.SetOverlay([&] { headlessOverlay = true; });
    impl.Headless = true;
    const auto beforeHeadless = Read(impl, impl.DisplayTexture.Get());
    frame(RenderScene{});
    Require(!headlessOverlay && Read(impl, impl.DisplayTexture.Get()) == beforeHeadless,
        "headless skips final output, UI and overlay");
    impl.Headless = false;
    std::cout << "PASS: simulation hooks coexist with mask, world UI depth and headless output gating\n";

    // UI must cover the finished outline, and the editor overlay must observe that result.
    UIDrawList ui;
    ui.Vertices = {{{14,30},{0,0},0xFF0000FF}, {{16,30},{0,0},0xFF0000FF},
        {{16,34},{0,0},0xFF0000FF}, {{14,34},{0,0},0xFF0000FF}};
    ui.Indices = {0,1,2,0,2,3};
    ui.Commands.push_back({{}, {0,0,64,64}, 0, 6});
    bool overlayObserved = false;
    renderer.SetOverlay([&]
    {
        const auto output = Read(impl, impl.DisplayTexture.Get());
        Require((output[32 * 64 + 15] & 0xFFFFFF) == 0x0000FF, "UI covers outline before overlay");
        overlayObserved = true;
    });
    Require(renderer.BeginFrame(), "UI frame");
    renderer.DrawScene(scene);
    renderer.SetUIDrawList(&ui);
    renderer.EndFrame();
    Require(overlayObserved, "editor overlay ran");
    renderer.SetOverlay({});
    Require(renderer.BeginFrame(), "game frame");
    renderer.DrawScene(scene);
    impl.ResolveSceneColor();
    impl.ExecutePostProcessing(impl.RenderTargetView.Get(), 64, 64);
    ComPtr<ID3D11Texture2D> backBuffer;
    Require(SUCCEEDED(impl.SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))), "back buffer");
    pixels = Read(impl, backBuffer.Get());
    Require(Yellow(pixels[32 * 64 + 15]), "game back-buffer final output includes outline");
    backBuffer.Reset();
    renderer.EndFrame();
    renderer.SetOverlay([] {});
    // Exercise stale-resource detection when the actual sample description changes at equal size.
    for (UINT count : {sampleCount == 4 ? 1u : 4u, sampleCount})
    {
        ComPtr<ID3D11Texture2D> oldMask = impl.OutlineMaskTexture;
        impl.SelectedSceneSampleDesc = {count, 0};
        Require(impl.CreateSceneTargets(64, 64), "recreate scene sample count");
        pixels = frame(scene);
        impl.OutlineMaskTexture->GetDesc(&maskDesc);
        Require(maskDesc.SampleDesc.Count == count && oldMask.Get() != impl.OutlineMaskTexture.Get(),
            "sample-count change recreates mask even at equal size");
        Require(Yellow(pixels[32 * 64 + 15]), "outline survives actual sample-count transition");
    }
    CheckMessages(messages.Get());
    renderer.Shutdown();
    Require(!impl.OutlineMaskTexture && !impl.OutlineMaskView && !impl.OutlineMaskTarget
        && !impl.OutlineMaskBuffer && !impl.OutlineCompositeBuffer && !impl.OutlineStyleBuffer
        && !impl.OutlineStyleView && impl.OutlineStyleCapacity == 0, "shutdown releases all outline resources");
    CheckMessages(messages.Get());
    DestroyWindow(window);
    std::cout << "PASS: DisplayTexture/game back buffer, tone-map order, UI/overlay order, shutdown, clean D3D11 debug layer\n";
}

int main(int argc, char** argv)
{
    try
    {
        const bool hardware = argc > 1 && std::strcmp(argv[1], "--hardware") == 0;
        for (UINT count : {1u, 4u})
        {
            std::cout << (hardware ? "Hardware " : "WARP ") << count << "x checks\n";
            Run(count, hardware ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP);
        }
        return 0;
    }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
