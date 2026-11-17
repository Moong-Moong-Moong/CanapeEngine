#ifndef CANAPE_SIMULATION_HLSLI
#define CANAPE_SIMULATION_HLSLI

// Mirrors SimulationOuter in Source/Simulation/SimulationScene.cpp.
cbuffer SimulationOuter : register(b9)
{
    float SimulationTime;
    float3 SimulationOuterPadding;
};

#endif
