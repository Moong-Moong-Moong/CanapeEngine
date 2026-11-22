#pragma once

#include "DX11Simulation/ConstantView.h"
#include "DX11Simulation/Handles.h"

namespace Canape
{
    class Session;

    class Outer
    {
    private:
        Session& session;
        BufferHandle constants;

    public:
        Outer(Session&, ConstantView);

        void Upload(ConstantView);

        Buffer& GetConstants() const;
    };
}
