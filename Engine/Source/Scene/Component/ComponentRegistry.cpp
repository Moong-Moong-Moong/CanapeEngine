#include "Scene/Component/ComponentRegistry.h"
#include "Physics/HingeComponent.h"
#include "Physics/RigidBodyComponent.h"
#include "Scene/Component/FogComponent.h"
#include "Scene/Component/LightComponent.h"
#include "Scene/Component/RenderableComponent.h"
#include "Scene/Component/ScriptComponent.h"
#include "Scene/Component/SkyboxComponent.h"

#include <algorithm>
#include <vector>

namespace Canape
{
	void ComponentRegistry::Register(Component* component)
	{
		if (!component)
		{
			return;
		}

		m_pendingAdd.push_back(component);
	}

	void ComponentRegistry::Unregister(Component* component)
	{
		if (!component)
		{
			return;
		}

		m_pendingRemove.push_back(component);
	}

	void ComponentRegistry::FlushPendingPhysics()
	{
		std::erase_if(m_pendingAdd, [this](Component* component)
		{
            auto* rigidBody = dynamic_cast<RigidBodyComponent*>(component);
            auto* hinge = dynamic_cast<HingeComponent*>(component);
            if (rigidBody == nullptr && hinge == nullptr) return false;

            if (!component->IsPendingDestroy())
            {
                if (rigidBody != nullptr) m_rigidBodies.push_back(rigidBody);
                if (hinge != nullptr) m_hinges.push_back(hinge);
            }
            return true;
        });
	}

	void ComponentRegistry::FlushPending()
	{
		//***** 추가
		for (Component* component : m_pendingAdd)
		{
			// 추가와 삭제가 동시에 이루어진다면 추가하지 않음
			if (component->IsPendingDestroy())
			{
				continue;
			}

			//*** ScriptComponent
			if (ScriptComponent* script = dynamic_cast<ScriptComponent*>(component))
			{
				m_scripts.push_back(script);
			}

			//*** RenderableComponent
			if (RenderableComponent* renderable = dynamic_cast<RenderableComponent*>(component))
			{
				m_renderables.push_back(renderable);
			}

			if (LightComponent* light = dynamic_cast<LightComponent*>(component))
			{
				m_lights.push_back(light);
			}

			if (FogComponent* fog = dynamic_cast<FogComponent*>(component))
			{
				m_fogs.push_back(fog);
			}

			if (SkyboxComponent* skybox = dynamic_cast<SkyboxComponent*>(component))
			{
				m_skyboxes.push_back(skybox);
			}

			if (RigidBodyComponent* rigidBody = dynamic_cast<RigidBodyComponent*>(component))
			{
				m_rigidBodies.push_back(rigidBody);
			}

			if (HingeComponent* hinge = dynamic_cast<HingeComponent*>(component))
			{
				m_hinges.push_back(hinge);
			}
		}
		m_pendingAdd.clear();

		//***** 삭제
		for (Component* component : m_pendingRemove)
		{
			//*** ScriptComponent
			if (ScriptComponent* script = dynamic_cast<ScriptComponent*>(component))
			{
				std::erase(m_scripts, script);
			}

			//*** RenderableComponent
			if (RenderableComponent* renderable = dynamic_cast<RenderableComponent*>(component))
			{
				std::erase(m_renderables, renderable);
			}

			if (LightComponent* light = dynamic_cast<LightComponent*>(component))
			{
				std::erase(m_lights, light);
			}

			if (FogComponent* fog = dynamic_cast<FogComponent*>(component))
			{
				std::erase(m_fogs, fog);
			}

			if (SkyboxComponent* skybox = dynamic_cast<SkyboxComponent*>(component))
			{
				std::erase(m_skyboxes, skybox);
			}

			if (RigidBodyComponent* rigidBody = dynamic_cast<RigidBodyComponent*>(component))
			{
				std::erase(m_rigidBodies, rigidBody);
			}

			if (HingeComponent* hinge = dynamic_cast<HingeComponent*>(component))
			{
				std::erase(m_hinges, hinge);
			}
		}
		m_pendingRemove.clear();
	}
}
