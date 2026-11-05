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
		std::vector<Component*> GetPendingRemove() const { return m_pendingRemove; }


	private:
		std::vector<ScriptComponent*> m_scripts;
		std::vector<RenderableComponent*> m_renderables;
		std::vector<LightComponent*> m_lights;
		std::vector<FogComponent*> m_fogs;
		std::vector<SkyboxComponent*> m_skyboxes;
		std::vector<RigidBodyComponent*> m_rigidBodies;

		std::vector<Component*> m_pendingAdd;
		std::vector<Component*> m_pendingRemove;
	};
}
