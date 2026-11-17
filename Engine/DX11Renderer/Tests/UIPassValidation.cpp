// Standalone UI pass integration test (WARP + D3D11 debug layer); not part of DX11Renderer.vcxproj.
// Same private-access pattern as ShadowValidation: confined to this translation unit, links the real library.
#define NOMINMAX
#include "DX11Renderer/Data/TextureData.h"
#include "DX11Renderer/Data/UIDrawList.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <d3d11sdklayers.h>
#include <iostream>
#include <span>
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
    void Write(MainTag, std::string_view tag, std::string message, const char*, int)
    {
        if (tag == "Error" || tag == "UI" || tag == "Resource")
            std::cerr << message << '\n';
    }
}

using namespace Canape;
using Backend = DX11Renderer::Impl;

namespace
{
    constexpr uint32_t kWidth = 160;
    constexpr uint32_t kHeight = 64;

    void Require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    struct Pixel
    {
        int r, g, b, a;
    };

    struct Image
    {
        uint32_t width = 0, height = 0;
        std::vector<unsigned char> rgba;

        Pixel At(uint32_t x, uint32_t y) const
        {
            const unsigned char* p = rgba.data() + (size_t(y) * width + x) * 4;
            return { p[0], p[1], p[2], p[3] };
        }
    };

    Image ReadDisplay(Backend& impl)
    {
        D3D11_TEXTURE2D_DESC desc{};
        impl.DisplayTexture->GetDesc(&desc);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Raw bytes, no sRGB decode.
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ComPtr<ID3D11Texture2D> staging;
        Require(SUCCEEDED(impl.Device->CreateTexture2D(&desc, nullptr, &staging)), "staging texture");
        impl.Context->CopyResource(staging.Get(), impl.DisplayTexture.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        Require(SUCCEEDED(impl.Context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map)), "map staging");
        Image image{ desc.Width, desc.Height, std::vector<unsigned char>(size_t(desc.Width) * desc.Height * 4) };
        for (uint32_t row = 0; row < desc.Height; ++row)
            std::memcpy(image.rgba.data() + size_t(row) * desc.Width * 4,
                static_cast<const unsigned char*>(map.pData) + size_t(row) * map.RowPitch, desc.Width * 4);
        impl.Context->Unmap(staging.Get(), 0);
        return image;
    }

    uint32_t Pack(int r, int g, int b, int a)
    {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }

    void AddQuad(UIDrawList& list, float x, float y, float w, float h, uint32_t color,
        TextureResourceId texture = {}, UIClipRect clip = { 0, 0, int32_t(kWidth), int32_t(kHeight) })
    {
        const auto base = static_cast<uint32_t>(list.Vertices.size());
        list.Vertices.push_back({ { x, y }, { 0, 0 }, color });
        list.Vertices.push_back({ { x + w, y }, { 1, 0 }, color });
        list.Vertices.push_back({ { x + w, y + h }, { 1, 1 }, color });
        list.Vertices.push_back({ { x, y + h }, { 0, 1 }, color });
        const auto offset = static_cast<uint32_t>(list.Indices.size());
        list.Indices.insert(list.Indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
        list.Commands.push_back({ texture, clip, offset, 6 });
    }

    bool Near(const Pixel& p, int r, int g, int b, int tolerance = 1)
    {
        return std::abs(p.r - r) <= tolerance && std::abs(p.g - g) <= tolerance && std::abs(p.b - b) <= tolerance;
    }

    void Expect(const Pixel& p, int r, int g, int b, const char* what, int tolerance = 1)
    {
        if (!Near(p, r, g, b, tolerance))
        {
            std::cerr << what << ": got (" << p.r << ',' << p.g << ',' << p.b << ") expected ("
                      << r << ',' << g << ',' << b << ")\n";
            throw std::runtime_error(what);
        }
    }

    Image Frame(DX11Renderer& renderer, const UIDrawList* list)
    {
        Require(renderer.BeginFrame(), "BeginFrame");
        renderer.SetUIDrawList(list);
        renderer.EndFrame();
        Require(renderer.m_Impl->PendingUI == nullptr, "EndFrame clears the borrowed UI list");
        return ReadDisplay(*renderer.m_Impl);
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
                std::cerr << "D3D11: " << message->pDescription << '\n';
            }
        }
        Require(warnings == 0, "D3D11 debug-layer warning/error");
    }

    void UIChecks()
    {
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        WNDCLASSW wc{};
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = instance;
        wc.lpszClassName = L"CanapeUIValidation";
        RegisterClassW(&wc);
        HWND window = CreateWindowExW(0, wc.lpszClassName, L"UI validation", WS_POPUP, 0, 0, 256, 256,
            nullptr, nullptr, instance, nullptr);
        Require(window != nullptr, "hidden window");

        DX11Renderer renderer;
        auto& impl = *renderer.m_Impl;
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
            &impl.SwapChain, &impl.Device, nullptr, &impl.Context)), "WARP debug device");
        ComPtr<ID3D11InfoQueue> queue;
        Require(SUCCEEDED(impl.Device.As(&queue)), "debug info queue");
        Require(impl.CreateTargets(256, 256) && impl.CreatePipeline() && impl.CreateSkyboxQuad(), "targets and pipeline");
        impl.Initialized = true;
        Require(impl.Pipelines.UIAvailable(), "UI pipeline compiled");

        // Overlay mode renders into DisplayTexture, which survives Present for readback.
        renderer.SetOverlay([] {});
        renderer.SetSceneSize(kWidth, kHeight);

        const Image empty = Frame(renderer, nullptr);
        Require(impl.Width == kWidth && impl.Height == kHeight, "scene resize");
        Require(impl.DisplayUIRenderTargetView != nullptr, "display UI view");
        const Pixel background = empty.At(150, 60);
        Require(empty.At(5, 5).r == background.r, "uniform background");

        // 1x1 textures: same bytes, one UNORM and one sRGB. Both must display the authored bytes.
        const std::array<uint8_t, 4> texel{ 128, 64, 32, 255 };
        const auto unormTexture = renderer.CreateTexture(1, 1, TextureFormat::RGBA8_UNORM, texel);
        const auto srgbTexture = renderer.CreateTexture(1, 1, TextureFormat::RGBA8_SRGB, texel);
        const auto destroyedTexture = renderer.CreateTexture(1, 1, TextureFormat::RGBA8_UNORM, texel);
        Require(unormTexture.IsValid() && srgbTexture.IsValid() && destroyedTexture.IsValid(), "UI textures");
        renderer.DestroyTexture(destroyedTexture);

        // 2x2 texture mapped 1:1 so each pixel center samples one texel center.
        const std::array<uint8_t, 16> quadTexels{ 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 };
        const auto atlas = renderer.CreateTexture(2, 2, TextureFormat::RGBA8_UNORM, quadTexels);
        Require(atlas.IsValid(), "atlas texture");
        const std::array<uint8_t, 4> updated{ 10, 20, 30, 255 };
        Require(renderer.UpdateTexture(atlas, 1, 1, 1, 1, updated), "UpdateTexture");
        Require(!renderer.UpdateTexture(atlas, 2, 0, 1, 1, updated), "UpdateTexture rejects out-of-bounds");
        Require(!renderer.UpdateTexture(atlas, 0, 0, 2, 1, updated), "UpdateTexture rejects size mismatch");

        UIDrawList list;
        AddQuad(list, 2, 2, 12, 12, Pack(255, 0, 0, 255));                       // opaque solid
        AddQuad(list, 20, 2, 12, 12, Pack(255, 255, 255, 128));                  // 50% white
        AddQuad(list, 38, 2, 12, 12, Pack(255, 255, 255, 255), unormTexture);    // UNORM texture
        AddQuad(list, 56, 2, 12, 12, Pack(255, 255, 255, 255), srgbTexture);     // sRGB texture, re-encoded
        AddQuad(list, 74, 2, 12, 12, Pack(255, 255, 255, 255), destroyedTexture);// skipped
        AddQuad(list, 92, 2, 40, 12, Pack(0, 0, 255, 255), {}, { 92, 0, 112, int32_t(kHeight) }); // scissor
        AddQuad(list, 140, 2, 2, 2, Pack(255, 255, 255, 255), atlas);            // 1:1 texels
        AddQuad(list, 2, 20, 12, 12, Pack(255, 255, 255, 255), unormTexture);    // later command on top...
        AddQuad(list, 6, 24, 4, 4, Pack(0, 255, 0, 255));                       // ...painter's order
        // Out-of-range command: must be skipped, not read past the index buffer.
        list.Commands.push_back({ {}, { 0, 0, int32_t(kWidth), int32_t(kHeight) }, uint32_t(list.Indices.size()), 6 });

        const Image ui = Frame(renderer, &list);
        Expect(ui.At(8, 8), 255, 0, 0, "opaque solid quad", 0);
        // Pixel-exact edges: [2, 14) covers 2..13.
        Expect(ui.At(2, 2), 255, 0, 0, "quad covers its top-left pixel", 0);
        Expect(ui.At(13, 13), 255, 0, 0, "quad covers its bottom-right pixel", 0);
        Expect(ui.At(1, 2), background.r, background.g, background.b, "quad excludes the pixel left of it", 0);
        Expect(ui.At(14, 2), background.r, background.g, background.b, "right edge is exclusive", 0);
        // UNORM-view blending happens on sRGB bytes: 128/255 white over the background.
        const float alpha = 128.0f / 255.0f;
        Expect(ui.At(26, 8), int(std::lround(255 * alpha + background.r * (1 - alpha))),
            int(std::lround(255 * alpha + background.g * (1 - alpha))),
            int(std::lround(255 * alpha + background.b * (1 - alpha))), "gamma-space alpha blend");
        Expect(ui.At(44, 8), 128, 64, 32, "UNORM texture bytes");
        Expect(ui.At(62, 8), 128, 64, 32, "sRGB texture re-encoded to authored bytes", 2);
        Expect(ui.At(80, 8), background.r, background.g, background.b, "destroyed texture skipped", 0);
        Expect(ui.At(100, 8), 0, 0, 255, "inside scissor", 0);
        Expect(ui.At(120, 8), background.r, background.g, background.b, "outside scissor", 0);
        Expect(ui.At(140, 2), 255, 0, 0, "atlas texel (0,0)", 0);
        Expect(ui.At(141, 2), 0, 255, 0, "atlas texel (1,0)", 0);
        Expect(ui.At(140, 3), 0, 0, 255, "atlas texel (0,1)", 0);
        Expect(ui.At(141, 3), 10, 20, 30, "atlas texel (1,1) after UpdateTexture", 0);
        Expect(ui.At(7, 25), 0, 255, 0, "later command draws on top", 0);
        Expect(ui.At(3, 21), 128, 64, 32, "earlier command visible around it", 0);
        std::cout << "UI pass: solid, gamma blend, UNORM/sRGB textures, scissor, order, UpdateTexture PASS\n";

        // The list is borrowed for one frame only.
        const Image next = Frame(renderer, nullptr);
        Expect(next.At(8, 8), background.r, background.g, background.b, "UI list not retained", 0);

        // Resize recreates the UNORM display view; the non-overlay path draws to the back buffer view.
        renderer.SetSceneSize(96, 48);
        const Image resized = Frame(renderer, &list);
        Require(resized.width == 96 && resized.height == 48, "resized display");
        Expect(resized.At(8, 8), 255, 0, 0, "UI after scene resize", 0);
        renderer.SetSceneSize(0, 0);
        renderer.SetOverlay({});
        Require(renderer.BeginFrame(), "BeginFrame without overlay");
        renderer.SetUIDrawList(&list);
        renderer.EndFrame();
        Require(impl.PendingUI == nullptr, "non-overlay EndFrame clears the UI list");

        // Growth: more geometry than the initial 1024-vertex buffer.
        UIDrawList large;
        for (int i = 0; i < 600; ++i)
            AddQuad(large, float(i % 150), float(i / 150), 1, 1, Pack(255, 255, 0, 255));
        const Image grown = Frame(renderer, &large);
        (void)grown;
        Require(impl.UIBuffers.VertexCapacity >= 2400, "UI vertex buffer grows");

        renderer.DestroyTexture(unormTexture);
        renderer.DestroyTexture(srgbTexture);
        renderer.DestroyTexture(atlas);
        CheckMessages(queue.Get());
        std::cout << "UI pass: resize, back buffer path, buffer growth, D3D11 debug layer clean PASS\n";
        DestroyWindow(window);
    }
}

int main()
{
    try
    {
        UIChecks();
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
