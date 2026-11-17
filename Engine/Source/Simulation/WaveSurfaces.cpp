#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include "Simulation/WaveSurfaces.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "DX11Simulation/Simulator.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Scene.h"
#include "Simulation/WaveSurfaceComponent.h"

namespace Canape
{
    namespace
    {
        constexpr auto ShaderPath = L"Shaders/WaveSurface.hlsl";
        constexpr std::uint32_t MinPointsPerSide = 2;
        constexpr std::uint32_t MaxPointsPerSide = 1024;
        // The shader divides by the wavelength.
        constexpr float MinSwellWavelength = 0.1f;

        // Mirrors cbuffer WaveSurfaceGeometry in Shader/WaveSurface.hlsl.
        struct WaveSurfaceGeometry
        {
            Vector2 areaOrigin;
            Vector2 areaExtent;
            Vector2 swellDirection;
            float swellAmplitude = 0.0f;
            float swellWavelength = 0.0f;
            float swellSpeed = 0.0f;
            float baseHeight = 0.0f;
            std::uint32_t pointsPerSide = 0;
            float padding = 0.0f;
        };
        static_assert(sizeof(WaveSurfaceGeometry) == 48);

        // Mirrors cbuffer WaveSurfaceLook in Shader/WaveSurface.hlsl.
        struct WaveSurfaceLook
        {
            Vector3 deepColor;
            float padding0 = 0.0f;
            Vector3 shallowColor;
            float padding1 = 0.0f;
        };
        static_assert(sizeof(WaveSurfaceLook) == 32);

        WaveSurfaceGeometry geometryOf(const WaveSurfaceComponent& surface)
        {
            const auto center = surface.GetTransform()->GetPosition();
            const auto& size = surface.GetAreaSize();

            return {
                .areaOrigin = {
                    center.X - size.X * 0.5f,
                    center.Z - size.Y * 0.5f
                },
                .areaExtent = size,
                .swellDirection = { 0.0f, 1.0f },
                .swellAmplitude = surface.GetSwellAmplitude(),
                .swellWavelength = std::max(
                    surface.GetSwellWavelength(),
                    MinSwellWavelength
                ),
                .swellSpeed = surface.GetSwellSpeed(),
                .baseHeight = center.Y,
                .pointsPerSide = std::clamp(
                    surface.GetResolution(),
                    MinPointsPerSide,
                    MaxPointsPerSide
                )
            };
        }

        WaveSurfaceLook lookOf(const WaveSurfaceComponent& surface)
        {
            return {
                .deepColor = surface.GetDeepColor(),
                .shallowColor = surface.GetShallowColor()
            };
        }

        std::uint32_t vertexCountOf(const WaveSurfaceGeometry& geometry)
        {
            const auto cells = geometry.pointsPerSide - 1;
            return cells * cells * 6;
        }
    }

    void WaveSurfaces::Update(Session& session, Components components)
    {
        SurfacesByComponent updated;
        for(const auto* component : components)
        {
            if(!Scene::IsRunnable(component)) continue;

            const auto geometry = geometryOf(*component);
            const auto look = lookOf(*component);

            auto kept = surfaces.extract(component);
            if(kept.empty())
            {
                updated.emplace(
                    component,
                    Surface {
                        .geometry = Geometry(
                            session,
                            geometry,
                            vertexCountOf(geometry)
                        ),
                        .material = Material(
                            session,
                            { ShaderPath, "vs_main" },
                            { ShaderPath, "ps_main" },
                            look
                        )
                    }
                );
                continue;
            }

            auto& surface = kept.mapped();
            surface.geometry.Upload(session, geometry);
            surface.geometry.SetVertexCount(vertexCountOf(geometry));
            surface.material.Upload(session, look);
            updated.insert(std::move(kept));
        }
        surfaces = std::move(updated);
    }

    void WaveSurfaces::Draw(Simulator& simulator) const
    {
        for(const auto& [component, surface] : surfaces)
        {
            simulator.Draw(surface.material, surface.geometry);
        }
    }
}
