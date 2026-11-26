#pragma once

#include <Engine.h>

#include <cstdint>
#include <functional>

class RenderDebugMenu
{
public:
    static constexpr const char* kButtonLabel = "Render Debug";

    float GetButtonWidth() const;
    // buttonScale shrinks only the toolbar button (narrow scene view); the popup keeps its normal size.
    void Draw(Canape::Renderer& renderer, float buttonScale = 1.0f);

private:
    void DrawViewOptions(Canape::Renderer& renderer, const Canape::RendererDebugSettings& settings);
    void DrawShadowOptions(Canape::Renderer& renderer, const Canape::RendererDebugSettings& settings);
    void DrawPcssOptions(Canape::Renderer& renderer);
    void DrawPostProcessOptions(Canape::Renderer& renderer);
    void DrawStats(Canape::Renderer& renderer, const Canape::RendererDebugSettings& settings);
    void DrawSampleCountCombo(const char* label, uint32_t current, const std::function<void(uint32_t)>& apply);
};
