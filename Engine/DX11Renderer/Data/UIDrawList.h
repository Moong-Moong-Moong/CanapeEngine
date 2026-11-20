#pragma once

#include "DX11Renderer/Resource/DX11ResourceId.h"

#include <cstdint>
#include <vector>

namespace Canape
{
    // UI geometry in canvas pixels (top-left origin, +Y down). Screen-space commands use
    // render-target pixels; world-space commands use their canvas pixels and a UIDrawTransform.
    // Color is packed R8G8B8A8 (R in the low byte), sRGB-encoded with straight alpha.
    struct UIVertex
    {
        float Position[2];
        float UV[2];
        uint32_t Color;
    };

    // Inclusive-exclusive pixel rectangle used as the scissor for one screen-space command.
    struct UIClipRect
    {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Right = 0;
        int32_t Bottom = 0;

        bool operator==(const UIClipRect&) const = default;
    };

    // Canvas pixels -> clip space for a world-space canvas (row vector: clip = float4(x, y, 0, 1) * M,
    // row-major like Matrix4). DepthTest compares against the scene depth without writing it.
    struct UIDrawTransform
    {
        float ClipFromCanvas[16]{};
        bool DepthTest = true;
    };

    // An invalid texture means solid color (the renderer's white texture).
    // A valid but destroyed texture skips the command.
    // Transform == kScreenSpace: render-target pixels, scissored by Clip, no depth test.
    // Otherwise it indexes UIDrawList::Transforms; Clip is ignored (world canvases are culled on the CPU).
    struct UIDrawCommand
    {
        static constexpr uint32_t kScreenSpace = UINT32_MAX;

        TextureResourceId Texture;
        UIClipRect Clip;
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
        uint32_t Transform = kScreenSpace;
    };

    // Indices address Vertices directly. Commands draw in order (painter's order).
    struct UIDrawList
    {
        std::vector<UIVertex> Vertices;
        std::vector<uint32_t> Indices;
        std::vector<UIDrawCommand> Commands;
        std::vector<UIDrawTransform> Transforms;

        void Clear()
        {
            Vertices.clear();
            Indices.clear();
            Commands.clear();
            Transforms.clear();
        }

        bool IsEmpty() const { return Commands.empty(); }
    };
}
