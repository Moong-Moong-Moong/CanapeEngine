#ifndef CANAPE_SWELL_HLSLI
#define CANAPE_SWELL_HLSLI

#include "Simulation.hlsli"

// Relative to the main wave; Turn in radians.
struct SwellWave
{
    float WavelengthScale;
    float AmplitudeScale;
    float Turn;
};

// Of the main wave, over WindSpeed^2 / Gravity: half the height of a fully developed sea.
static const float SwellAmplitudeFactor = 0.105;
static const uint SwellWaveCount = 3;
static const SwellWave SwellWaves[SwellWaveCount] = {
    { 1.0, 1.0, 0.0 },
    { 0.53, 0.45, 0.6 },
    { 0.29, 0.2, -0.9 }
};

// The swell the wind raises at xz, over the sea's base height. A wave shorter than shortestWavelength fades out.
float SwellHeight(float2 xz, float shortestWavelength, out float2 gradient)
{
    // Deep water: a wave as fast as the wind is this long.
    const float mainWavelength = TwoPi * WindSpeed * WindSpeed / Gravity;
    const float mainAmplitude = SwellAmplitudeFactor * WindSpeed * WindSpeed / Gravity;

    float height = 0.0;
    gradient = float2(0.0, 0.0);

    [unroll]
    for (uint i = 0; i < SwellWaveCount; ++i)
    {
        const SwellWave wave = SwellWaves[i];
        float sinTurn;
        float cosTurn;
        sincos(wave.Turn, sinTurn, cosTurn);
        const float2 direction = float2(
            WindDirection.x * cosTurn - WindDirection.y * sinTurn,
            WindDirection.x * sinTurn + WindDirection.y * cosTurn
        );
        const float wavelength = mainWavelength * wave.WavelengthScale;
        const float k = TwoPi / max(wavelength, shortestWavelength);

        // Deep water: the speed scales with the square root of the wavelength.
        const float phase = k * (dot(xz, direction) - WindSpeed * sqrt(wave.WavelengthScale) * SimulationTime);
        const float amplitude = mainAmplitude * wave.AmplitudeScale * saturate(wavelength / shortestWavelength - 1.0);
        height += amplitude * sin(phase);
        gradient += amplitude * k * cos(phase) * direction;
    }

    return height;
}

#endif
