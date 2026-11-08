#pragma once

#include <optional>

#include "JoltPhysics/PhysicsTypes.h"

namespace Canape
{
    // The runtime side of one water volume: the box last sent to the world, and which warnings were logged.
    struct WaterState {
        // Empty while the water is off, invalid or not yet applied.
        std::optional<PhysicsWaterDesc> applied;
        bool invalidLogged = false;
        bool rotationLogged = false;
    };
}
