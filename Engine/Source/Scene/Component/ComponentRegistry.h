#pragma once

#include <span>
#include <vector>

namespace Canape
{
	class Component;
	class ScriptComponent;
	class RenderableComponent;
	class LightComponent;
	class FogComponent;
	class SkyboxComponent;
	class RigidBodyComponent;
	class HingeComponent;
	class WaterVolumeComponent;
	class TerrainGeneratorComponent;
	class WaveSurfaceComponent;
	class WindComponent;
	class ParticleEffectComponent;
	class AnimatorComponent;
	class UICanvasComponent;

	//***** Scene 이 순회할 특정 컴포넌트 목록을 보관
	class ComponentRegistry final
	{
	public:
		ComponentRegistry() = default;
		~ComponentRegistry() = default;

		ComponentRegistry(const ComponentRegistry&) = delete;
		ComponentRegistry& operator=(const ComponentRegistry&) = delete;
		
		void Register(Component* component);
		void Unregister(Component* component);
		void FlushPending();

		//*** Registers only physics components early so ones added during FixedUpdate join the same tick.
		void FlushPendingPhysics();

		std::span<ScriptComponent* const> GetScripts() const { return m_scripts; }
		std::span<RenderableComponent* const> GetRenderables() const { return m_renderables; }
		std::span<LightComponent* const> GetLights() const { return m_lights; }
		std::span<FogComponent* const> GetFogs() const { return m_fogs; }
		std::span<SkyboxComponent* const> GetSkyboxes() const { return m_skyboxes; }
		std::span<RigidBodyComponent* const> GetRigidBodies() const { return m_rigidBodies; }
		std::span<HingeComponent* const> GetHinges() const { return m_hinges; }
		std::span<WaterVolumeComponent* const> GetWaterVolumes() const { return m_waterVolumes; }
		std::span<TerrainGeneratorComponent* const> GetTerrainGenerators() const { return m_terrainGenerators; }
		std::span<WaveSurfaceComponent* const> GetWaveSurfaces() const { return m_waveSurfaces; }
		std::span<WindComponent* const> GetWinds() const { return m_winds; }
		std::span<ParticleEffectComponent* const> GetParticleEffects() const { return m_particleEffects; }
		std::span<AnimatorComponent* const> GetAnimators() const { return m_animators; }
		std::span<UICanvasComponent* const> GetUICanvases() const { return m_uiCanvases; }
		std::vector<Component*> GetPendingRemove() const { return m_pendingRemove; }


	private:
		std::vector<ScriptComponent*> m_scripts;
		std::vector<RenderableComponent*> m_renderables;
		std::vector<LightComponent*> m_lights;
		std::vector<FogComponent*> m_fogs;
		std::vector<SkyboxComponent*> m_skyboxes;
		std::vector<RigidBodyComponent*> m_rigidBodies;
		std::vector<HingeComponent*> m_hinges;
		std::vector<WaterVolumeComponent*> m_waterVolumes;
		std::vector<TerrainGeneratorComponent*> m_terrainGenerators;
		std::vector<WaveSurfaceComponent*> m_waveSurfaces;
		std::vector<WindComponent*> m_winds;
		std::vector<ParticleEffectComponent*> m_particleEffects;
		std::vector<AnimatorComponent*> m_animators;
		std::vector<UICanvasComponent*> m_uiCanvases;

		std::vector<Component*> m_pendingAdd;
		std::vector<Component*> m_pendingRemove;
	};
}
