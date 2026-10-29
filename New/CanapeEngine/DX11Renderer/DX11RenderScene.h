#pragma once

#include "DX11RenderItem.h"

#include <vector>

namespace Canape
{
	//***** 렌더링에 사용될 렌더 아이템들을 모아놓는 클래스
	class DX11RenderScene
	{
	public:
		void SetCamera(const DX11RenderView& camera);
		void AddRenderItem(const DX11RenderItem& item);
		void Clear();

		const DX11RenderView& GetCamera() const { return m_Camera; }
		const std::vector<DX11RenderItem>& GetItems() const { return m_Items; }

	private:
		DX11RenderView m_Camera;
		std::vector<DX11RenderItem> m_Items;
	};
}
