#pragma once

#include "Scene/Component/Component.h"
#include "Renderer/Mesh.h"

#include <memory>
#include <utility>

namespace Canape
{
	class MeshRendererComponent : public Component
	{
	public:
		MeshRendererComponent() = default;
		explicit MeshRendererComponent(std::shared_ptr<Mesh> mesh) : m_Mesh(std::move(mesh)) {}

		const std::shared_ptr<Mesh>& GetMesh() const { return m_Mesh; }
		void SetMesh(std::shared_ptr<Mesh> mesh) { m_Mesh = std::move(mesh); }

	private:
		std::shared_ptr<Mesh> m_Mesh;
	};
}
