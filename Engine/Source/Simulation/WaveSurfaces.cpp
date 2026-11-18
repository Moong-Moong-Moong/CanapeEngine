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
        constexpr auto RippleShaderPath = L"Shaders/WaveRipple.hlsl";
        constexpr std::uint32_t MinPointsPerSide = 2;
        constexpr std::uint32_t MaxPointsPerSide = 1024;
        // The kernel divides by the cell size.
        constexpr float MinRippleCellSize = 0.001f;
        // Of a ripple step: how far a wave may travel, in cells.
        constexpr float MaxRippleCourant = 0.7f;

        // Mirrors cbuffer WaveSurfaceGeometry in Shader/WaveSurface.hlsl.
        struct WaveSurfaceGeometry
        {
            Vector2 areaOrigin;
            Vector2 areaExtent;
            float baseHeight = 0.0f;
            std::uint32_t pointsPerSide = 0;
            Vector2 pad_;
        };
        static_assert(sizeof(WaveSurfaceGeometry) == 32);

        // Mirrors cbuffer WaveSurfaceLook in Shader/WaveSurface.hlsl.
        struct WaveSurfaceLook
        {
            Vector3 deepColor;
            float pad0_ = 0.0f;
            Vector3 shallowColor;
            float pad1_ = 0.0f;
        };
        static_assert(sizeof(WaveSurfaceLook) == 32);

        // Mirrors cbuffer WaveRipple in Shader/WaveRipple.hlsl.
        struct WaveRipple
        {
            Vector2 areaOrigin;
            Vector2 cellSize;
            float speed = 0.0f;
            float damping = 0.0f;
            float strength = 0.0f;
            std::uint32_t pointsPerSide = 0;
        };
        static_assert(sizeof(WaveRipple) == 32);

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

        WaveRipple rippleOf(
            const WaveSurfaceComponent& surface,
            const WaveSurfaceGeometry& geometry
        )
        {
            const auto cells = static_cast<float>(geometry.pointsPerSide - 1);
            const Vector2 cellSize {
                std::max(geometry.areaExtent.X / cells, MinRippleCellSize),
                std::max(geometry.areaExtent.Y / cells, MinRippleCellSize)
            };
            const auto maxSpeed = MaxRippleCourant
                * std::min(cellSize.X, cellSize.Y)
                / WaveSurfaces::MaxStepSeconds;

            return {
                .areaOrigin = geometry.areaOrigin,
                .cellSize = cellSize,
                .speed = std::clamp(surface.GetRippleSpeed(), 0.0f, maxSpeed),
                .damping = std::max(surface.GetRippleDamping(), 0.0f),
                .strength = surface.GetRippleStrength(),
                .pointsPerSide = geometry.pointsPerSide
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
            const auto ripple = rippleOf(*component, geometry);
            const FieldDesc rippleSize {
                .width = geometry.pointsPerSide,
                .height = geometry.pointsPerSide
            };

            auto kept = surfaces.extract(component);
            // A new grid size starts the ripples over, so the whole surface is made again.
            const bool reusable = !kept.empty() && kept.mapped().ripples.GetDesc() == rippleSize;
            if(!reusable)
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
                        ),
                        .ripples = Field(session, rippleSize),
                        .rippleKernel = Kernel(
                            session,
                            { RippleShaderPath, "cs_main" },
                            ripple
                        )
                    }
                );
                continue;
            }

            auto& surface = kept.mapped();
            surface.geometry.Upload(geometry);
            surface.material.Upload(look);
            surface.rippleKernel.Upload(ripple);
            updated.insert(std::move(kept));
        }
        surfaces = std::move(updated);
    }

    void WaveSurfaces::Step(Simulator& simulator)
    {
        for(auto& [component, surface] : surfaces)
        {
            simulator.Dispatch(surface.rippleKernel, surface.ripples);
        }
    }

    void WaveSurfaces::Draw(Simulator& simulator) const
    {
        for(const auto& [component, surface] : surfaces)
        {
            simulator.Draw(surface.material, surface.geometry, surface.ripples);
        }
    }
}
