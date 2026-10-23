#pragma once

#include "Scene/Component/Component.h"

namespace Canape
{
	class AssetManager;
	class Renderer;
	struct RenderScene;

	class RenderableComponent : public Component
	{
	public:
		virtual ~RenderableComponent() = default;
		virtual void BuildRenderScene(RenderScene& renderScene, const AssetManager& assets, const Renderer& renderer) const = 0;

	protected:
		RenderableComponent() = default;
	};
}
