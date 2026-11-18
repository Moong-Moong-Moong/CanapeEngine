#pragma once

#include "Asset/AssetId.h"
#include "DX11Renderer/Data/UIDrawList.h"
#include "UI/UITypes.h"

#include <functional>

namespace Canape
{
	//*** 텍스처 에셋 ID → GPU 리소스 ID. 업로드되지 않았으면 빈 ID를 돌려준다.
	using UITextureResolver = std::function<TextureResourceId(TextureAssetId)>;

	//***** UIElement::OnPaint가 사각형을 UIDrawList에 쌓는 도구
	//*** 텍스처와 클립이 같은 연속 사각형은 한 드로우 명령으로 합친다.
	class UIPainter
	{
	public:
		UIPainter(UIDrawList& list, const UIClipRect& clip, const UITextureResolver* resolver = nullptr);

		//*** texture가 비어 있으면 단색. 텍스처가 아직 GPU에 없으면 건너뛴다.
		void AddQuad(const UIRect& rect, const UIRect& uv, UIColor color, TextureAssetId texture = {});

	private:
		UIDrawList& m_List;
		UIClipRect m_Clip;
		const UITextureResolver* m_Resolver = nullptr;
	};
}
