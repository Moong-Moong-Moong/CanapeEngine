#pragma once

#include "DX11Renderer/Resource/DX11ResourceId.h"

#include <cstdint>
#include <vector>

namespace Canape
{
    // Screen-space UI geometry in render-target pixels (top-left origin, +Y down).
    // Color is packed R8G8B8A8 (R in the low byte), sRGB-encoded with straight alpha.
    struct UIVertex
    {
        float Position[2];
        float UV[2];
        uint32_t Color;
    };

    // Inclusive-exclusive pixel rectangle used as the scissor for one command.
    struct UIClipRect
    {
        int32_t Left = 0;
        int32_t Top = 0;
        int32_t Right = 0;
        int32_t Bottom = 0;

        bool operator==(const UIClipRect&) const = default;
    };

    // An invalid texture means solid color (the renderer's white texture).
    // A valid but destroyed texture skips the command.
    struct UIDrawCommand
    {
        TextureResourceId Texture;
        UIClipRect Clip;
        uint32_t IndexOffset = 0;
        uint32_t IndexCount = 0;
    };

    // Indices address Vertices directly. Commands draw in order (painter's order).
    struct UIDrawList
    {
        std::vector<UIVertex> Vertices;
        std::vector<uint32_t> Indices;
        std::vector<UIDrawCommand> Commands;

        void Clear()
        {
            Vertices.clear();
            Indices.clear();
            Commands.clear();
        }

        bool IsEmpty() const { return Commands.empty(); }
    };
}
