#include "Simulation/TerrainGeneration.h"

#include <algorithm>
#include <cstdint>

#include "DX11Simulation/Simulator.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Scene/Scene.h"
#include "Simulation/TerrainGeneratorComponent.h"

namespace Canape
{
    namespace
    {
        constexpr auto SurfaceShaderPath = L"Shaders/TerrainSurface.hlsl";
        constexpr auto UpliftShaderPath = L"Shaders/TerrainUplift.hlsl";
        constexpr std::uint32_t PointsPerSide = 769;
        constexpr float CellSizeMeters = 4.0f;

        // Mirrors cbuffer TerrainUplift in Shader/TerrainUplift.hlsl.
        struct TerrainUplift
        {
            std::uint32_t seed = 0;
            float amount = 0.0f;
            float cellSize = 0.0f;
            std::uint32_t pointsPerSide = 0;
        };
        static_assert(sizeof(TerrainUplift) == 16);

        // Mirrors cbuffer TerrainGeometry in Shader/TerrainSurface.hlsl.
        struct TerrainGeometry
        {
            float cellSize = 0.0f;
            std::uint32_t pointsPerSide = 0;
            Vector2 pad_;
        };
        static_assert(sizeof(TerrainGeometry) == 16);

        // Mirrors cbuffer TerrainLook in Shader/TerrainSurface.hlsl.
        struct TerrainLook
        {
            Vector3 color;
            float pad_ = 0.0f;
        };
        static_assert(sizeof(TerrainLook) == 16);
    }

    void TerrainGeneration::Request(const TerrainGeneratorComponent& component)
    {
        requests.insert_or_assign(
            &component,
            Inputs {
                .seed = component.GetSeed(),
                .amount = component.GetAmount()
            }
        );
    }

    void TerrainGeneration::Update(Session& session, Components components)
    {
        std::erase_if(
            workspaces,
            [components](const auto& entry)
            {
                return std::ranges::find(components, entry.first) == components.end();
            }
        );

        for(const auto* component : components)
        {
            const auto request = requests.find(component);
            if(request == requests.end()) continue;

            const TerrainUplift uplift {
                .seed = request->second.seed,
                .amount = request->second.amount,
                .cellSize = CellSizeMeters,
                .pointsPerSide = PointsPerSide
            };
            auto found = workspaces.find(component);
            if(found == workspaces.end())
            {
                found = workspaces.emplace(
                    component,
                    Workspace {
                        .geometry = Geometry(
                            session,
                            TerrainGeometry {
                                .cellSize = CellSizeMeters,
                                .pointsPerSide = PointsPerSide
                            },
                            (PointsPerSide - 1) * (PointsPerSide - 1) * 6
                        ),
                        .material = Material(
                            session,
                            { SurfaceShaderPath, "vs_main" },
                            { SurfaceShaderPath, "ps_main" },
                            TerrainLook { .color = { 0.34f, 0.31f, 0.25f } }
                        ),
                        .ground = Field(
                            session,
                            { .width = PointsPerSide, .height = PointsPerSide }
                        ),
                        .upliftKernel = Kernel(
                            session,
                            { UpliftShaderPath, "cs_main" },
                            uplift
                        )
                    }
                ).first;
            }
            else
            {
                found->second.upliftKernel.Upload(uplift);
            }

            auto& workspace = found->second;
            workspace.remainingStages = { &workspace.upliftKernel };
        }
        requests.clear();
    }

    void TerrainGeneration::Step(Simulator& simulator)
    {
        for(auto& [component, workspace] : workspaces)
        {
            for(const auto* stage : workspace.remainingStages)
            {
                simulator.Dispatch(
                    *stage,
                    workspace.ground
                );
            }
            workspace.remainingStages.clear();
        }
    }

    void TerrainGeneration::Draw(Simulator& simulator) const
    {
        for(const auto& [component, workspace] : workspaces)
        {
            if(!Scene::IsRunnable(component)) continue;

            simulator.Draw(
                workspace.material,
                workspace.geometry,
                workspace.ground
            );
        }
    }
}
