#include "DX11RenderScene.h"

namespace Canape
{
	void DX11RenderScene::SetCamera(const DX11RenderView& camera)
	{
		m_Camera = camera;
	}

	void DX11RenderScene::AddRenderItem(const DX11RenderItem& item)
	{
		m_Items.push_back(item);
	}

	void DX11RenderScene::Clear()
	{
		m_Camera = {};
		m_Items.clear();
	}
}
