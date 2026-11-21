#pragma once

#include <cstdint>
#include <utility>

#include "DX11Simulation/Handles.h"

namespace Canape
{
    class Session;

    struct FieldDesc
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;

        bool operator==(const FieldDesc&) const = default;
    };

    // A 2D grid of simulation state kept across frames: two floats per cell, zero at first.
    // A dispatch reads the latest texture and writes the next one.
    class Field
    {
    private:
        Session& session;
        FieldDesc desc;
        TextureHandle latest;
        TextureHandle next;

    public:
        Field(Session&, const FieldDesc&);

        void Advance()
        {
            std::swap(latest, next);
        }

        const FieldDesc& GetDesc() const
        {
            return desc;
        }

        Texture& GetLatest() const;
        Texture& GetNext() const;
    };
}
