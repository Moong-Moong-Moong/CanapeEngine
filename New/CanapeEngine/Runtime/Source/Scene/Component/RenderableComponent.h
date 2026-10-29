#pragma once

#include "Scene/Component/Component.h"

namespace Canape
{
	class AssetManager;
	class Renderer;

	class RenderableComponent : public Component
	{
	public:
		virtual ~RenderableComponent() = default;
		virtual void AddRenderItem(const AssetManager& assets, Renderer& renderer) const = 0;

	protected:
		RenderableComponent() = default;
	};
}
