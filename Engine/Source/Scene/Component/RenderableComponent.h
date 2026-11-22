#pragma once

#include "Scene/Component/Component.h"

namespace Canape
{
	class AssetManager;
	class Renderer;

	class RenderableComponent : public Component
	{
		RTTR_ENABLE(Component)

	public:
		virtual ~RenderableComponent() = default;
		virtual void AddRenderItem(const AssetManager& assets, Renderer& renderer) const = 0;

		// false면 렌더 아이템을 아예 제출하지 않는다(메인/그림자 모두). 컴포넌트의 다른 동작은 그대로.
		bool IsVisible() const { return m_Visible; }
		void SetVisible(bool visible) { m_Visible = visible; }

	protected:
		RenderableComponent() = default;

	private:
		bool m_Visible = true;
	};
}
