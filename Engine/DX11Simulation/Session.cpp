#include "DX11Simulation/Session.h"

#include "DX11Simulation/Device.h"

namespace Canape
{
    Session::~Session() = default;

    Session::Session(ID3D11Device& device, ID3D11DeviceContext& context)
        : device(std::make_unique<Device>(device, context)) {}
}
