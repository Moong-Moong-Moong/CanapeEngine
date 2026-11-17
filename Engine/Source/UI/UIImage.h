#pragma once

#include "Asset/AssetId.h"
#include "UI/UIElement.h"

namespace Canape
{
	enum class UIFillDirection : uint8_t
	{
		LeftToRight,
		RightToLeft,
		TopToBottom,
		BottomToTop
	};

	//***** 단색 또는 텍스처 사각형. 채움 비율로 체력바 같은 게이지를 만든다.
	//*** UI 텍스처는 RGBA8_UNORM으로 올리는 것을 권장한다. sRGB 텍스처도 렌더러가 보정해서 그린다.
	class UIImage : public UIElement
	{
	public:
		using UIElement::UIElement;

		UIColor GetColor() const { return m_Color; }
		void SetColor(UIColor color) { m_Color = color; }

		TextureAssetId GetTexture() const { return m_Texture; }
		void SetTexture(TextureAssetId texture) { m_Texture = texture; }

		//*** 텍스처 안에서 쓸 영역(0~1). 아틀라스의 일부를 쓸 때 지정한다.
		const UIRect& GetUVRect() const { return m_UVRect; }
		void SetUVRect(const UIRect& uv) { m_UVRect = uv; }

		//*** 0~1. 1이면 전체, 0이면 그리지 않는다.
		float GetFillAmount() const { return m_FillAmount; }
		void SetFillAmount(float amount);

		UIFillDirection GetFillDirection() const { return m_FillDirection; }
		void SetFillDirection(UIFillDirection direction) { m_FillDirection = direction; }

	protected:
		void OnPaint(UIPainter& painter) const override;

		//*** 파생 클래스(버튼 상태 등)가 색에 곱할 틴트
		virtual UIColor GetTint() const { return UIColor::White(); }

	private:
		UIColor m_Color;
		TextureAssetId m_Texture;
		UIRect m_UVRect{ 0.0f, 0.0f, 1.0f, 1.0f };
		float m_FillAmount = 1.0f;
		UIFillDirection m_FillDirection = UIFillDirection::LeftToRight;
	};
}
