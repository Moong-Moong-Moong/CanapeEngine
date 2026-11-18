#ifndef CANAPE_SIMULATION_HLSLI
#define CANAPE_SIMULATION_HLSLI

static const uint MaxWaveDisturbances = 8;

// Mirrors WaveDisturbance in Source/Simulation/SimulationScene.h.
struct WaveDisturbance
{
    float2 Center;
    float Radius;
    float Strength;
};

// Mirrors SimulationOuter in Source/Simulation/SimulationScene.cpp.
cbuffer SimulationOuter : register(b9)
{
    float SimulationTime;
    // Of the step being computed.
    float StepDeltaTime;
    uint StepNumber;
    uint WaveDisturbanceCount;
    // Unit in world XZ; zero when calm.
    float2 WindDirection;
    // Meters per second.
    float WindSpeed;
    float outerPad_;
    WaveDisturbance WaveDisturbances[MaxWaveDisturbances];
};

#endif
