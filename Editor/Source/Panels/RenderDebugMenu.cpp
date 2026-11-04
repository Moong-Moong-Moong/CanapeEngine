#include "Panels/RenderDebugMenu.h"

#include <imgui.h>

#include <iterator>
#include <string>

using namespace Canape;

namespace
{
    constexpr const char* kPopupId = "##RenderDebugPopup";
    constexpr float kPopupWidth = 320.0f;

    struct ViewModeOption
    {
        DebugViewMode mode;
        const char* name;
    };

    constexpr ViewModeOption kViewModes[] = {
        { DebugViewMode::Final, "Final" },
        { DebugViewMode::CameraDepth, "Camera Depth" },
        { DebugViewMode::WorldNormal, "World Normal" },
        { DebugViewMode::UV, "UV" },
        { DebugViewMode::MipLOD, "Mip LOD" },
        { DebugViewMode::ShadowDepth, "Shadow Depth" },
        { DebugViewMode::ShadowVisibility, "Shadow Visibility" },
        { DebugViewMode::PcssFilterRadius, "PCSS Filter Radius" },
    };

    constexpr const char* kCullModes[] = { "None", "Front", "Back" };

    const char* GetViewModeName(DebugViewMode mode)
    {
        for (const ViewModeOption& option : kViewModes)
        {
            if (option.mode == mode)
            {
                return option.name;
            }
        }
        return "Unknown";
    }
}

float RenderDebugMenu::GetButtonWidth() const
{
    return ImGui::CalcTextSize(kButtonLabel).x + ImGui::GetStyle().FramePadding.x * 2.0f;
}

void RenderDebugMenu::Draw(Renderer& renderer)
{
    const bool active = renderer.GetDebugSettings().enabled;
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.35f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.7f, 0.45f, 0.15f, 1.0f));
    }
    if (ImGui::Button(kButtonLabel))
    {
        ImGui::OpenPopup(kPopupId);
    }
    if (active)
    {
        ImGui::PopStyleColor(2);
    }

    const ImVec2 buttonMax = ImGui::GetItemRectMax();
    ImGui::SetNextWindowPos(ImVec2(buttonMax.x, buttonMax.y + 2.0f), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(kPopupWidth, 0.0f), ImVec2(kPopupWidth, FLT_MAX));
    if (!ImGui::BeginPopup(kPopupId))
    {
        return;
    }

    const RendererDebugSettings settings = renderer.GetDebugSettings();
    ImGui::PushItemWidth(-150.0f);
    if (Renderer::IsDebugAvailable())
    {
        DrawViewOptions(renderer, settings);
        DrawShadowOptions(renderer, settings);
    }
    else
    {
        ImGui::TextDisabled("Debug views are disabled in this build");
    }
    DrawPostProcessOptions(renderer);
    DrawPcssOptions(renderer);
    if (Renderer::IsDebugAvailable())
    {
        DrawStats(renderer, settings);
    }
    ImGui::PopItemWidth();

    ImGui::Separator();
    if (ImGui::Button("Reset All", ImVec2(-FLT_MIN, 0.0f)))
    {
        renderer.ResetDebugSettings();
        renderer.ResetShadowFilterSettings();
        renderer.ResetPostProcessSettings();
    }
    ImGui::EndPopup();
}

void RenderDebugMenu::DrawPostProcessOptions(Renderer& renderer)
{
    ImGui::SeparatorText("Post Processing");

    bool toneMapping = renderer.GetPostProcessSettings().toneMappingEnabled;
    if (ImGui::Checkbox("Tone Mapping", &toneMapping))
    {
        renderer.SetToneMappingEnabled(toneMapping);
    }

    const RendererDebugSettings& debug = renderer.GetDebugSettings();
    if (Renderer::IsDebugAvailable() && debug.enabled && debug.viewMode != DebugViewMode::Final)
    {
        ImGui::TextDisabled("Bypassed in debug views");
    }
}

void RenderDebugMenu::DrawPcssOptions(Renderer& renderer)
{
    ImGui::SeparatorText("PCSS");

    const ShadowFilterSettings settings = renderer.GetShadowFilterSettings();

    float distance = settings.shadowDistance;
    if (ImGui::DragFloat("Shadow Distance", &distance, 0.5f, MinShadowDistance, 500.0f, "%.1f", ImGuiSliderFlags_AlwaysClamp))
    {
        renderer.SetShadowDistance(distance);
    }

    bool pcss = settings.pcss;
    if (ImGui::Checkbox("PCSS", &pcss))
    {
        renderer.SetPcss(pcss);
    }

    ImGui::BeginDisabled(!settings.pcss);
    DrawSampleCountCombo("PCF Samples", settings.pcfSampleCount, [&renderer](uint32_t count)
    {
        renderer.SetPcfSampleCount(count);
    });

    ImGui::BeginDisabled(settings.pcfSampleCount != 25);
    bool legacy = settings.legacyPcf25;
    if (ImGui::Checkbox("Legacy 25-Tap Pattern", &legacy))
    {
        renderer.SetLegacyPcf25(legacy);
    }
    ImGui::EndDisabled();

    float spacing = settings.pcfTapSpacingScale;
    if (ImGui::DragFloat("Tap Spacing", &spacing, 0.01f, 0.1f, 4.0f, "%.2fx", ImGuiSliderFlags_AlwaysClamp))
    {
        renderer.SetPcfTapSpacingScale(spacing);
    }

    DrawSampleCountCombo("Blocker Samples", settings.blockerSampleCount, [&renderer](uint32_t count)
    {
        renderer.SetBlockerSampleCount(count);
    });

    float radius = settings.blockerSearchRadius;
    if (ImGui::DragFloat("Blocker Radius", &radius, 0.005f, 0.05f, 2.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp))
    {
        renderer.SetBlockerSearchRadius(radius);
    }
    ImGui::EndDisabled();
}

void RenderDebugMenu::DrawSampleCountCombo(const char* label, uint32_t current, const std::function<void(uint32_t)>& apply)
{
    const std::string preview = std::to_string(current);
    if (!ImGui::BeginCombo(label, preview.c_str()))
    {
        return;
    }

    for (const uint32_t count : ShadowSampleCountOptions)
    {
        const std::string option = std::to_string(count);
        if (ImGui::Selectable(option.c_str(), count == current))
        {
            apply(count);
        }
    }
    ImGui::EndCombo();
}

void RenderDebugMenu::DrawViewOptions(Renderer& renderer, const RendererDebugSettings& settings)
{
    ImGui::SeparatorText("View");

    bool enabled = settings.enabled;
    if (ImGui::Checkbox("Debug View", &enabled))
    {
        renderer.SetDebugEnabled(enabled);
    }

    ImGui::BeginDisabled(!settings.enabled);
    if (ImGui::BeginCombo("View Mode", GetViewModeName(settings.viewMode)))
    {
        for (const ViewModeOption& option : kViewModes)
        {
            if (ImGui::Selectable(option.name, option.mode == settings.viewMode))
            {
                renderer.SetDebugViewMode(option.mode);
            }
        }
        ImGui::EndCombo();
    }

    bool wireframe = settings.wireframe;
    if (ImGui::Checkbox("Wireframe", &wireframe))
    {
        renderer.SetWireframe(wireframe);
    }

    if (settings.viewMode == DebugViewMode::CameraDepth)
    {
        float depthMin = settings.depthMin;
        float depthMax = settings.depthMax;
        if (ImGui::DragFloatRange2("Depth Range", &depthMin, &depthMax, 0.0005f, 0.0f, 1.0f, "%.4f", "%.4f",
            ImGuiSliderFlags_AlwaysClamp) && depthMin < depthMax)
        {
            renderer.SetDebugDepthRange(depthMin, depthMax);
        }
    }
    ImGui::EndDisabled();
}

void RenderDebugMenu::DrawShadowOptions(Renderer& renderer, const RendererDebugSettings& settings)
{
    ImGui::SeparatorText("Shadow");

    int cullMode = static_cast<int>(settings.shadowCullMode);
    if (ImGui::Combo("Cull Mode", &cullMode, kCullModes, static_cast<int>(std::size(kCullModes))))
    {
        renderer.SetShadowCullMode(static_cast<ShadowCullMode>(cullMode));
    }

    float slopeBias = settings.shadowSlopeBias;
    if (ImGui::DragFloat("Slope Bias", &slopeBias, 0.05f, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp))
    {
        renderer.SetShadowSlopeBias(slopeBias);
    }

    float receiverBias = settings.shadowReceiverBias;
    if (ImGui::DragFloat("Receiver Bias", &receiverBias, 0.00001f, 0.0f, 0.01f, "%.5f", ImGuiSliderFlags_AlwaysClamp))
    {
        renderer.SetShadowReceiverBias(receiverBias);
    }

    bool pcf = settings.shadowPcf;
    if (ImGui::Checkbox("PCF", &pcf))
    {
        renderer.SetShadowPcf(pcf);
    }
}

void RenderDebugMenu::DrawStats(Renderer& renderer, const RendererDebugSettings& settings)
{
    ImGui::SeparatorText("Stats");

    bool captureGpuTimings = settings.captureGpuTimings;
    if (ImGui::Checkbox("GPU Timings", &captureGpuTimings))
    {
        renderer.SetGpuTimingCapture(captureGpuTimings);
    }

    const RendererDebugStats stats = renderer.GetDebugStats();
    ImGui::Text("Draw Calls  %u", stats.drawCalls);
    ImGui::Text("Triangles   %llu", static_cast<unsigned long long>(stats.triangleCount));
    if (!settings.captureGpuTimings)
    {
        ImGui::TextDisabled("GPU         off");
    }
    else if (!stats.gpuTimingValid)
    {
        ImGui::TextDisabled("GPU         pending / unavailable");
    }
    else
    {
        ImGui::Text("GPU Frame   %.3f ms", stats.gpuFrameMs);
        ImGui::Text("  Scene     %.3f ms", stats.drawSceneMs);
        ImGui::Text("  Output    %.3f ms", stats.finalOutputMs);
    }
}
