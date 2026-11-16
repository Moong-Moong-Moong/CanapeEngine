#pragma once

#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/Handles.h"

namespace Canape
{
    class Session;

    class Outer
    {
    private:
        BufferHandle constants;

    public:
        Outer(Session&, ConstantView);

        void Upload(Session&, ConstantView);

        BufferHandle GetConstants() const
        {
            return constants;
        }
    };
}
