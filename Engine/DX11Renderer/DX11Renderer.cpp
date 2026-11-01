#include "DX11Renderer/DX11Renderer.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <d3d11.h>
#include "DX11Renderer/Pipeline/DX11PipelineState.h"
#if CANAPE_RENDER_DEBUG
#include "DX11Renderer/Pipeline/GPUProfiler.h"
#endif
#include "DX11Renderer/Resource/DX11ResourceCache.h"
#include <DirectXMath.h>
#include <wrl/client.h>

#include <sstream>
#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

#include "CNDebug/DebugLog.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace Canape
{
    namespace
    {
        // Temporary asset copied beside Game.exe by Game.vcxproj's Content post-build step.
        constexpr wchar_t TestSkyboxDds[] = L"Content\\HDRI\\uffizi_cross.dds";

        struct SkyboxVertex { float Position[2]; };
        static_assert(sizeof(SkyboxVertex) == sizeof(float) * 2);

        uint32_t DdsWord(const std::array<uint8_t, 128>& header, size_t offset)
        {
            uint32_t value;
            std::memcpy(&value, header.data() + offset, sizeof(value));
            return value;
        }

        bool LoadDDSCubeMap(ID3D11Device* device, const std::filesystem::path& path,
            DX11TextureResource& output, std::string& error)
        {
            output = {};
            const auto utf8Path = path.u8string();
            const std::string name(utf8Path.begin(), utf8Path.end());
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file)
            {
                error = "DDS CubeMap file missing or unreadable: " + name;
                return false;
            }
            const auto fileSize = file.tellg();
            if (fileSize < 128)
            {
                error = "DDS CubeMap header is truncated: " + name;
                return false;
            }
            file.seekg(0);
            std::array<uint8_t, 128> header{};
            if (!file.read(reinterpret_cast<char*>(header.data()), header.size())
                || std::memcmp(header.data(), "DDS ", 4) != 0
                || DdsWord(header, 4) != 124 || DdsWord(header, 76) != 32)
            {
                error = "Invalid DDS header: " + name;
                return false;
            }
            constexpr uint32_t AllCubeFaces = 0x0000FE00;
            if ((DdsWord(header, 112) & AllCubeFaces) != AllCubeFaces)
            {
                error = "DDS is not a complete six-face CubeMap: " + name;
                return false;
            }
            const uint32_t width = DdsWord(header, 16);
            const uint32_t height = DdsWord(header, 12);
            const uint32_t mipCount = DdsWord(header, 28);
            if (!width || width != height || width > D3D11_REQ_TEXTURECUBE_DIMENSION
                || mipCount > 1)
            {
                error = "DDS CubeMap needs square faces and a single mip level: " + name;
                return false;
            }
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            uint32_t bytesPerPixel = 0;
            const uint32_t pixelFlags = DdsWord(header, 80);
            if ((pixelFlags & 0x4) && DdsWord(header, 84) == 113) // D3DFMT_A16B16G16R16F.
            {
                format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                bytesPerPixel = 8;
            }
            else if ((pixelFlags & 0x41) == 0x41 && DdsWord(header, 88) == 32
                && DdsWord(header, 92) == 0x00FF0000 && DdsWord(header, 96) == 0x0000FF00
                && DdsWord(header, 100) == 0x000000FF && DdsWord(header, 104) == 0xFF000000)
            {
                format = DXGI_FORMAT_B8G8R8A8_UNORM;
                bytesPerPixel = 4;
            }
            if (format == DXGI_FORMAT_UNKNOWN)
            {
                error = "Unsupported DDS CubeMap pixel format: " + name;
                return false;
            }
            const uint64_t rowBytes = uint64_t(width) * bytesPerPixel;
            const uint64_t faceBytes = rowBytes * height;
            const uint64_t totalBytes = faceBytes * 6;
            if (totalBytes > 512ull * 1024 * 1024 || uint64_t(fileSize) < 128 + totalBytes)
            {
                error = "DDS CubeMap pixel data is truncated or too large: " + name;
                return false;
            }
            std::vector<uint8_t> pixels(static_cast<size_t>(totalBytes));
            if (!file.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(totalBytes)))
            {
                error = "Failed to read DDS CubeMap pixels: " + name;
                return false;
            }
            std::array<D3D11_SUBRESOURCE_DATA, 6> faces{};
            for (size_t face = 0; face < faces.size(); ++face)
            {
                faces[face].pSysMem = pixels.data() + face * faceBytes;
                faces[face].SysMemPitch = static_cast<UINT>(rowBytes);
            }
            D3D11_TEXTURE2D_DESC textureDesc{};
            textureDesc.Width = width;
            textureDesc.Height = height;
            textureDesc.MipLevels = 1;
            textureDesc.ArraySize = 6;
            textureDesc.Format = format;
            textureDesc.SampleDesc.Count = 1;
            textureDesc.Usage = D3D11_USAGE_IMMUTABLE;
            textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
            DX11TextureResource pending;
            HRESULT result = device->CreateTexture2D(&textureDesc, faces.data(), &pending.Texture);
            if (FAILED(result))
            {
                error = "CreateTexture2D(DDS CubeMap) failed for " + name
                    + " (HRESULT " + std::to_string(static_cast<unsigned long>(result)) + ')';
                return false;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
            viewDesc.Format = format;
            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
            viewDesc.TextureCube.MipLevels = 1;
            result = device->CreateShaderResourceView(pending.Texture.Get(), &viewDesc,
                &pending.ShaderResourceView);
            if (FAILED(result))
            {
                error = "CreateShaderResourceView(DDS CubeMap) failed for " + name
                    + " (HRESULT " + std::to_string(static_cast<unsigned long>(result)) + ')';
                return false;
            }
            D3D11_SHADER_RESOURCE_VIEW_DESC actualView{};
            pending.ShaderResourceView->GetDesc(&actualView);
            if (actualView.ViewDimension != D3D11_SRV_DIMENSION_TEXTURECUBE)
            {
                error = "DDS CubeMap SRV has an unexpected dimension: " + name;
                return false;
            }
            output = std::move(pending);
            error.clear();
            return true;
        }

        // All temporary light values live here, outside GPU packing/upload code.
        [[maybe_unused]] std::array<RenderLight, 2> CreateTestLights()
        {
            RenderLight directional;
            directional.id = 1;
            directional.type = LightType::Directional;
            directional.direction = { -0.4f, -1.0f, 0.6f };
            directional.color = { 0.85f, 0.85f, 0.9f };
            directional.intensity = 0.9f;
            directional.castShadow = true;

            RenderLight point;
            point.id = 2;
            point.type = LightType::Point;
            point.position = { 2.0f, 3.0f, -2.0f };
            point.color = { 1.0f, 0.45f, 0.18f };
            point.intensity = 2.0f;
            point.range = 12.0f;
            return { directional, point };
        }

        std::span<const RenderLight> SelectLights(const RenderScene& scene)
        {
            // Engine integration: replace this body with: return scene.lights;
            // Both BeginFrame (direct DrawMesh) and DrawScene use this selector.
            return scene.lights;
        }

        bool IsFinite(const Vector3& value)
        {
            return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
        }

        std::string HResultMessage(const char* operation, HRESULT result)
        {
            std::ostringstream stream;
            stream << operation << " failed (HRESULT 0x" << std::hex
                   << static_cast<unsigned long>(result) << ')';
            return stream.str();
        }
    }

    struct DX11Renderer::Impl
    {
        // Renderer-local settings. The camera frustum determines the XY extent;
        // lightDistance and farPlane retain depth room for off-screen casters.
        struct DirectionalShadowSettings
        {
            bool enabled = true;
            uint32_t resolution = 4096*2;
            bool pcf = true; // 3x3 taps with hardware comparison filtering; false selects a single comparison.
            float shadowDistance = 40.0f; // Camera-space extent covered by this shadow map.
            Vector3 center{}; // Legacy fixed-frustum values; fitted projection uses the camera.
            float width = 40.0f;
            float height = 40.0f;
            float lightDistance = 80.0f;
            float nearPlane = 0.1f;
            float farPlane = 200.0f;
            float depthBias = DefaultShadowReceiverBias; // Receiver comparison bias.
            bool pcss = true; // Ctrl+Shift+P switches between the existing PCF and PCSS.
            float lightAngularRadius = 0.02f; // Radians; directional light's virtual angular radius.
            float blockerSearchRadiusWorld = 0.4f;
            float minFilterRadiusWorld = 0.0f; // One shadow texel is the shader-side floor.
            float maxFilterRadiusWorld = 0.4f / 1.5f; // 0.4-world effective cap at the default 1.5x spacing.
            float pcfTapSpacingScale = 1.5f; // Diagnostic multiplier after penumbra estimation.
            uint32_t pcfSampleCount = 64;
            uint32_t blockerSampleCount = 25;
            bool legacyPcf25 = true; // Original 25 offsets remain available for comparison.
        };

        struct ShadowMapResources
        {
            ComPtr<ID3D11Texture2D> Texture;
            ComPtr<ID3D11DepthStencilView> DepthView;
            ComPtr<ID3D11ShaderResourceView> ShaderView;
            ComPtr<ID3D11SamplerState> PointSampler;
            ComPtr<ID3D11SamplerState> PcfSampler;
            ComPtr<ID3D11Buffer> Constants;
            ComPtr<ID3D11Buffer> PcssConstants;
            uint32_t Resolution = 0;
        };

        struct alignas(16) ObjectConstants
        {
            DirectX::XMFLOAT4X4 World;
            DirectX::XMFLOAT4X4 WorldInverseTranspose;
        };
        static_assert(sizeof(ObjectConstants) == 128);
        static_assert(offsetof(ObjectConstants, WorldInverseTranspose) == 64);

        struct alignas(16) ViewConstants
        {
            DirectX::XMFLOAT4X4 View{};
            DirectX::XMFLOAT4X4 Projection{};
            DirectX::XMFLOAT4X4 ViewProjection{};
            Vector3 CameraPosition{};
            float Padding = 0.0f;
        };
        static_assert(sizeof(ViewConstants) == 208);
        static_assert(offsetof(ViewConstants, Projection) == 64);
        static_assert(offsetof(ViewConstants, ViewProjection) == 128);
        static_assert(offsetof(ViewConstants, CameraPosition) == 192);

        // Matches Skybox.hlsl b6: two row-major float4x4 matrices, updated in stage 5.
        struct alignas(16) SkyboxViewConstants
        {
            DirectX::XMFLOAT4X4 InverseProjection{};
            DirectX::XMFLOAT4X4 InverseView{};
        };
        static_assert(sizeof(SkyboxViewConstants) == 128);
        static_assert(alignof(SkyboxViewConstants) == 16);
        static_assert(offsetof(SkyboxViewConstants, InverseView) == 64);

        // Initial fixed capacities. Keep these in sync with Constants.hlsli.
        static constexpr uint32_t MaxDirectionalLights = 4;
        static constexpr uint32_t MaxLocalLights = 64;
        static constexpr uint32_t MaxShadowViews = 64;
        static constexpr uint32_t InvalidShadowView = UINT32_MAX;

        enum class LocalLightType : uint32_t { Point = 0, Spot = 1 };

        struct alignas(16) DirectionalLightGpu
        {
            Vector3 Direction{ 0.0f, -1.0f, 0.0f }; // Normalized world travel direction.
            uint32_t ShadowViewOffset = InvalidShadowView;
            Vector3 ColorIntensity{}; // Linear color multiplied by intensity.
            uint32_t ShadowViewCount = 0; // No shadow: 0. Directional shadow: 1.
        };
        static_assert(sizeof(DirectionalLightGpu) == 32);
        static_assert(offsetof(DirectionalLightGpu, ShadowViewOffset) == 12);
        static_assert(offsetof(DirectionalLightGpu, ColorIntensity) == 16);
        static_assert(offsetof(DirectionalLightGpu, ShadowViewCount) == 28);

        struct alignas(16) LocalLightGpu
        {
            Vector3 Position{};
            float Range = 10.0f;
            Vector3 Direction{ 0.0f, -1.0f, 0.0f }; // Spot only; normalized.
            float OuterAngleCos = 1.0f; // Spot only; cosine of the half-angle.
            Vector3 ColorIntensity{};
            float InnerAngleCos = 1.0f;
            uint32_t Type = static_cast<uint32_t>(LocalLightType::Point);
            uint32_t ShadowViewOffset = InvalidShadowView;
            uint32_t ShadowViewCount = 0; // No shadow: 0. Spot: 1. Point cubemap: 6.
            uint32_t Padding = 0;
        };
        static_assert(sizeof(LocalLightGpu) == 64);
        static_assert(offsetof(LocalLightGpu, Range) == 12);
        static_assert(offsetof(LocalLightGpu, Direction) == 16);
        static_assert(offsetof(LocalLightGpu, OuterAngleCos) == 28);
        static_assert(offsetof(LocalLightGpu, ColorIntensity) == 32);
        static_assert(offsetof(LocalLightGpu, InnerAngleCos) == 44);
        static_assert(offsetof(LocalLightGpu, Type) == 48);
        static_assert(offsetof(LocalLightGpu, ShadowViewOffset) == 52);
        static_assert(offsetof(LocalLightGpu, ShadowViewCount) == 56);

        struct alignas(16) LightConstants
        {
            uint32_t DirectionalCount = 0;
            uint32_t LocalCount = 0;
            uint32_t Padding[2]{};
            DirectionalLightGpu DirectionalLights[MaxDirectionalLights]{};
            LocalLightGpu LocalLights[MaxLocalLights]{};
        };
        static_assert(offsetof(LightConstants, DirectionalLights) == 16);
        static_assert(offsetof(LightConstants, LocalLights) == 16 + 32 * MaxDirectionalLights);
        static_assert(sizeof(LightConstants) == 16 + 32 * MaxDirectionalLights + 64 * MaxLocalLights);

        struct alignas(16) ShadowViewGpu
        {
            DirectX::XMFLOAT4X4 LightViewProjection{}; // LightView * LightProjection.
            float DepthBias = 0.0f;
            uint32_t MapIndex = UINT32_MAX; // 2D-array slice or cube index, according to light type.
            float NearPlane = 0.1f;
            float FarPlane = 10.0f;
        };
        static_assert(sizeof(ShadowViewGpu) == 80);
        static_assert(offsetof(ShadowViewGpu, DepthBias) == 64);
        static_assert(offsetof(ShadowViewGpu, MapIndex) == 68);
        static_assert(offsetof(ShadowViewGpu, NearPlane) == 72);
        static_assert(offsetof(ShadowViewGpu, FarPlane) == 76);

        struct alignas(16) ShadowConstants
        {
            uint32_t ViewCount = 0;
            uint32_t PcfEnabled = 0;
            uint32_t Padding[2]{};
            // Directional/Spot use one view. Point uses six in +X,-X,+Y,-Y,+Z,-Z order.
            ShadowViewGpu Views[MaxShadowViews]{};
        };
        static_assert(offsetof(ShadowConstants, PcfEnabled) == 4);
        static_assert(offsetof(ShadowConstants, Views) == 16);
        static_assert(sizeof(ShadowConstants) == 16 + 80 * MaxShadowViews);
        static_assert(sizeof(LightConstants) <= 65536 && sizeof(ShadowConstants) <= 65536);

        // Mesh.hlsl b7. b5 remains unchanged for all existing shadow consumers.
        struct alignas(16) PcssConstants
        {
            DirectX::XMFLOAT2 OrthoSize{};
            float TanLightAngularRadius = 0.0f;
            float BlockerSearchRadiusWorld = 0.0f;
            float MinFilterRadiusWorld = 0.0f;
            float MaxFilterRadiusWorld = 0.0f;
            uint32_t FilterMode = 0; // 0: single, 1: 3x3 PCF, 2: Vogel PCSS, 3: legacy 25 PCSS.
            uint32_t PcfSampleCount = 64;
            float PcfTapSpacingScale = 1.5f;
            uint32_t BlockerSampleCount = 25;
            float Padding[2]{};
        };
        static_assert(sizeof(PcssConstants) == 48);
        static_assert(offsetof(PcssConstants, TanLightAngularRadius) == 8);
        static_assert(offsetof(PcssConstants, MinFilterRadiusWorld) == 16);
        static_assert(offsetof(PcssConstants, FilterMode) == 24);
        static_assert(offsetof(PcssConstants, PcfSampleCount) == 28);
        static_assert(offsetof(PcssConstants, PcfTapSpacingScale) == 32);
        static_assert(offsetof(PcssConstants, BlockerSampleCount) == 36);

        struct alignas(16) SceneConstants
        {
            Vector3 AmbientColor{ 1.0f, 1.0f, 1.0f };
            float AmbientIntensity = 0.1f;
            Vector3 FogColor{ 0.5f, 0.5f, 0.5f };
            float FogDensity = 0.01f;
            float FogStartDistance = 0.0f;
            float FogEndDistance = 100.0f;
            uint32_t FogMode = static_cast<uint32_t>(Canape::FogMode::Disabled);
            float Padding = 0.0f;
        };
        static_assert(sizeof(SceneConstants) == 48);
        static_assert(offsetof(SceneConstants, AmbientIntensity) == 12);
        static_assert(offsetof(SceneConstants, FogColor) == 16);
        static_assert(offsetof(SceneConstants, FogDensity) == 28);
        static_assert(offsetof(SceneConstants, FogStartDistance) == 32);
        static_assert(offsetof(SceneConstants, FogEndDistance) == 36);
        static_assert(offsetof(SceneConstants, FogMode) == 40);

        struct MaterialConstants
        {
            Vector4 BaseColor;
            uint32_t Mode;
            float AlphaCutoff;
            uint32_t HasBaseColorTexture = 0;
            float Padding = 0.0f;
        };
        static_assert(sizeof(MaterialConstants) == 32);
        static_assert(offsetof(MaterialConstants, Mode) == 16 && offsetof(MaterialConstants, AlphaCutoff) == 20);
        static_assert(offsetof(MaterialConstants, HasBaseColorTexture) == 24);

#if CANAPE_RENDER_DEBUG
        struct alignas(16) DepthDisplayConstants
        {
            float Min;
            float Max;
            float Padding[2]{};
        };
        static_assert(sizeof(DepthDisplayConstants) == 16);
#endif

        HWND Window = nullptr;
        uint32_t Width = 0;
        uint32_t Height = 0;
        bool FrameActive = false;
        std::string LastError;
#if CANAPE_RENDER_DEBUG
        RendererDebugSettings DebugSettings{};
        RendererDebugStats CurrentStats{};
        RendererDebugStats CompletedStats{};
        uint64_t NextFrameId = 0;
        GPUProfiler Profiler;
        bool ProfilerAvailable = false;
        uint64_t MinGpuFrameId = 0;
#endif

        ViewConstants ViewData{};
        SceneConstants SceneData{};
        bool ViewDirty = true;
        bool SceneDirty = true;
        RenderScene QueuedScene;
        DirectionalShadowSettings ShadowSettings{};
        bool ShadowDistancePlusHeld = false;
        bool ShadowDistanceMinusHeld = false;
        bool PcssToggleHeld = false;
        bool PcfCountDecreaseHeld = false;
        bool PcfCountIncreaseHeld = false;
        bool Legacy25ToggleHeld = false;
        bool PcfSpacingDecreaseHeld = false;
        bool PcfSpacingIncreaseHeld = false;
        bool BlockerRadiusDecreaseHeld = false;
        bool BlockerRadiusIncreaseHeld = false;
        bool BlockerCountDecreaseHeld = false;
        bool BlockerCountIncreaseHeld = false;
        float LastSourceShadowSlopeBias = DefaultShadowSlopeBias;
        bool ShadowSlopeShortcutOverride = false;
        bool ShadowDiagnosticsDirty = true;
#if CANAPE_RENDER_DEBUG
        bool PcssRadiusViewHeld = false;
        bool PcssRadiusViewOverride = false;
#endif
        bool ShadowActive = false;
        uint32_t AttemptedShadowResolution = UINT32_MAX;

        static constexpr std::array<uint32_t, 6> PcfSampleOptions{ 8, 16, 25, 32, 64, 128 };
        static constexpr std::array<uint32_t, 6> BlockerSampleOptions{ 8, 16, 25, 32, 64, 128 };
        static constexpr std::array<float, 10> ShadowSlopeBiasOptions{ 0.0f, 0.5f, 1.0f,
            2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
        static constexpr std::array<float, 11> PcfSpacingOptions{ 0.25f, 0.5f,
            1.0f, 1.2f, 1.4f, 1.5f, 1.6f, 1.8f, 2.0f, 3.0f, 4.0f };
        static constexpr std::array<float, 7> BlockerRadiusOptions{ 0.125f, 0.25f, 0.4f, 0.5f,
            0.75f, 1.0f, 1.5f };

        template <size_t N>
        static float AdjacentOption(const std::array<float, N>& options, float current, bool increase)
        {
            const auto it = std::find(options.begin(), options.end(), current);
            if (it == options.end()) return current;
            return increase ? (it + 1 == options.end() ? options.front() : *(it + 1))
                : (it == options.begin() ? options.back() : *(it - 1));
        }

        bool SetPcssPcfSampleCount(uint32_t count)
        {
            if (std::find(PcfSampleOptions.begin(), PcfSampleOptions.end(), count) == PcfSampleOptions.end())
                return false;
            if (ShadowSettings.pcfSampleCount == count) return true;
            ShadowSettings.pcfSampleCount = count;
            CN_LOG_RENDERER("Shadow", "PCSS final PCF samples: ", count,
                count == 25 && ShadowSettings.legacyPcf25 ? " (legacy)" : " (Vogel)");
            return true;
        }

        void UpdateShadowDistanceShortcut()
        {
            const auto keyDown = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
            const bool focused = GetFocus() == Window;
            const bool ctrlDown = focused && keyDown(VK_CONTROL);
            const bool plusDown = ctrlDown && (keyDown(VK_OEM_PLUS) || keyDown(VK_ADD));
            const bool minusDown = ctrlDown && (keyDown(VK_OEM_MINUS) || keyDown(VK_SUBTRACT));
            const bool increase = plusDown && !ShadowDistancePlusHeld;
            const bool decrease = minusDown && !ShadowDistanceMinusHeld;
            ShadowDistancePlusHeld = plusDown;
            ShadowDistanceMinusHeld = minusDown;

            if (increase == decrease) return;
            constexpr float step = 5.0f;
            const float previous = ShadowSettings.shadowDistance;
            ShadowSettings.shadowDistance = increase ? previous + step : std::max(5.0f, previous - step);
            if (ShadowSettings.shadowDistance != previous)
            {
                ShadowDiagnosticsDirty = true;
                CN_LOG_RENDERER("Shadow", "Distance: ", ShadowSettings.shadowDistance);
            }
        }

        void UpdatePcssShortcut()
        {
            const bool down = GetFocus() == Window
                && (GetAsyncKeyState(VK_CONTROL) & 0x8000)
                && (GetAsyncKeyState(VK_SHIFT) & 0x8000)
                && (GetAsyncKeyState('P') & 0x8000);
            if (down && !PcssToggleHeld)
            {
                ShadowSettings.pcss = !ShadowSettings.pcss;
                CN_LOG_RENDERER("Shadow", ShadowSettings.pcss ? "PCSS" : "3x3 PCF");
            }
            PcssToggleHeld = down;
        }

        void UpdatePcfSampleShortcuts()
        {
            const auto keyDown = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
            const bool ctrl = GetFocus() == Window && keyDown(VK_CONTROL);
            const bool shift = ctrl && keyDown(VK_SHIFT);
            const bool alt = ctrl && keyDown(VK_MENU);
            const bool decrease = ctrl && !shift && !alt && keyDown(VK_NEXT); // PageDown.
            const bool increase = ctrl && !shift && !alt && keyDown(VK_PRIOR); // PageUp.
            const bool toggleLegacy = shift && !alt && keyDown(VK_PRIOR);
            if (decrease != increase)
            {
                const bool pressed = decrease ? !PcfCountDecreaseHeld : !PcfCountIncreaseHeld;
                if (pressed)
                {
                    const auto it = std::find(PcfSampleOptions.begin(), PcfSampleOptions.end(),
                        ShadowSettings.pcfSampleCount);
                    if (it != PcfSampleOptions.end())
                    {
                        const auto next = decrease ? (it == PcfSampleOptions.begin() ? it : it - 1)
                            : (it + 1 == PcfSampleOptions.end() ? it : it + 1);
                        SetPcssPcfSampleCount(*next);
                    }
                }
            }
            if (toggleLegacy && !Legacy25ToggleHeld && ShadowSettings.pcfSampleCount == 25)
            {
                ShadowSettings.legacyPcf25 = !ShadowSettings.legacyPcf25;
                CN_LOG_RENDERER("Shadow", "PCSS 25-tap pattern: ",
                    ShadowSettings.legacyPcf25 ? "legacy" : "Vogel");
            }
            PcfCountDecreaseHeld = decrease;
            PcfCountIncreaseHeld = increase;
            Legacy25ToggleHeld = toggleLegacy;
#if CANAPE_RENDER_DEBUG
            const bool radiusView = shift && !alt && keyDown(VK_NEXT);
            if (radiusView && !PcssRadiusViewHeld)
            {
                PcssRadiusViewOverride = !PcssRadiusViewOverride;
                CN_LOG_RENDERER("Shadow", "PCSS filter radius view: ",
                    PcssRadiusViewOverride ? "ON (log2 texels)" : "OFF");
            }
            PcssRadiusViewHeld = radiusView;
#endif
        }

        void UpdatePcssDiagnosticShortcuts()
        {
            const auto keyDown = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
            const bool ctrl = GetFocus() == Window && keyDown(VK_CONTROL);
            const bool shift = ctrl && keyDown(VK_SHIFT);
            const bool alt = ctrl && keyDown(VK_MENU);
            const bool spacingDown = shift && !alt && keyDown(VK_HOME);
            const bool spacingUp = shift && !alt && keyDown(VK_END);
            const bool searchDown = alt && !shift && keyDown(VK_HOME);
            const bool searchUp = alt && !shift && keyDown(VK_END);
            const bool countDown = alt && !shift && keyDown(VK_NEXT); // PageDown.
            const bool countUp = alt && !shift && keyDown(VK_PRIOR); // PageUp.
            if ((spacingDown && !PcfSpacingDecreaseHeld) || (spacingUp && !PcfSpacingIncreaseHeld))
            {
                ShadowSettings.pcfTapSpacingScale = AdjacentOption(PcfSpacingOptions,
                    ShadowSettings.pcfTapSpacingScale, spacingUp);
                CN_LOG_RENDERER("Shadow", "PCSS final PCF tap spacing: ",
                    ShadowSettings.pcfTapSpacingScale, "x (base radius unchanged)");
                ShadowDiagnosticsDirty = true;
            }
            if ((searchDown && !BlockerRadiusDecreaseHeld) || (searchUp && !BlockerRadiusIncreaseHeld))
            {
                ShadowSettings.blockerSearchRadiusWorld = AdjacentOption(BlockerRadiusOptions,
                    ShadowSettings.blockerSearchRadiusWorld, searchUp);
                CN_LOG_RENDERER("Shadow", "PCSS blocker search radius: ",
                    ShadowSettings.blockerSearchRadiusWorld, " world units");
                ShadowDiagnosticsDirty = true;
            }
            if ((countDown && !BlockerCountDecreaseHeld) || (countUp && !BlockerCountIncreaseHeld))
            {
                const auto it = std::find(BlockerSampleOptions.begin(), BlockerSampleOptions.end(),
                    ShadowSettings.blockerSampleCount);
                if (it != BlockerSampleOptions.end())
                {
                    const auto next = countDown ? (it == BlockerSampleOptions.begin() ? it : it - 1)
                        : (it + 1 == BlockerSampleOptions.end() ? it : it + 1);
                    ShadowSettings.blockerSampleCount = *next;
                    CN_LOG_RENDERER("Shadow", "PCSS blocker search samples: ", *next,
                        *next == 25 ? " (legacy disk)" : " (Vogel disk)");
                }
            }
            PcfSpacingDecreaseHeld = spacingDown;
            PcfSpacingIncreaseHeld = spacingUp;
            BlockerRadiusDecreaseHeld = searchDown;
            BlockerRadiusIncreaseHeld = searchUp;
            BlockerCountDecreaseHeld = countDown;
            BlockerCountIncreaseHeld = countUp;
        }

        ComPtr<ID3D11Device> Device;
        ComPtr<ID3D11DeviceContext> Context;
        // Reverse member destruction releases cached resources before Context and Device.
        // The cache retains neither pointer; all creation calls borrow this renderer's pair.
        DX11ResourceCache Resources;
        // Temporary skybox asset; kept separate from material Texture2D IDs.
        DX11TextureResource TestSkyboxCube;
        ComPtr<ID3D11Buffer> SkyboxVertexBuffer;
        ComPtr<ID3D11Buffer> SkyboxIndexBuffer;
        static constexpr UINT SkyboxIndexCount = 6;
        DX11PipelineState Pipelines;
        ShadowMapResources ShadowMap;
        ComPtr<ID3D11SamplerState> FallbackShadowSampler;
        ComPtr<IDXGISwapChain> SwapChain;
        ComPtr<ID3D11RenderTargetView> RenderTargetView;
        ComPtr<ID3D11Texture2D> SceneColorTexture;
        ComPtr<ID3D11RenderTargetView> SceneColorRenderTargetView;
        ComPtr<ID3D11ShaderResourceView> SceneColorShaderResourceView;
        ComPtr<ID3D11Texture2D> DepthTexture;
        ComPtr<ID3D11DepthStencilView> DepthStencilView;
#if CANAPE_RENDER_DEBUG
        ComPtr<ID3D11ShaderResourceView> DepthShaderResourceView;
#endif
        ComPtr<ID3D11Buffer> ObjectBuffer;
        ComPtr<ID3D11Buffer> MaterialBuffer;
        ComPtr<ID3D11Buffer> ViewBuffer;
        ComPtr<ID3D11Buffer> SkyboxViewBuffer;
        ComPtr<ID3D11Buffer> SceneBuffer;
        ComPtr<ID3D11Buffer> LightBuffer;
#if CANAPE_RENDER_DEBUG
        ComPtr<ID3D11Buffer> DepthDisplayBuffer;
#endif

        Impl()
        {
            DirectX::XMStoreFloat4x4(&ViewData.View, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&ViewData.Projection, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&ViewData.ViewProjection, DirectX::XMMatrixIdentity());
        }

        ~Impl()
        {
            // Drop context-held buffer/SRV references before releasing the owned GPU objects.
            if (Context) Context->ClearState();
        }

        static LightConstants BuildLightConstants(std::span<const RenderLight> lights,
            uint32_t* shadowDirectionalIndex = nullptr)
        {
            if (shadowDirectionalIndex) *shadowDirectionalIndex = InvalidShadowView;
            // Rebuild the entire buffer, including unused entries and shadow metadata.
            LightConstants data{};
            for (const RenderLight& light : lights)
            {
                if (!IsFinite(light.color) || !std::isfinite(light.intensity) || light.intensity <= 0.0f)
                    continue;
                const Vector3 colorIntensity = light.color * light.intensity;
                if (!IsFinite(colorIntensity)) continue;

                if (light.type == LightType::Directional)
                {
                    if (data.DirectionalCount == MaxDirectionalLights || !IsFinite(light.direction)) continue;
                    const float lengthSquared = light.direction.LengthSquared();
                    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-12f) continue;
                    // The first accepted Directional light owns the one shadow map.
                    // Respect its existing castShadow flag; do not silently choose another light.
                    if (shadowDirectionalIndex && data.DirectionalCount == 0 && light.castShadow)
                        *shadowDirectionalIndex = 0;
                    auto& gpu = data.DirectionalLights[data.DirectionalCount++];
                    gpu.Direction = light.direction / std::sqrt(lengthSquared);
                    gpu.ColorIntensity = colorIntensity;
                }
                else if (light.type == LightType::Point)
                {
                    if (data.LocalCount == MaxLocalLights || !IsFinite(light.position)
                        || !std::isfinite(light.range) || light.range <= 0.0f) continue;
                    auto& gpu = data.LocalLights[data.LocalCount++];
                    gpu.Position = light.position;
                    gpu.Range = light.range;
                    gpu.ColorIntensity = colorIntensity;
                    gpu.Type = static_cast<uint32_t>(LocalLightType::Point);
                }
                // Spot shading is future work. Shadow metadata is published only after a valid pass.
            }
            return data;
        }

        bool EnsureShadowResources()
        {
            if (ShadowMap.Texture && ShadowMap.Resolution == ShadowSettings.resolution) return true;
            // Retry after a resolution change or renderer reinitialization, without logging every frame.
            if (AttemptedShadowResolution == ShadowSettings.resolution) return false;
            AttemptedShadowResolution = ShadowSettings.resolution;
            auto failed = [](const char* operation, HRESULT result)
            {
                CN_LOG_RENDERER("Shadow", HResultMessage(operation, result), "; rendering without shadows");
                (void)operation;
                (void)result;
                return false;
            };
            if (ShadowSettings.resolution == 0 || ShadowSettings.resolution > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION)
                return failed("Shadow map resolution", E_INVALIDARG);

            // Publish a complete set only. A failed recreation never exposes a partial texture/view pair.
            ShadowMapResources pending;
            D3D11_TEXTURE2D_DESC texture{};
            texture.Width = texture.Height = ShadowSettings.resolution;
            texture.MipLevels = texture.ArraySize = 1;
            texture.Format = DXGI_FORMAT_R32_TYPELESS;
            texture.SampleDesc.Count = 1;
            texture.Usage = D3D11_USAGE_DEFAULT;
            texture.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
            HRESULT result = Device->CreateTexture2D(&texture, nullptr, &pending.Texture);
            if (FAILED(result)) return failed("CreateTexture2D(shadow)", result);
            D3D11_DEPTH_STENCIL_VIEW_DESC dsv{};
            dsv.Format = DXGI_FORMAT_D32_FLOAT;
            dsv.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            result = Device->CreateDepthStencilView(pending.Texture.Get(), &dsv, &pending.DepthView);
            if (FAILED(result)) return failed("CreateDepthStencilView(shadow)", result);
            D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R32_FLOAT;
            srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srv.Texture2D.MipLevels = 1;
            result = Device->CreateShaderResourceView(pending.Texture.Get(), &srv, &pending.ShaderView);
            if (FAILED(result)) return failed("CreateShaderResourceView(shadow)", result);
            D3D11_SAMPLER_DESC sampler{};
            sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            sampler.BorderColor[0] = sampler.BorderColor[1] = sampler.BorderColor[2] = sampler.BorderColor[3] = 1.0f;
            sampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
            sampler.MaxAnisotropy = 1;
            sampler.MaxLOD = D3D11_FLOAT32_MAX;
            result = Device->CreateSamplerState(&sampler, &pending.PointSampler);
            if (FAILED(result)) return failed("CreateSamplerState(shadow point comparison)", result);
            sampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
            result = Device->CreateSamplerState(&sampler, &pending.PcfSampler);
            if (FAILED(result)) return failed("CreateSamplerState(shadow PCF comparison)", result);
            D3D11_BUFFER_DESC buffer{};
            buffer.ByteWidth = sizeof(ShadowConstants);
            buffer.Usage = D3D11_USAGE_DEFAULT;
            buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            result = Device->CreateBuffer(&buffer, nullptr, &pending.Constants);
            if (FAILED(result)) return failed("CreateBuffer(shadow constants)", result);
            buffer.ByteWidth = sizeof(PcssConstants);
            result = Device->CreateBuffer(&buffer, nullptr, &pending.PcssConstants);
            if (FAILED(result)) return failed("CreateBuffer(PCSS constants)", result);
            pending.Resolution = ShadowSettings.resolution;
            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(1, 1, &unbound);
            ShadowMap = std::move(pending);
            return true;
        }

        static bool BuildFrustumBoundingSphere(const ViewConstants& camera, float shadowDistance, Vector3& center,
            float& radius, double& fittedRadius)
        {
            using namespace DirectX;
            const XMMATRIX projection = XMLoadFloat4x4(&camera.Projection);
            const XMMATRIX view = XMLoadFloat4x4(&camera.View);
            XMVECTOR projectionDeterminant, viewDeterminant;
            const XMMATRIX inverseProjection = XMMatrixInverse(&projectionDeterminant, projection);
            const XMMATRIX inverseView = XMMatrixInverse(&viewDeterminant, view);
            const float projectionDet = XMVectorGetX(projectionDeterminant);
            const float viewDet = XMVectorGetX(viewDeterminant);
            if (!std::isfinite(projectionDet) || projectionDet == 0.0f
                || !std::isfinite(viewDet) || viewDet == 0.0f
                || XMMatrixIsNaN(inverseProjection) || XMMatrixIsInfinite(inverseProjection)
                || XMMatrixIsNaN(inverseView) || XMMatrixIsInfinite(inverseView)) return false;

            const XMVECTOR nearCenter = XMVector3TransformCoord(XMVectorSet(0, 0, 0, 1), inverseProjection);
            const XMVECTOR farCenter = XMVector3TransformCoord(XMVectorSet(0, 0, 1, 1), inverseProjection);
            const float cameraNear = XMVectorGetZ(nearCenter);
            const float cameraFar = XMVectorGetZ(farCenter);
            if (!std::isfinite(shadowDistance) || !std::isfinite(cameraNear) || !std::isfinite(cameraFar)
                || cameraNear < 0.0f || cameraFar <= cameraNear) return false;
            const float shadowFar = std::min(cameraFar, shadowDistance);
            if (shadowFar <= cameraNear) return false;
            const float farFraction = (shadowFar - cameraNear) / (cameraFar - cameraNear);

            std::array<XMFLOAT3, 8> viewCorners{};
            double worldX = 0.0, worldY = 0.0, worldZ = 0.0;
            double viewX = 0.0, viewY = 0.0, viewZ = 0.0;
            size_t index = 0;
            // DirectX LH clip space has x/y in [-1,1] and z in [0,1].
            // Interpolating each near-to-far edge caps the camera frustum without
            // changing its projection or requiring a separate camera matrix.
            for (float z : { 0.0f, 1.0f })
                for (float y : { -1.0f, 1.0f })
                    for (float x : { -1.0f, 1.0f })
                    {
                        const XMVECTOR nearCorner = XMVector3TransformCoord(XMVectorSet(x, y, 0, 1), inverseProjection);
                        const XMVECTOR farCorner = XMVector3TransformCoord(XMVectorSet(x, y, 1, 1), inverseProjection);
                        const XMVECTOR viewCorner = z == 0.0f ? nearCorner
                            : XMVectorLerp(nearCorner, farCorner, farFraction);
                        const XMVECTOR worldCorner = XMVector3TransformCoord(viewCorner, inverseView);
                        XMFLOAT3 worldPoint;
                        XMStoreFloat3(&viewCorners[index++], viewCorner);
                        XMStoreFloat3(&worldPoint, worldCorner);
                        if (!std::isfinite(worldPoint.x) || !std::isfinite(worldPoint.y)
                            || !std::isfinite(worldPoint.z)) return false;
                        worldX += worldPoint.x; worldY += worldPoint.y; worldZ += worldPoint.z;
                        viewX += XMVectorGetX(viewCorner);
                        viewY += XMVectorGetY(viewCorner);
                        viewZ += XMVectorGetZ(viewCorner);
                    }
            center = { static_cast<float>(worldX / 8.0), static_cast<float>(worldY / 8.0),
                static_cast<float>(worldZ / 8.0) };
            if (!IsFinite(center)) return false;

            // Distances in camera space are rotation invariant. Computing them from
            // world corners would introduce view-inverse rounding into the radius.
            double radiusSquared = 0.0;
            for (const XMFLOAT3& corner : viewCorners)
            {
                const double dx = corner.x - viewX / 8.0;
                const double dy = corner.y - viewY / 8.0;
                const double dz = corner.z - viewZ / 8.0;
                radiusSquared = std::max(radiusSquared, dx * dx + dy * dy + dz * dz);
            }
            fittedRadius = std::sqrt(radiusSquared);
            // Fixed, projection-dependent margin covers float transforms of the
            // center and corners without allowing camera rotation to resize XY.
            radius = static_cast<float>(fittedRadius * 1.001 + 0.01);
            return std::isfinite(radius) && radius > 0.0f;
        }

        bool BuildDirectionalShadowView(const Vector3& direction,
            const DirectionalShadowSettings& settings, ShadowViewGpu& output, PcssConstants& pcssOutput)
        {
            if (!IsFinite(direction) || direction.LengthSquared() < 1e-12f
                || !std::isfinite(settings.lightDistance) || settings.lightDistance <= 0.0f
                || !std::isfinite(settings.nearPlane) || settings.nearPlane < 0.0f
                || !std::isfinite(settings.farPlane) || settings.farPlane <= settings.nearPlane
                || !std::isfinite(settings.depthBias) || settings.depthBias < 0.0f) return false;
            using namespace DirectX;
            Vector3 center;
            float radius;
            double fittedRadius;
            if (!BuildFrustumBoundingSphere(ViewData, settings.shadowDistance, center, radius, fittedRadius)
                || ShadowMap.Resolution == 0) return false;
            // A snapped center can move by half a texel on each light-space axis.
            // Expand XY only when the sphere's existing padding is insufficient.
            const double resolution = static_cast<double>(ShadowMap.Resolution);
            const double requiredRadius = ShadowMap.Resolution > 1
                ? fittedRadius / (1.0 - 1.0 / resolution) : fittedRadius;
            const float projectionRadius = requiredRadius > radius
                ? std::nextafter(static_cast<float>(requiredRadius), std::numeric_limits<float>::infinity())
                : radius;
            const float projectionWidth = 2.0f * projectionRadius;
            const float projectionHeight = 2.0f * projectionRadius;
            const float texelSizeX = projectionWidth / ShadowMap.Resolution;
            const float texelSizeY = projectionHeight / ShadowMap.Resolution;
            if (!std::isfinite(projectionWidth) || !std::isfinite(projectionHeight)
                || !std::isfinite(texelSizeX) || !std::isfinite(texelSizeY)
                || texelSizeX <= 0.0f || texelSizeY <= 0.0f) return false;
            // Snapping changes only XY; retain the existing depth placement and range.
            const float lightDistance = radius + settings.lightDistance;
            const float farPlane = 2.0f * radius + settings.farPlane;
            if (!std::isfinite(lightDistance) || !std::isfinite(farPlane)
                || farPlane <= settings.nearPlane) return false;
            const XMVECTOR forward = XMVector3Normalize(direction.ToXM());
            const XMVECTOR up = std::abs(XMVectorGetY(forward)) > 0.99f
                ? XMVectorSet(0, 0, 1, 0) : XMVectorSet(0, 1, 0, 0);
            // A light view at the world origin supplies fixed axes. A view centered
            // on the sphere would always report (0,0) and cannot define a grid.
            const XMMATRIX lightAxes = XMMatrixLookToLH(XMVectorZero(), forward, up);
            const XMVECTOR centerLS = XMVector3TransformNormal(center.ToXM(), lightAxes);
            const float originalX = XMVectorGetX(centerLS);
            const float originalY = XMVectorGetY(centerLS);
            const float snappedX = ShadowMap.Resolution > 1
                ? std::round(originalX / texelSizeX) * texelSizeX : originalX;
            const float snappedY = ShadowMap.Resolution > 1
                ? std::round(originalY / texelSizeY) * texelSizeY : originalY;
            const XMVECTOR offsetLS = XMVectorSet(snappedX - originalX, snappedY - originalY, 0.0f, 0.0f);
            const XMVECTOR snappedCenter = XMVectorAdd(center.ToXM(),
                XMVector3TransformNormal(offsetLS, XMMatrixTranspose(lightAxes)));
            if (!std::isfinite(snappedX) || !std::isfinite(snappedY)
                || !IsFinite(Vector3::FromXM(snappedCenter))) return false;
            const XMVECTOR eye = XMVectorSubtract(snappedCenter, XMVectorScale(forward, lightDistance));
            const XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
            const XMMATRIX projection = XMMatrixOrthographicOffCenterLH(-projectionRadius, projectionRadius,
                -projectionRadius, projectionRadius,
                settings.nearPlane, farPlane);
            const XMMATRIX viewProjection = view * projection;
            if (XMMatrixIsNaN(viewProjection) || XMMatrixIsInfinite(viewProjection)) return false;
            // Row-major, left-handed, z [0,1], no upload transpose, same as the camera.
            XMStoreFloat4x4(&output.LightViewProjection, viewProjection);
            output.DepthBias = settings.depthBias;
            output.MapIndex = 0;
            output.NearPlane = settings.nearPlane;
            output.FarPlane = farPlane;
            pcssOutput.OrthoSize = { projectionWidth, projectionHeight };
            return true;
        }

        void UploadLights(std::span<const RenderLight> lights, const RenderScene* scene = nullptr)
        {
            uint32_t shadowIndex = InvalidShadowView;
            LightConstants data = BuildLightConstants(lights, &shadowIndex);
            ShadowConstants shadowData{};
            shadowData.PcfEnabled = ShadowSettings.pcf ? 1u : 0u;
            PcssConstants pcssData{};
            pcssData.TanLightAngularRadius = std::tan(ShadowSettings.lightAngularRadius);
            pcssData.BlockerSearchRadiusWorld = ShadowSettings.blockerSearchRadiusWorld;
            pcssData.MinFilterRadiusWorld = ShadowSettings.minFilterRadiusWorld;
            pcssData.MaxFilterRadiusWorld = ShadowSettings.maxFilterRadiusWorld;
            pcssData.PcfSampleCount = ShadowSettings.pcfSampleCount;
            pcssData.PcfTapSpacingScale = ShadowSettings.pcfTapSpacingScale;
            pcssData.BlockerSampleCount = ShadowSettings.blockerSampleCount;
            pcssData.FilterMode = !ShadowSettings.pcf ? 0u : !ShadowSettings.pcss ? 1u
                : ShadowSettings.pcfSampleCount == 25 && ShadowSettings.legacyPcf25 ? 3u : 2u;
            ShadowActive = scene && ShadowSettings.enabled && shadowIndex != InvalidShadowView
                && Pipelines.ShadowsAvailable() && EnsureShadowResources()
                && BuildDirectionalShadowView(data.DirectionalLights[shadowIndex].Direction,
                    ShadowSettings, shadowData.Views[0], pcssData);
            if (ShadowActive)
            {
                shadowData.ViewCount = 1;
                auto& light = data.DirectionalLights[shadowIndex];
                light.ShadowViewOffset = 0;
                light.ShadowViewCount = 1;
#if defined(_DEBUG) && CANAPE_RENDER_DEBUG
                if (ShadowDiagnosticsDirty)
                {
                    const float texelsPerWorld = ShadowMap.Resolution / pcssData.OrthoSize.x;
                    CN_LOG_RENDERER("Shadow", "PCSS footprint: world/texel ",
                        1.0f / texelsPerWorld, ", search ",
                        std::max(ShadowSettings.blockerSearchRadiusWorld * texelsPerWorld, 2.0f),
                        " texels, max final tap radius ",
                        std::min(ShadowSettings.maxFilterRadiusWorld * ShadowSettings.pcfTapSpacingScale,
                            ShadowSettings.blockerSearchRadiusWorld) * texelsPerWorld,
                        " texels");
                    ShadowDiagnosticsDirty = false;
                }
#endif
            }
            // Full uploads clear old views/metadata on every frame and every scene, including fallback.
            Context->UpdateSubresource(LightBuffer.Get(), 0, nullptr, &data, 0, 0);
            if (ShadowMap.Constants)
                Context->UpdateSubresource(ShadowMap.Constants.Get(), 0, nullptr, &shadowData, 0, 0);
            if (ShadowMap.PcssConstants)
                Context->UpdateSubresource(ShadowMap.PcssConstants.Get(), 0, nullptr, &pcssData, 0, 0);
            if (ShadowActive) RenderShadowPass(*scene);
        }

        void BindMainTargets()
        {
            ID3D11RenderTargetView* target = SceneColorRenderTargetView.Get();
            Context->OMSetRenderTargets(1, &target, DepthStencilView.Get());
            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(Width);
            viewport.Height = static_cast<float>(Height);
            viewport.MaxDepth = 1.0f;
            Context->RSSetViewports(1, &viewport);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        }

        void UpdateObjectConstants(const float* world)
        {
            using namespace DirectX;
            const XMMATRIX worldMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(world));
            ObjectConstants constants{};
            XMStoreFloat4x4(&constants.World, worldMatrix);
            XMVECTOR determinant;
            const XMMATRIX inverseWorld = XMMatrixInverse(&determinant, worldMatrix);
            const float det = XMVectorGetX(determinant);
            // Normal inverse-transpose is mathematical, unrelated to row-major upload packing.
            if (std::isfinite(det) && det != 0.0f && !XMMatrixIsInfinite(inverseWorld) && !XMMatrixIsNaN(inverseWorld))
                XMStoreFloat4x4(&constants.WorldInverseTranspose, XMMatrixTranspose(inverseWorld));
            Context->UpdateSubresource(ObjectBuffer.Get(), 0, nullptr, &constants, 0, 0);
            ID3D11Buffer* buffer = ObjectBuffer.Get();
            Context->VSSetConstantBuffers(0, 1, &buffer);
        }

        MaterialDesc BindMaterial(MaterialResourceId material)
        {
            const DX11MaterialResource* resource = Resources.GetMaterial(material);
            const MaterialDesc desc = resource ? resource->Desc : MaterialDesc{};
            ID3D11ShaderResourceView* textureView = Resources.GetWhiteTextureView();
            uint32_t hasTexture = 0;
            if (resource)
            {
                if (const auto* texture = Resources.GetTexture(resource->BaseColorTexture))
                {
                    textureView = texture->ShaderResourceView.Get();
#if CANAPE_RENDER_DEBUG
                    hasTexture = 1;
#endif
                }
            }
            const MaterialConstants constants{ desc.baseColor, static_cast<uint32_t>(desc.alphaMode),
                desc.alphaCutoff, hasTexture };
            Context->UpdateSubresource(MaterialBuffer.Get(), 0, nullptr, &constants, 0, 0);
            ID3D11Buffer* buffer = MaterialBuffer.Get();
            Context->PSSetConstantBuffers(1, 1, &buffer);
            Context->PSSetShaderResources(0, 1, &textureView);
            return desc;
        }

        void DrawMeshGeometry(const DX11MeshResource& mesh, uint32_t submeshIndex)
        {
            const UINT stride = mesh.Stride;
            constexpr UINT offset = 0;
            ID3D11Buffer* vertexBuffer = mesh.VertexBuffer.Get();
            Context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            Context->IASetIndexBuffer(mesh.IndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            const SubMesh& part = mesh.SubMeshes[submeshIndex];
            Context->DrawIndexed(part.indexCount, part.startIndex, part.baseVertex);
#if CANAPE_RENDER_DEBUG
            ++CurrentStats.drawCalls;
            CurrentStats.triangleCount += part.indexCount / 3;
#endif
        }

        void RenderShadowPass(const RenderScene& scene)
        {
            // Unbind the read view before binding the same texture as writable depth.
            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(1, 1, &unbound);
            Context->OMSetRenderTargets(0, nullptr, ShadowMap.DepthView.Get());
            Context->ClearDepthStencilView(ShadowMap.DepthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
            D3D11_VIEWPORT viewport{};
            viewport.Width = viewport.Height = static_cast<float>(ShadowMap.Resolution);
            viewport.MaxDepth = 1.0f;
            Context->RSSetViewports(1, &viewport);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11Buffer* shadowBuffer = ShadowMap.Constants.Get();
            Context->VSSetConstantBuffers(5, 1, &shadowBuffer);
            for (const RenderItem& item : scene.items)
            {
                if (!item.castShadow) continue;
                const auto* mesh = Resources.GetMesh(item.mesh);
                if (!mesh || item.submeshIndex >= mesh->SubMeshes.size()) continue;
                const auto* material = Resources.GetMaterial(item.material);
                const AlphaMode alpha = material ? material->Desc.alphaMode : AlphaMode::Opaque;
                if (alpha == AlphaMode::Transparent) continue;
                BindPipeline(Pipelines.ResolveShadow(mesh->Format, alpha));
                UpdateObjectConstants(item.world.Data());
                if (alpha == AlphaMode::Masked) BindMaterial(item.material);
                DrawMeshGeometry(*mesh, item.submeshIndex);
            }
            // The camera view constants were never overwritten. Restore camera targets,
            // viewport and the default pipeline; each main draw then binds its own variant.
            BindMainTargets();
            BindPipeline(Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque));
        }

        void BindSharedConstants()
        {
            // Upload once per view/scene change, including the direct DrawMesh path.
            if (ViewDirty)
            {
                Context->UpdateSubresource(ViewBuffer.Get(), 0, nullptr, &ViewData, 0, 0);
                ViewDirty = false;
            }
            if (SceneDirty)
            {
                Context->UpdateSubresource(SceneBuffer.Get(), 0, nullptr, &SceneData, 0, 0);
                SceneDirty = false;
            }

            ID3D11Buffer* viewBuffer = ViewBuffer.Get();
            ID3D11Buffer* sceneBuffer = SceneBuffer.Get();
            ID3D11Buffer* lightBuffer = LightBuffer.Get();
            Context->VSSetConstantBuffers(2, 1, &viewBuffer);
            Context->PSSetConstantBuffers(2, 1, &viewBuffer);
            Context->PSSetConstantBuffers(3, 1, &lightBuffer);
            Context->PSSetConstantBuffers(4, 1, &sceneBuffer);
            ID3D11Buffer* shadowBuffer = ShadowMap.Constants.Get();
            Context->VSSetConstantBuffers(5, 1, &shadowBuffer);
            Context->PSSetConstantBuffers(5, 1, &shadowBuffer);
            ID3D11Buffer* pcssBuffer = ShadowMap.PcssConstants.Get();
            Context->PSSetConstantBuffers(7, 1, &pcssBuffer);
            ID3D11ShaderResourceView* shadowView = ShadowActive ? ShadowMap.ShaderView.Get() : nullptr;
            ID3D11SamplerState* shadowSampler = ShadowSettings.pcf
                ? ShadowMap.PcfSampler.Get() : ShadowMap.PointSampler.Get();
            if (!shadowSampler) shadowSampler = FallbackShadowSampler.Get();
            Context->PSSetShaderResources(1, 1, &shadowView);
            Context->PSSetSamplers(1, 1, &shadowSampler);
        }

        void BindPipeline(const DX11PipelineSelection& pipeline)
        {
            Context->IASetInputLayout(pipeline.InputLayout);
            Context->VSSetShader(pipeline.VS, nullptr, 0);
            Context->PSSetShader(pipeline.PS, nullptr, 0);
            Context->RSSetState(pipeline.Rasterizer);
            Context->OMSetBlendState(pipeline.Blend, nullptr, UINT32_MAX);
            Context->OMSetDepthStencilState(pipeline.Depth, 0);
            Context->PSSetSamplers(0, 1, &pipeline.Sampler);
        }

        void DrawSkybox()
        {
            if (!TestSkyboxCube.ShaderResourceView || !SkyboxVertexBuffer || !SkyboxIndexBuffer
                || !SkyboxViewBuffer) return;

            using namespace DirectX;
            const XMMATRIX projection = XMLoadFloat4x4(&ViewData.Projection);
            const XMMATRIX view = XMLoadFloat4x4(&ViewData.View);
            XMVECTOR projectionDeterminant;
            XMVECTOR viewDeterminant;
            const XMMATRIX inverseProjection = XMMatrixInverse(&projectionDeterminant, projection);
            const XMMATRIX inverseView = XMMatrixInverse(&viewDeterminant, view);
            const float projectionDet = XMVectorGetX(projectionDeterminant);
            const float viewDet = XMVectorGetX(viewDeterminant);
            if (!std::isfinite(projectionDet) || projectionDet == 0.0f
                || !std::isfinite(viewDet) || viewDet == 0.0f
                || XMMatrixIsNaN(inverseProjection) || XMMatrixIsInfinite(inverseProjection)
                || XMMatrixIsNaN(inverseView) || XMMatrixIsInfinite(inverseView)) return;

            SkyboxViewConstants constants{};
            // Existing shaders use row-major mul(vector, matrix); upload without a transpose.
            XMStoreFloat4x4(&constants.InverseProjection, inverseProjection);
            XMStoreFloat4x4(&constants.InverseView, inverseView);
            Context->UpdateSubresource(SkyboxViewBuffer.Get(), 0, nullptr, &constants, 0, 0);

            BindMainTargets();
            const DX11PipelineSelection pipeline = Pipelines.ResolveSkybox();
            BindPipeline(pipeline);
            const UINT stride = sizeof(SkyboxVertex);
            constexpr UINT offset = 0;
            ID3D11Buffer* vertexBuffer = SkyboxVertexBuffer.Get();
            Context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
            Context->IASetIndexBuffer(SkyboxIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            ID3D11Buffer* viewBuffer = SkyboxViewBuffer.Get();
            Context->VSSetConstantBuffers(6, 1, &viewBuffer);
            ID3D11ShaderResourceView* cubeView = TestSkyboxCube.ShaderResourceView.Get();
            Context->PSSetShaderResources(2, 1, &cubeView);
            Context->PSSetSamplers(2, 1, &pipeline.Sampler);
            Context->DrawIndexed(SkyboxIndexCount, 0, 0);
#if CANAPE_RENDER_DEBUG
            ++CurrentStats.drawCalls;
            CurrentStats.triangleCount += SkyboxIndexCount / 3;
#endif
            ID3D11ShaderResourceView* unboundView = nullptr;
            ID3D11SamplerState* unboundSampler = nullptr;
            ID3D11Buffer* unboundBuffer = nullptr;
            Context->PSSetShaderResources(2, 1, &unboundView);
            Context->PSSetSamplers(2, 1, &unboundSampler);
            Context->VSSetConstantBuffers(6, 1, &unboundBuffer);
        }

        void RenderFinalOutput()
        {
            // Rebinding the back buffer unbinds both SceneColor RTV and depth DSV before SRV reads.
            ID3D11RenderTargetView* backBuffer = RenderTargetView.Get();
            Context->OMSetRenderTargets(1, &backBuffer, nullptr);

            D3D11_VIEWPORT viewport{};
            viewport.Width = static_cast<float>(Width);
            viewport.Height = static_cast<float>(Height);
            viewport.MaxDepth = 1.0f;
            Context->RSSetViewports(1, &viewport);

#if CANAPE_RENDER_DEBUG
            const bool cameraDepth = !PcssRadiusViewOverride && DebugSettings.enabled
                && DebugSettings.viewMode == DebugViewMode::CameraDepth;
            const bool shadowDepth = !PcssRadiusViewOverride && DebugSettings.enabled
                && DebugSettings.viewMode == DebugViewMode::ShadowDepth
                && ShadowActive;
            BindPipeline(Pipelines.ResolveFinalOutput(cameraDepth || shadowDepth));
            ID3D11ShaderResourceView* output = shadowDepth ? ShadowMap.ShaderView.Get()
                : cameraDepth ? DepthShaderResourceView.Get() : SceneColorShaderResourceView.Get();
#else
            BindPipeline(Pipelines.ResolveFinalOutput(false));
            ID3D11ShaderResourceView* output = SceneColorShaderResourceView.Get();
#endif
            Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            Context->PSSetShaderResources(0, 1, &output);
#if CANAPE_RENDER_DEBUG
            if (cameraDepth || shadowDepth)
            {
                const DepthDisplayConstants display{ shadowDepth ? 0.0f : DebugSettings.depthMin,
                    shadowDepth ? 1.0f : DebugSettings.depthMax };
                Context->UpdateSubresource(DepthDisplayBuffer.Get(), 0, nullptr, &display, 0, 0);
                ID3D11Buffer* buffer = DepthDisplayBuffer.Get();
                Context->PSSetConstantBuffers(0, 1, &buffer);
            }
#endif
            Context->Draw(3, 0);
#if CANAPE_RENDER_DEBUG
            ++CurrentStats.drawCalls;
            ++CurrentStats.triangleCount;
#endif

            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(0, 1, &unbound);
            ID3D11Buffer* unboundBuffer = nullptr;
            Context->PSSetConstantBuffers(0, 1, &unboundBuffer);
        }

        bool CreateTargets(uint32_t width, uint32_t height)
        {
            ComPtr<ID3D11Texture2D> backBuffer;
            HRESULT result = SwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
            if (FAILED(result))
            {
                LastError = HResultMessage("IDXGISwapChain::GetBuffer", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            // Keep the UNORM swap-chain storage; encode linear RGB once through its sRGB view.
            // CreateTargets is also used after ResizeBuffers, so resize preserves this policy.
            D3D11_RENDER_TARGET_VIEW_DESC renderTargetDesc{};
            renderTargetDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            renderTargetDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            result = Device->CreateRenderTargetView(backBuffer.Get(), &renderTargetDesc, &RenderTargetView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateRenderTargetView(back buffer, RGBA8_UNORM_SRGB)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            // Match the old back-buffer sRGB render target so scene blending stays linear.
            D3D11_TEXTURE2D_DESC sceneColorDesc{};
            sceneColorDesc.Width = width;
            sceneColorDesc.Height = height;
            sceneColorDesc.MipLevels = 1;
            sceneColorDesc.ArraySize = 1;
            sceneColorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            sceneColorDesc.SampleDesc.Count = 1;
            sceneColorDesc.Usage = D3D11_USAGE_DEFAULT;
            sceneColorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            result = Device->CreateTexture2D(&sceneColorDesc, nullptr, &SceneColorTexture);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateTexture2D(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
            result = Device->CreateRenderTargetView(SceneColorTexture.Get(), nullptr, &SceneColorRenderTargetView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateRenderTargetView(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
            result = Device->CreateShaderResourceView(SceneColorTexture.Get(), nullptr, &SceneColorShaderResourceView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateShaderResourceView(scene color)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = width;
            depthDesc.Height = height;
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
#if CANAPE_RENDER_DEBUG
            depthDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
#else
            depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
#endif
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
#if CANAPE_RENDER_DEBUG
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
#else
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
#endif

            result = Device->CreateTexture2D(&depthDesc, nullptr, &DepthTexture);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateTexture2D(depth)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            D3D11_DEPTH_STENCIL_VIEW_DESC depthViewDesc{};
            depthViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            depthViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            result = Device->CreateDepthStencilView(DepthTexture.Get(), &depthViewDesc, &DepthStencilView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateDepthStencilView", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

#if CANAPE_RENDER_DEBUG
            D3D11_SHADER_RESOURCE_VIEW_DESC depthResourceDesc{};
            depthResourceDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            depthResourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            depthResourceDesc.Texture2D.MipLevels = 1;
            result = Device->CreateShaderResourceView(DepthTexture.Get(), &depthResourceDesc, &DepthShaderResourceView);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateShaderResourceView(depth)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
#endif

            Width = width;
            Height = height;
            return true;
        }

        bool ResizeIfNeeded(uint32_t width, uint32_t height)
        {
            if (width == Width && height == Height)
            {
                return true;
            }

            Context->OMSetRenderTargets(0, nullptr, nullptr);
            ID3D11ShaderResourceView* unbound = nullptr;
            Context->PSSetShaderResources(0, 1, &unbound);
            RenderTargetView.Reset();
            SceneColorShaderResourceView.Reset();
            SceneColorRenderTargetView.Reset();
            SceneColorTexture.Reset();
#if CANAPE_RENDER_DEBUG
            DepthShaderResourceView.Reset();
#endif
            DepthStencilView.Reset();
            DepthTexture.Reset();

            const HRESULT result = SwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(result))
            {
                LastError = HResultMessage("IDXGISwapChain::ResizeBuffers", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            return CreateTargets(width, height);
        }

        bool CreateSkyboxQuad()
        {
            constexpr std::array<SkyboxVertex, 4> vertices{{
                {{ -1.0f,  1.0f }},
                {{  1.0f,  1.0f }},
                {{  1.0f, -1.0f }},
                {{ -1.0f, -1.0f }}
            }};
            constexpr std::array<uint32_t, SkyboxIndexCount> indices{ 0, 1, 2, 0, 2, 3 };

            D3D11_BUFFER_DESC vertexDesc{};
            vertexDesc.ByteWidth = sizeof(vertices);
            vertexDesc.Usage = D3D11_USAGE_IMMUTABLE;
            vertexDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vertexData{ vertices.data() };
            ComPtr<ID3D11Buffer> vertexBuffer;
            HRESULT result = Device->CreateBuffer(&vertexDesc, &vertexData, &vertexBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(skybox vertices)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            D3D11_BUFFER_DESC indexDesc{};
            indexDesc.ByteWidth = sizeof(indices);
            indexDesc.Usage = D3D11_USAGE_IMMUTABLE;
            indexDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
            D3D11_SUBRESOURCE_DATA indexData{ indices.data() };
            ComPtr<ID3D11Buffer> indexBuffer;
            result = Device->CreateBuffer(&indexDesc, &indexData, &indexBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(skybox indices)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            SkyboxVertexBuffer = std::move(vertexBuffer);
            SkyboxIndexBuffer = std::move(indexBuffer);
            return true;
        }

        bool CreatePipeline()
        {
            if (!Pipelines.Initialize(Device.Get(), LastError)) return false;

            D3D11_BUFFER_DESC constantBufferDesc{};
            constantBufferDesc.ByteWidth = sizeof(ObjectConstants);
            constantBufferDesc.Usage = D3D11_USAGE_DEFAULT;
            constantBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            HRESULT result = Device->CreateBuffer(&constantBufferDesc, nullptr, &ObjectBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            static_assert(sizeof(Vector4) == 16);
            constantBufferDesc.ByteWidth = sizeof(MaterialConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &MaterialBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(material constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(ViewConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &ViewBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(view constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(SkyboxViewConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &SkyboxViewBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(skybox view constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(LightConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &LightBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(light constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

            constantBufferDesc.ByteWidth = sizeof(SceneConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &SceneBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(scene constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }

#if CANAPE_RENDER_DEBUG
            constantBufferDesc.ByteWidth = sizeof(DepthDisplayConstants);
            result = Device->CreateBuffer(&constantBufferDesc, nullptr, &DepthDisplayBuffer);
            if (FAILED(result))
            {
                LastError = HResultMessage("ID3D11Device::CreateBuffer(depth display constants)", result);
                CN_LOG_RENDERER("Error", LastError);
                return false;
            }
#endif

            // The PS declares a comparison sampler even when shadow branches are disabled.
            // Keep that slot valid before the first map allocation and on resource fallback.
            D3D11_SAMPLER_DESC shadowSampler{};
            shadowSampler.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
            shadowSampler.AddressU = shadowSampler.AddressV = shadowSampler.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
            shadowSampler.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
            shadowSampler.MaxAnisotropy = 1;
            shadowSampler.MaxLOD = D3D11_FLOAT32_MAX;
            result = Device->CreateSamplerState(&shadowSampler, &FallbackShadowSampler);
            if (FAILED(result)) CN_LOG_RENDERER("Shadow", HResultMessage("CreateSamplerState(shadow fallback)", result));

            return Resources.CreateWhiteTexture(Device.Get(), Context.Get(), LastError);
        }
    };

    DX11Renderer::DX11Renderer()
        : m_Impl(std::make_unique<Impl>())
    {

    }

    DX11Renderer::~DX11Renderer() = default;

    bool DX11Renderer::Initialize(void* nativeWindow, uint32_t width, uint32_t height)
    {
        CN_LOG_RENDERER("Init", "렌더러 초기화 완료");
        Shutdown();
        m_Impl = std::make_unique<Impl>();
        m_Impl->Window = static_cast<HWND>(nativeWindow);

        if (!m_Impl->Window || !IsWindow(m_Impl->Window))
        {
            m_Impl->LastError = "Initialize received an invalid native window";
            CN_LOG_RENDERER("Error", m_Impl->LastError);
            return false;
        }

        DXGI_SWAP_CHAIN_DESC swapChainDesc{};
        swapChainDesc.BufferDesc.Width = width;
        swapChainDesc.BufferDesc.Height = height;
        swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swapChainDesc.SampleDesc.Count = 1;
        swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapChainDesc.BufferCount = 2;
        swapChainDesc.OutputWindow = m_Impl->Window;
        swapChainDesc.Windowed = TRUE;
        swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        constexpr D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL selectedFeatureLevel{};

        HRESULT result = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
            &swapChainDesc, &m_Impl->SwapChain, &m_Impl->Device,
            &selectedFeatureLevel, &m_Impl->Context);

        if (FAILED(result))
        {
            result = D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                &swapChainDesc, &m_Impl->SwapChain, &m_Impl->Device,
                &selectedFeatureLevel, &m_Impl->Context);
        }

        if (FAILED(result))
        {
            m_Impl->LastError = HResultMessage("D3D11CreateDeviceAndSwapChain", result);
            CN_LOG_RENDERER("Error", m_Impl->LastError);
            return false;
        }

        if (!m_Impl->CreateTargets(width, height) || !m_Impl->CreatePipeline()
            || !m_Impl->CreateSkyboxQuad()) return false;
        std::vector<wchar_t> executablePath(32768);
        const DWORD pathLength = GetModuleFileNameW(nullptr, executablePath.data(),
            static_cast<DWORD>(executablePath.size()));
        std::string skyboxError;
        if (!pathLength || pathLength >= executablePath.size())
        {
            skyboxError = "GetModuleFileNameW failed while resolving test skybox DDS";
        }
        else
        {
            const auto path = std::filesystem::path(executablePath.data()).parent_path() / TestSkyboxDds;
            LoadDDSCubeMap(m_Impl->Device.Get(), path, m_Impl->TestSkyboxCube, skyboxError);
        }
        if (!skyboxError.empty())
        {
            // Skybox is optional until its render pass exists; keep existing rendering available.
            CN_LOG_RENDERER("Skybox", skyboxError);
            OutputDebugStringA(("[DX11Renderer][Skybox] " + skyboxError + "\r\n").c_str());
        }
        else
        {
            CN_LOG_RENDERER("Skybox", "DDS TextureCube SRV loaded");
            OutputDebugStringA("[DX11Renderer][Skybox] DDS TextureCube SRV loaded\r\n");
        }
#if CANAPE_RENDER_DEBUG
        std::string profilerError;
        m_Impl->ProfilerAvailable = m_Impl->Profiler.Initialize(m_Impl->Device.Get(), profilerError);
        if (!m_Impl->ProfilerAvailable) CN_LOG_RENDERER("Profiler", profilerError);
#endif
        return true;
    }

    void DX11Renderer::Shutdown()
    {
        m_Impl.reset();
    }

    MeshResourceId DX11Renderer::CreateMesh(
        std::span<const Vertex> vertices,
        std::span<const uint32_t> indices)
    {
        return CreateMeshData(vertices.data(), vertices.size(), sizeof(Vertex), VertexFormat::Position, indices, {});
    }

    MeshResourceId DX11Renderer::CreateMesh(std::span<const StaticVertex> vertices,
        std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        return CreateMeshData(vertices.data(), vertices.size(), sizeof(StaticVertex), VertexFormat::Static, indices, submeshes);
    }

    MeshResourceId DX11Renderer::CreateMeshData(const void* vertices, size_t vertexCount,
        uint32_t stride, VertexFormat format, std::span<const uint32_t> indices, std::span<const SubMesh> submeshes)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.CreateMesh(m_Impl->Device.Get(), vertices, vertexCount, stride, format,
            indices, submeshes, m_Impl->LastError);
    }

    void DX11Renderer::DestroyMesh(MeshResourceId mesh)
    {
        if (m_Impl) m_Impl->Resources.DestroyMesh(mesh);
    }

    bool DX11Renderer::IsMeshValid(MeshResourceId mesh) const
    {
        return m_Impl && m_Impl->Resources.IsMeshValid(mesh);
    }

    void DX11Renderer::SetCamera(const float* view, const float* projection)
    {
        if (!m_Impl)
        {
            return;
        }

        using namespace DirectX;

        const XMMATRIX viewMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(view));
        const XMMATRIX projectionMatrix = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(projection));
        XMStoreFloat4x4(&m_Impl->ViewData.View, viewMatrix);
        XMStoreFloat4x4(&m_Impl->ViewData.Projection, projectionMatrix);
        XMStoreFloat4x4(&m_Impl->ViewData.ViewProjection, viewMatrix * projectionMatrix);

        // Preserve the legacy SetCamera + DrawMesh path, which has no position argument.
        m_Impl->ViewData.CameraPosition = {};
        XMVECTOR determinant;
        const XMMATRIX inverseView = XMMatrixInverse(&determinant, viewMatrix);
        const float det = XMVectorGetX(determinant);
        if (std::isfinite(det) && det != 0.0f && !XMMatrixIsInfinite(inverseView) && !XMMatrixIsNaN(inverseView))
            m_Impl->ViewData.CameraPosition = Vector3::FromXM(inverseView.r[3]);
        m_Impl->ViewDirty = true;
    }

    void DX11Renderer::SetDebugSettings(const RendererDebugSettings& settings)
    {
#if CANAPE_RENDER_DEBUG
        if (!m_Impl) return;

        RendererDebugSettings valid = settings;
        if (valid.viewMode != DebugViewMode::Final && valid.viewMode != DebugViewMode::CameraDepth
            && valid.viewMode != DebugViewMode::WorldNormal && valid.viewMode != DebugViewMode::UV
            && valid.viewMode != DebugViewMode::MipLOD && valid.viewMode != DebugViewMode::ShadowDepth
            && valid.viewMode != DebugViewMode::ShadowVisibility
            && valid.viewMode != DebugViewMode::PcssFilterRadius)
            valid.viewMode = DebugViewMode::Final;
        const RendererDebugSettings defaults;
        if (!std::isfinite(valid.depthMin) || !std::isfinite(valid.depthMax)
            || valid.depthMin < 0.0f || valid.depthMax > 1.0f || valid.depthMin >= valid.depthMax)
        {
            valid.depthMin = defaults.depthMin;
            valid.depthMax = defaults.depthMax;
        }
        if (valid.shadowCullMode != ShadowCullMode::None && valid.shadowCullMode != ShadowCullMode::Front
            && valid.shadowCullMode != ShadowCullMode::Back)
            valid.shadowCullMode = defaults.shadowCullMode;
        if (!std::isfinite(valid.shadowSlopeBias) || valid.shadowSlopeBias < 0.0f)
            valid.shadowSlopeBias = defaults.shadowSlopeBias;
        if (!std::isfinite(valid.shadowReceiverBias) || valid.shadowReceiverBias < 0.0f)
            valid.shadowReceiverBias = defaults.shadowReceiverBias;
        const float sourceSlope = valid.shadowSlopeBias;
        const auto keyDown = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
        const bool focused = m_Impl->Window && GetFocus() == m_Impl->Window;
        const bool sourceCtrlF2 = focused && keyDown(VK_CONTROL) && keyDown(VK_F2);
        const bool sourceReset = focused && keyDown(VK_F10);
        bool slopeShortcutAdvanced = false;
        if (sourceReset)
            m_Impl->ShadowSlopeShortcutOverride = false;
        else if (sourceCtrlF2 && sourceSlope != m_Impl->LastSourceShadowSlopeBias)
        {
            valid.shadowSlopeBias = Impl::AdjacentOption(Impl::ShadowSlopeBiasOptions,
                m_Impl->DebugSettings.shadowSlopeBias, true);
            m_Impl->ShadowSlopeShortcutOverride = true;
            slopeShortcutAdvanced = true;
        }
        else if (m_Impl->ShadowSlopeShortcutOverride
            && sourceSlope == m_Impl->LastSourceShadowSlopeBias)
            valid.shadowSlopeBias = m_Impl->DebugSettings.shadowSlopeBias;
        else if (sourceSlope != m_Impl->LastSourceShadowSlopeBias)
            m_Impl->ShadowSlopeShortcutOverride = false;
        m_Impl->LastSourceShadowSlopeBias = sourceSlope;
        if (m_Impl->Device && m_Impl->Pipelines.ShadowsAvailable()
            && (valid.shadowCullMode != m_Impl->DebugSettings.shadowCullMode
                || valid.shadowSlopeBias != m_Impl->DebugSettings.shadowSlopeBias))
        {
            std::string error;
            if (!m_Impl->Pipelines.SetShadowRasterizer(m_Impl->Device.Get(),
                valid.shadowCullMode, valid.shadowSlopeBias, error))
            {
                CN_LOG_RENDERER("Shadow", error);
                valid.shadowCullMode = m_Impl->DebugSettings.shadowCullMode;
                valid.shadowSlopeBias = m_Impl->DebugSettings.shadowSlopeBias;
            }
        }
        m_Impl->ShadowSettings.depthBias = valid.shadowReceiverBias;
        m_Impl->ShadowSettings.pcf = valid.shadowPcf;
        if (valid.captureGpuTimings && !m_Impl->DebugSettings.captureGpuTimings)
            m_Impl->MinGpuFrameId = m_Impl->NextFrameId + 1;
        m_Impl->DebugSettings = valid;
        if (slopeShortcutAdvanced)
            CN_LOG_RENDERER("Shadow", "Active slope-scaled rasterizer bias: ",
                valid.shadowSlopeBias, " (Ctrl+F2; engine title retains its 0..2 value)");
#else
        (void)settings;
#endif
    }

    RendererDebugStats DX11Renderer::GetDebugStats() const
    {
        if (!m_Impl) return {};
        RendererDebugStats stats{};
#if CANAPE_RENDER_DEBUG
        stats = m_Impl->CompletedStats;
        if (m_Impl->DebugSettings.captureGpuTimings && m_Impl->ProfilerAvailable)
        {
            const GpuTimingResult& timing = m_Impl->Profiler.LatestResult();
            if (timing.frameId >= m_Impl->MinGpuFrameId)
            {
                stats.gpuFrameId = timing.frameId;
                stats.gpuTimingValid = timing.valid;
                stats.gpuFrameMs = timing.frameMs;
                stats.drawSceneMs = timing.drawSceneMs;
                stats.finalOutputMs = timing.finalOutputMs;
            }
        }
#endif
        return stats;
    }

    bool DX11Renderer::SetPcssPcfSampleCount(uint32_t count)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->SetPcssPcfSampleCount(count);
    }

    uint32_t DX11Renderer::GetPcssPcfSampleCount() const
    {
        return m_Impl ? m_Impl->ShadowSettings.pcfSampleCount : 64u;
    }

    void DX11Renderer::SetRenderView(const RenderView& view)
    {
        if (m_Impl) m_Impl->QueuedScene.camera = view;
    }

    void DX11Renderer::AddRenderItem(const RenderItem& item)
    {
        if (m_Impl) m_Impl->QueuedScene.items.push_back(item);
    }

    void DX11Renderer::AddLight(const RenderLight& light)
    {
        if (m_Impl) m_Impl->QueuedScene.lights.push_back(light);
    }

    void DX11Renderer::SetFog(const RenderFog& fog)
    {
        if (m_Impl) m_Impl->QueuedScene.fog = fog;
    }

    void DX11Renderer::SetSkybox(const RenderSkybox& skybox)
    {
        if (m_Impl) m_Impl->QueuedScene.skybox = skybox;
    }

    void DX11Renderer::SetAmbient(const Vector3& color, float intensity)
    {
        if (!m_Impl) return;
        m_Impl->QueuedScene.ambientColor = color;
        m_Impl->QueuedScene.ambientIntensity = intensity;
    }

    MaterialResourceId DX11Renderer::CreateMaterial(const MaterialDesc& desc, TextureResourceId baseColorTexture)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        if (!m_Impl->Device)
        {
            m_Impl->LastError = "CreateMaterial requires an initialized renderer";
            CN_LOG_RENDERER("Error", m_Impl->LastError);
            return {};
        }
        return m_Impl->Resources.CreateMaterial(desc, baseColorTexture, m_Impl->LastError);
    }

    MaterialDesc DX11Renderer::GetMaterial(MaterialResourceId material) const
    {
        if (m_Impl)
        {
            if (const auto* resource = m_Impl->Resources.GetMaterial(material)) return resource->Desc;
        }
        return {}; // Default white material, including nonexistent or stale IDs.
    }

    void DX11Renderer::DestroyMaterial(MaterialResourceId material)
    {
        if (m_Impl) m_Impl->Resources.DestroyMaterial(material);
    }

    bool DX11Renderer::IsMaterialValid(MaterialResourceId material) const
    {
        return m_Impl && m_Impl->Resources.IsMaterialValid(material);
    }

    TextureResourceId DX11Renderer::CreateTexture(uint32_t width, uint32_t height, TextureFormat format, std::span<const uint8_t> pixels)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.CreateTexture(m_Impl->Device.Get(), m_Impl->Context.Get(),
            width, height, format, pixels, m_Impl->LastError);
    }

    bool DX11Renderer::IsTextureValid(TextureResourceId texture) const
    {
        return m_Impl && m_Impl->Resources.IsTextureValid(texture);
    }

    void DX11Renderer::DestroyTexture(TextureResourceId texture)
    {
        if (!IsTextureValid(texture)) return;
        ID3D11ShaderResourceView* unbound = nullptr;
        m_Impl->Context->PSSetShaderResources(0, 1, &unbound);
        m_Impl->Resources.DestroyTexture(texture);
    }

    CubemapResourceId DX11Renderer::CreateCubemap(uint32_t faceSize, uint32_t mipLevels, TextureFormat format, std::span<const uint8_t> pixels)
    {
        if (!m_Impl) m_Impl = std::make_unique<Impl>();
        return m_Impl->Resources.CreateCubemap(m_Impl->Device.Get(), m_Impl->Context.Get(),
            faceSize, mipLevels, format, pixels, m_Impl->LastError);
    }

    bool DX11Renderer::IsCubemapValid(CubemapResourceId cubemap) const
    {
        return m_Impl && m_Impl->Resources.IsCubemapValid(cubemap);
    }

    void DX11Renderer::DestroyCubemap(CubemapResourceId cubemap)
    {
        if (m_Impl) m_Impl->Resources.DestroyCubemap(cubemap);
    }

    bool DX11Renderer::BeginFrame()
    {
        if (!m_Impl || !m_Impl->Context || !m_Impl->SwapChain || m_Impl->FrameActive)
        {
            return false;
        }

        RECT clientRect{};
        if (!GetClientRect(m_Impl->Window, &clientRect))
        {
            return false;
        }

        const uint32_t width = static_cast<uint32_t>(clientRect.right - clientRect.left);
        const uint32_t height = static_cast<uint32_t>(clientRect.bottom - clientRect.top);
        if (width == 0 || height == 0 || !m_Impl->ResizeIfNeeded(width, height))
        {
            return false;
        }

        m_Impl->UpdateShadowDistanceShortcut();
        m_Impl->UpdatePcssShortcut();
        m_Impl->UpdatePcfSampleShortcuts();
        m_Impl->UpdatePcssDiagnosticShortcuts();

        ID3D11DeviceContext* context = m_Impl->Context.Get();

#if CANAPE_RENDER_DEBUG
        m_Impl->CurrentStats = {};
        m_Impl->CurrentStats.frameId = ++m_Impl->NextFrameId;
        if (m_Impl->ProfilerAvailable)
        {
            m_Impl->Profiler.Poll(context);
            if (m_Impl->DebugSettings.captureGpuTimings)
                m_Impl->Profiler.BeginFrame(context, m_Impl->CurrentStats.frameId);
        }
#endif

        // Linear RGB and linear alpha. The SceneColor sRGB RTV encodes RGB, including clear values.
        const float clearColor[] = { 0.025f, 0.035f, 0.055f, 1.0f };
        context->ClearRenderTargetView(m_Impl->SceneColorRenderTargetView.Get(), clearColor);
        context->ClearDepthStencilView(m_Impl->DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

        m_Impl->BindMainTargets();

        // Direct draws start with scene defaults instead of inheriting the previous frame.
        // Always upload a freshly initialized LightConstants, even with no scene draws.
        m_Impl->UploadLights(SelectLights(m_Impl->QueuedScene));
        m_Impl->SceneData = {};
        m_Impl->SceneDirty = true;
        // Reset the complete default pipeline, including frames ending in transparency.
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(VertexFormat::Position, AlphaMode::Opaque));

        m_Impl->FrameActive = true;
        return true;
    }

    void DX11Renderer::DrawScene()
    {
        if (!m_Impl)
        {
            return;
        }

        DrawScene(m_Impl->QueuedScene);
        m_Impl->QueuedScene = {};
    }

    void DX11Renderer::DrawScene(const RenderScene& scene)
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

#if CANAPE_RENDER_DEBUG
        if (m_Impl->ProfilerAvailable) m_Impl->Profiler.BeginDrawScene(m_Impl->Context.Get());
#endif

        SetCamera(scene.camera.view.Data(), scene.camera.projection.Data());
        m_Impl->ViewData.CameraPosition = scene.camera.cameraPosition;
        m_Impl->SceneData.AmbientColor = scene.ambientColor;
        m_Impl->SceneData.AmbientIntensity = scene.ambientIntensity;
        m_Impl->SceneData.FogColor = scene.fog.color;
        m_Impl->SceneData.FogDensity = scene.fog.density;
        m_Impl->SceneData.FogStartDistance = scene.fog.startDistance;
        m_Impl->SceneData.FogEndDistance = scene.fog.endDistance;
        m_Impl->SceneData.FogMode = static_cast<uint32_t>(scene.fog.mode);
        // Upload even for an empty scene or a second scene within the same frame.
        m_Impl->UploadLights(SelectLights(scene), &scene);
        m_Impl->SceneDirty = true;
        m_Impl->BindSharedConstants();
        std::vector<const RenderItem*> opaque, masked;
        struct TransparentDraw { const RenderItem* Item; float Depth; };
        std::vector<TransparentDraw> transparent;
        for (const RenderItem& item : scene.items)
        {
            if (!IsMeshValid(item.mesh)) continue;
            switch (GetMaterial(item.material).alphaMode)
            {
            case AlphaMode::Masked: masked.push_back(&item); break;
            case AlphaMode::Transparent:
            {
                // Approximate camera-space depth from the world origin; no bounds/triangle sorting.
                float depth = scene.camera.view.TransformPoint(item.world.GetTranslation()).Z;
                if (!std::isfinite(depth)) depth = 0.0f; // Keep the comparator ordered for malformed input.
                transparent.push_back({ &item, depth });
                break;
            }
            default: opaque.push_back(&item); break;
            }
        }
        std::stable_sort(transparent.begin(), transparent.end(), [](const auto& a, const auto& b) { return a.Depth > b.Depth; });
        auto draw = [&](const RenderItem* item) { DrawMesh(item->mesh, item->world.Data(), item->submeshIndex, item->material); };
        for (const auto* item : opaque) draw(item);
        for (const auto* item : masked) draw(item);
        m_Impl->DrawSkybox();
        for (const auto& entry : transparent) draw(entry.Item);
#if CANAPE_RENDER_DEBUG
        if (m_Impl->ProfilerAvailable) m_Impl->Profiler.EndDrawScene(m_Impl->Context.Get());
#endif
    }

    void DX11Renderer::DrawMesh(MeshResourceId mesh, const float* world, uint32_t submeshIndex, MaterialResourceId material)
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

        const auto* resource = m_Impl->Resources.GetMesh(mesh);
        if (!resource)
        {
            return;
        }
        const DX11MeshResource& gpuMesh = *resource;
        if (submeshIndex >= gpuMesh.SubMeshes.size())
        {
            m_Impl->LastError = "DrawMesh received an invalid submeshIndex";
            CN_LOG_RENDERER("Draw", "잘못된 서브메시 인덱스: ", submeshIndex);
            return;
        }

        m_Impl->UpdateObjectConstants(world);
        const MaterialDesc materialDesc = m_Impl->BindMaterial(material);
        // Bind every draw, including direct DrawMesh calls, so prior pipeline state cannot leak.
#if CANAPE_RENDER_DEBUG
        const DebugViewMode meshView = m_Impl->PcssRadiusViewOverride
            ? DebugViewMode::PcssFilterRadius
            : m_Impl->DebugSettings.enabled ? m_Impl->DebugSettings.viewMode : DebugViewMode::Final;
        const bool wireframe = m_Impl->DebugSettings.enabled && m_Impl->DebugSettings.wireframe;
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(gpuMesh.Format, materialDesc.alphaMode, meshView,
            wireframe));
#else
        m_Impl->BindPipeline(m_Impl->Pipelines.Resolve(gpuMesh.Format, materialDesc.alphaMode));
#endif
        m_Impl->BindSharedConstants();
        m_Impl->DrawMeshGeometry(gpuMesh, submeshIndex);
    }

    void DX11Renderer::EndFrame()
    {
        if (!m_Impl || !m_Impl->FrameActive)
        {
            return;
        }

#if CANAPE_RENDER_DEBUG
        if (m_Impl->ProfilerAvailable) m_Impl->Profiler.BeginFinalOutput(m_Impl->Context.Get());
#endif
        m_Impl->RenderFinalOutput();
#if CANAPE_RENDER_DEBUG
        if (m_Impl->ProfilerAvailable)
        {
            m_Impl->Profiler.EndFinalOutput(m_Impl->Context.Get());
            m_Impl->Profiler.EndFrame(m_Impl->Context.Get());
        }
#endif
        m_Impl->FrameActive = false;
#if CANAPE_RENDER_DEBUG
        m_Impl->CompletedStats = m_Impl->CurrentStats;
#endif

        const HRESULT result = m_Impl->SwapChain->Present(1, 0);
        if (FAILED(result))
        {
            m_Impl->LastError = HResultMessage("IDXGISwapChain::Present", result);
            CN_LOG_RENDERER("Error", m_Impl->LastError);
        }
    }

    uint32_t DX11Renderer::GetWidth() const
    {
        return m_Impl ? m_Impl->Width : 0;
    }

    uint32_t DX11Renderer::GetHeight() const
    {
        return m_Impl ? m_Impl->Height : 0;
    }

    const std::string& DX11Renderer::GetLastError() const
    {
        static const std::string emptyError;
        return m_Impl ? m_Impl->LastError : emptyError;
    }
}
