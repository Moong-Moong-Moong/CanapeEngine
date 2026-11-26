#include "Simulation/TerrainGeneration.h"

#include <algorithm>
#include <cstdint>

#include "DX11Simulation/Simulator.h"
#include "Math/Matrix4.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Scene/Component/TransformComponent.h"
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
        constexpr float SideMeters = (PointsPerSide - 1) * CellSizeMeters;

        // Mirrors cbuffer TerrainUplift in Shader/TerrainUplift.hlsl.
        struct TerrainUplift
        {
            std::uint32_t seed = 0;
            float baseHeight = 0.0f;
            float maxHeight = 0.0f;
            float cellSize = 0.0f;
            std::uint32_t pointsPerSide = 0;
            Vector3 pad_;
        };
        static_assert(sizeof(TerrainUplift) == 32);

        // Mirrors cbuffer TerrainGeometry in Shader/TerrainSurface.hlsl.
        struct TerrainGeometry
        {
            float cellSize = 0.0f;
            std::uint32_t pointsPerSide = 0;
            Vector2 pad_;
            Matrix4 modelToWorld;
        };
        static_assert(sizeof(TerrainGeometry) == 80);

        // Mirrors cbuffer TerrainLook in Shader/TerrainSurface.hlsl.
        struct TerrainLook
        {
            Vector3 color;
            float pad_ = 0.0f;
        };
        static_assert(sizeof(TerrainLook) == 16);

        TerrainGeometry geometryOf(const TerrainGeneratorComponent& component)
        {
            return {
                .cellSize = CellSizeMeters,
                .pointsPerSide = PointsPerSide,
                .modelToWorld = Matrix4::Scale((TerrainBoxMax - TerrainBoxMin) / SideMeters)
                    * Matrix4::Translation(TerrainBoxMin)
                    * component.GetTransform()->GetLocalToWorldMatrix()
            };
        }
    }

    void TerrainGeneration::Request(const TerrainGeneratorComponent& component)
    {
        requests.insert_or_assign(
            &component,
            Inputs {
                .seed = component.GetSeed(),
                .baseHeightMeters = component.GetBaseHeightRatio() * SideMeters,
                .maxHeightMeters = component.GetMaxHeightRatio() * SideMeters
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
                .baseHeight = request->second.baseHeightMeters,
                .maxHeight = request->second.maxHeightMeters,
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
                            geometryOf(*component),
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

        for(auto& [component, workspace] : workspaces)
        {
            workspace.geometry.Upload(geometryOf(*component));
        }
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
