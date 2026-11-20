#pragma once

#include "Asset/AssetId.h"
#include "DX11Renderer/Data/UIDrawList.h"
#include "UI/UITypes.h"

#include <functional>
#include <vector>

namespace Canape
{
	class UIGlyphAtlas;

	//*** 텍스처 에셋 ID → GPU 리소스 ID. 업로드되지 않았으면 빈 ID를 돌려준다.
	using UITextureResolver = std::function<TextureResourceId(TextureAssetId)>;

	//***** UIElement::OnPaint가 사각형을 UIDrawList에 쌓는 도구
	//*** 텍스처와 클립이 같은 연속 사각형은 한 드로우 명령으로 합친다.
	//*** 클립 밖으로 완전히 나간 사각형은 아예 쌓지 않는다.
	class UIPainter
	{
	public:
		UIPainter(UIDrawList& list, const UIClipRect& clip, const UITextureResolver* resolver = nullptr);

		//*** texture가 비어 있으면 단색. 텍스처가 아직 GPU에 없으면 건너뛴다.
		void AddQuad(const UIRect& rect, const UIRect& uv, UIColor color, TextureAssetId texture = {});
		//*** GPU 텍스처를 직접 쓰는 사각형(글리프 아틀라스, 코드로 만든 텍스처). 빈 ID면 건너뛴다.
		void AddQuadResource(const UIRect& rect, const UIRect& uv, UIColor color, TextureResourceId texture);

		//*** 현재 클립과 교집합을 새 클립으로 쌓는다.
		void PushClip(const UIRect& rect);
		void PopClip();
		const UIClipRect& GetClip() const { return m_ClipStack.back(); }

		//*** 단색 사각형을 이 텍스처의 한 점(흰 텍셀)으로 그린다. 글자와 같은 텍스처라 한 드로우로 묶인다.
		void SetSolidSource(TextureResourceId texture, const Vector2& uv);

		void SetGlyphAtlas(UIGlyphAtlas* atlas) { m_Atlas = atlas; }
		UIGlyphAtlas* GetGlyphAtlas() const { return m_Atlas; }

		//*** 이후 사각형이 쓸 변환(UIDrawList::Transforms의 번호). 기본은 화면 공간.
		void SetTransform(uint32_t transform) { m_Transform = transform; }

	private:
		void Emit(const UIRect& rect, const UIRect& uv, UIColor color, TextureResourceId texture);

		UIDrawList& m_List;
		std::vector<UIClipRect> m_ClipStack;
		const UITextureResolver* m_Resolver = nullptr;
		UIGlyphAtlas* m_Atlas = nullptr;
		TextureResourceId m_SolidTexture;
		Vector2 m_SolidUV;
		uint32_t m_Transform = UIDrawCommand::kScreenSpace;
	};
}
