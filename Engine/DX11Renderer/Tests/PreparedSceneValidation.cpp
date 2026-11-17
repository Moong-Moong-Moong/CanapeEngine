// Standalone tests linked against the real renderer. Not a runtime project item.
#define NOMINMAX
#include "DX11Renderer/Frame/DX11ScenePreparation.h"
#include "DX11Renderer/Pipeline/DX11MeshDraw.h"
#include "CNDebug/DebugLog.h"
#include <d3d11sdklayers.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace Canape::Debug
{
    void Write(MainTag, std::string_view, std::string, const char*, int) {}
}

using namespace Canape;
using Microsoft::WRL::ComPtr;

void Require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

struct Fixture
{
    ComPtr<ID3D11Device> Device;
    ComPtr<ID3D11DeviceContext> Context;
    DX11ResourceCache Resources;
    MeshResourceId Position, Static, Skinned;
    MaterialResourceId Opaque, Masked, Transparent;
    std::string Error;

    Fixture()
    {
        Require(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
            D3D11_CREATE_DEVICE_DEBUG, nullptr, 0, D3D11_SDK_VERSION,
            &Device, nullptr, &Context)), "WARP debug device");
        const std::array<Vector3, 3> positions{ Vector3{0, 0, 0}, Vector3{1, 0, 0}, Vector3{0, 1, 0} };
        std::array<StaticVertex, 3> staticVertices{};
        std::array<SkinnedVertex, 3> skinnedVertices{};
        for (size_t i = 0; i < positions.size(); ++i)
        {
            staticVertices[i].Position = positions[i];
            skinnedVertices[i].Position = positions[i];
            skinnedVertices[i].Weights[0] = 1.0f;
        }
        const std::array<uint32_t, 3> indices{0, 1, 2};
        const std::array<SubMesh, 2> submeshes{ SubMesh{0, 3, 0, 0}, SubMesh{0, 3, 0, 1} };
        Position = Resources.CreateMesh(Device.Get(), positions.data(), positions.size(),
            sizeof(Vector3), VertexFormat::Position, indices, {}, Error);
        Static = Resources.CreateMesh(Device.Get(), staticVertices.data(), staticVertices.size(),
            sizeof(StaticVertex), VertexFormat::Static, indices, submeshes, Error);
        Skinned = Resources.CreateMesh(Device.Get(), skinnedVertices.data(), skinnedVertices.size(),
            sizeof(SkinnedVertex), VertexFormat::Skinned, indices, submeshes, Error);
        Require(Position.IsValid() && Static.IsValid() && Skinned.IsValid(), "fixture meshes");
        auto material = [&](AlphaMode alpha)
        {
            MaterialDesc desc;
            desc.alphaMode = alpha;
            return Resources.CreateMaterial(desc, {}, {}, {}, {}, {}, Error);
        };
        Opaque = material(AlphaMode::Opaque);
        Masked = material(AlphaMode::Masked);
        Transparent = material(AlphaMode::Transparent);
        Require(Opaque.IsValid() && Masked.IsValid() && Transparent.IsValid(), "fixture materials");
    }

    SkinnedMeshItem Skin(MaterialResourceId material, float depth, uint32_t offset = 0,
        uint32_t count = 2, uint32_t submesh = 0) const
    {
        return { Skinned, submesh, material, Matrix4::Translation({0, 0, depth}), offset, count };
    }
};

void QueueAndOwnershipChecks(Fixture& fixture)
{
    RenderScene scene;
    scene.bonePalettes = { Matrix4::Translation({1, 2, 3}), Matrix4::Identity,
        Matrix4::Translation({4, 5, 6}), Matrix4::Identity };
    scene.items = {
        {fixture.Position, 0, fixture.Opaque, Matrix4::Translation({0, 0, 2}), true},
        {fixture.Static, 1, fixture.Masked, Matrix4::Translation({0, 0, 3}), true},
        {fixture.Static, 0, fixture.Transparent, Matrix4::Translation({0, 0, 10}), true},
        {fixture.Static, 0, fixture.Transparent, Matrix4::Translation({0, 0, 6}), true},
    };
    scene.skinnedItems = { fixture.Skin(fixture.Opaque, 4), fixture.Skin(fixture.Masked, 5, 0, 2, 1),
        fixture.Skin(fixture.Transparent, 8, 2), fixture.Skin(fixture.Transparent, 6, 2, 2, 1) };
    auto prepared = ScenePreparation::PrepareScene(scene, fixture.Resources, fixture.Error);
    Require(fixture.Error.empty(), "valid scene has no preparation error");
    Require(prepared.Draws.size() == 8, "both vertex formats prepared");
    Require(prepared.Opaque == std::vector<size_t>{0, 4}, "shared opaque queue");
    Require(prepared.Masked == std::vector<size_t>{1, 5}, "shared masked queue");
    Require(prepared.Transparent == std::vector<size_t>{2, 6, 3, 7}, "global stable transparent sorting");
    Require(prepared.ShadowCasters == std::vector<size_t>{0, 1}, "only eligible static/position shadow casters");
    for (size_t i = 0; i < 4; ++i)
        Require(prepared.Draws[i].PaletteOffset == 0 && prepared.Draws[i].PaletteCount == 0,
            "non-skinned draws have no palette");
    for (size_t i = 4; i < 8; ++i)
    {
        const auto& item = scene.skinnedItems[i - 4];
        const auto& draw = prepared.Draws[i];
        Require(draw.Mesh == fixture.Resources.GetMesh(fixture.Skinned), "shared mesh resource");
        Require(draw.PaletteOffset == item.paletteOffset && draw.PaletteCount == item.paletteCount,
            "instance palette range preserved");
        Require(draw.SubmeshIndex == item.submeshIndex, "submesh preserved");
        Require(std::memcmp(&draw.Object.World, item.world.Data(), sizeof(draw.Object.World)) == 0,
            "per-instance World preserved");
    }
    Require(prepared.Draws[4].PaletteOffset == prepared.Draws[5].PaletteOffset
        && prepared.Draws[4].PaletteOffset != prepared.Draws[6].PaletteOffset,
        "submeshes share palettes; different instances retain different palettes");
    scene.bonePalettes[0] = Matrix4::Identity;
    scene = {};
    Require(prepared.BonePalettes.size() == 4
        && prepared.BonePalettes[0].GetTranslation().X == 1.0f
        && prepared.BonePalettes[2].GetTranslation().X == 4.0f,
        "prepared palette snapshot survives source edits and reset");
    std::cout << "PASS: shared alpha queues, global stable sorting, submeshes/instances, palette ownership\n";

    // Uninitialized pipelines and null object/material buffers intentionally ensure
    // the shadow skinning guard runs before any uploads, binding, stats or draws.
    DX11PipelineState pipelines;
    MeshDrawContext context{ *fixture.Context.Get(), pipelines, nullptr, nullptr, {} };
    RendererDebugStats stats{};
    stats.drawCalls = 7;
    stats.triangleCount = 11;
    RendererDebugSettings settings{};
    context.Stats = &stats;
    context.DebugSettings = &settings;
    for (const auto& draw : prepared.Draws)
    {
        if (draw.Mesh->Format != VertexFormat::Skinned) continue;
        MeshDraw::SubmitShadow(draw, context);
    }
    Require(stats.drawCalls == 7 && stats.triangleCount == 11, "Skinned shadow submissions do no work");
    std::cout << "PASS: Skinned shadow submission guard precedes GPU access\n";
}

void RangeAndFailureChecks(Fixture& fixture)
{
    RenderScene scene;
    scene.bonePalettes.resize(ShaderConstants::MaxSkinMatrices);
    PreparedMeshDraw draw;
    auto accept = [&](uint32_t offset, uint32_t count)
    {
        fixture.Error.clear();
        const auto item = fixture.Skin(fixture.Opaque, 0, offset, count);
        Require(ScenePreparation::PrepareSkinnedMeshDraw(scene, fixture.Resources, item, draw, fixture.Error),
            "valid palette range rejected");
        Require(draw.PaletteOffset == offset && draw.PaletteCount == count, "accepted palette range");
    };
    auto reject = [&](SkinnedMeshItem item)
    {
        fixture.Error.clear();
        Require(!ScenePreparation::PrepareSkinnedMeshDraw(scene, fixture.Resources, item, draw, fixture.Error),
            "invalid Skinned draw accepted");
        Require(!fixture.Error.empty(), "invalid draw reports diagnostic");
        Require(draw.Mesh == nullptr && draw.PaletteOffset == 0 && draw.PaletteCount == 0,
            "failed preparation clears reused output");
    };
    accept(0, 256);
    accept(255, 1);
    // Palette ranges and Skinned format are caller contracts asserted in Debug.
    // Do not pass contract violations to the normal in-process test executable.
    auto missingMesh = fixture.Skin(fixture.Opaque, 0);
    missingMesh.mesh = {};
    reject(missingMesh);
    reject(fixture.Skin(fixture.Opaque, 0, 0, 2, 2));
    accept(0, 2);
    Require(ScenePreparation::PrepareMeshDraw(fixture.Resources, fixture.Static,
        Matrix4::Identity.Data(), 0, fixture.Opaque, draw, fixture.Error), "reuse output for Static");
    Require(draw.PaletteOffset == 0 && draw.PaletteCount == 0, "Static clears previous skin range");
    std::cout << "PASS: valid palette boundaries, missing resources, invalid submeshes, output reuse\n";

    scene.items = { {fixture.Skinned}, {fixture.Static, 0, fixture.Opaque} };
    scene.skinnedItems = { missingMesh, fixture.Skin(fixture.Masked, 0) };
    auto prepared = ScenePreparation::PrepareScene(scene, fixture.Resources, fixture.Error);
    Require(prepared.Draws.size() == 2 && prepared.Opaque == std::vector<size_t>{0}
        && prepared.Masked == std::vector<size_t>{1} && !fixture.Error.empty(),
        "reject only invalid draws and preserve diagnostic after later successful draws");
    prepared = ScenePreparation::PrepareScene({}, fixture.Resources, fixture.Error);
    Require(prepared.Draws.empty() && prepared.BonePalettes.empty()
        && prepared.Opaque.empty() && prepared.Masked.empty() && prepared.Transparent.empty()
        && prepared.ShadowCasters.empty() && fixture.Error.empty(), "empty scene resets data and diagnostic");
    std::cout << "PASS: individual draw rejection, later valid draws, empty scene reset\n";
}

int main()
{
    try
    {
        Fixture fixture;
        QueueAndOwnershipChecks(fixture);
        RangeAndFailureChecks(fixture);
        ComPtr<ID3D11InfoQueue> messages;
        Require(SUCCEEDED(fixture.Device.As(&messages)), "D3D11 debug info queue");
        Require(messages->GetNumStoredMessages() == 0, "unexpected D3D11 debug message");
        std::cout << "PASS: no D3D11 debug messages\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
