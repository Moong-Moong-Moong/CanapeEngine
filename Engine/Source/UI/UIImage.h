#pragma once

#include "Asset/AssetId.h"
#include "DX11Renderer/Resource/DX11ResourceId.h"
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

	//***** 9-slice: 모서리는 늘리지 않고 가운데만 늘린다.
	//*** UV = 텍스처 안 테두리 두께(0~1, UV 영역 기준), Size = 화면 테두리 두께(캔버스 기준 픽셀).
	//*** 사각형이 테두리 합보다 작으면 테두리를 비율대로 줄인다.
	struct UINineSlice
	{
		UIEdges UV;
		UIEdges Size;

		//*** 텍스처 픽셀 단위 테두리를 그대로 화면 테두리로 쓴다(기준 해상도에서 1:1).
		static UINineSlice FromPixels(const UIEdges& borderPixels, float textureWidth, float textureHeight)
		{
			return { { borderPixels.Left / textureWidth, borderPixels.Top / textureHeight,
				borderPixels.Right / textureWidth, borderPixels.Bottom / textureHeight }, borderPixels };
		}
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

		//*** 코드로 만든 GPU 텍스처를 바로 쓴다(지정하면 SetTexture보다 우선). 수명은 만든 쪽이 관리.
		TextureResourceId GetTextureResource() const { return m_TextureResource; }
		void SetTextureResource(TextureResourceId texture) { m_TextureResource = texture; }

		bool HasNineSlice() const { return m_HasNineSlice; }
		const UINineSlice& GetNineSlice() const { return m_NineSlice; }
		void SetNineSlice(const UINineSlice& slice) { m_NineSlice = slice; m_HasNineSlice = true; }
		void ClearNineSlice() { m_HasNineSlice = false; }

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
		void Draw(UIPainter& painter, const UIRect& rect, const UIRect& uv, UIColor color) const;
		void DrawNineSlice(UIPainter& painter, const UIRect& rect, UIColor color) const;

		UIColor m_Color;
		TextureAssetId m_Texture;
		TextureResourceId m_TextureResource;
		UINineSlice m_NineSlice;
		bool m_HasNineSlice = false;
		UIRect m_UVRect{ 0.0f, 0.0f, 1.0f, 1.0f };
		float m_FillAmount = 1.0f;
		UIFillDirection m_FillDirection = UIFillDirection::LeftToRight;
	};
}
