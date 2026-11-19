#include "UI/UIImage.h"
#include "UI/UIPainter.h"

#include <algorithm>
#include <array>

namespace Canape
{
	void UIImage::SetFillAmount(float amount)
	{
		m_FillAmount = std::clamp(amount, 0.0f, 1.0f);
	}

	void UIImage::Draw(UIPainter& painter, const UIRect& rect, const UIRect& uv, UIColor color) const
	{
		if (m_TextureResource.IsValid())
		{
			painter.AddQuadResource(rect, uv, color, m_TextureResource);
		}
		else
		{
			painter.AddQuad(rect, uv, color, m_Texture);
		}
	}

	void UIImage::DrawNineSlice(UIPainter& painter, const UIRect& rect, UIColor color) const
	{
		const float scale = GetLayoutScale();
		float left = m_NineSlice.Size.Left * scale;
		float right = m_NineSlice.Size.Right * scale;
		float top = m_NineSlice.Size.Top * scale;
		float bottom = m_NineSlice.Size.Bottom * scale;
		if (left + right > rect.Width && left + right > 0.0f)
		{
			const float shrink = rect.Width / (left + right);
			left *= shrink;
			right *= shrink;
		}
		if (top + bottom > rect.Height && top + bottom > 0.0f)
		{
			const float shrink = rect.Height / (top + bottom);
			top *= shrink;
			bottom *= shrink;
		}

		const UIRect& uv = m_UVRect;
		const std::array<float, 4> xs{ rect.X, rect.X + left, rect.Right() - right, rect.Right() };
		const std::array<float, 4> ys{ rect.Y, rect.Y + top, rect.Bottom() - bottom, rect.Bottom() };
		const std::array<float, 4> us{ uv.X, uv.X + m_NineSlice.UV.Left * uv.Width,
			uv.Right() - m_NineSlice.UV.Right * uv.Width, uv.Right() };
		const std::array<float, 4> vs{ uv.Y, uv.Y + m_NineSlice.UV.Top * uv.Height,
			uv.Bottom() - m_NineSlice.UV.Bottom * uv.Height, uv.Bottom() };
		for (size_t row = 0; row < 3; ++row)
		{
			for (size_t column = 0; column < 3; ++column)
			{
				const UIRect cell{ xs.at(column), ys.at(row), xs.at(column + 1) - xs.at(column), ys.at(row + 1) - ys.at(row) };
				const UIRect cellUV{ us.at(column), vs.at(row), us.at(column + 1) - us.at(column), vs.at(row + 1) - vs.at(row) };
				Draw(painter, cell, cellUV, color); // 크기 0인 칸은 Painter가 건너뛴다.
			}
		}
	}

	void UIImage::OnPaint(UIPainter& painter) const
	{
		if (m_FillAmount <= 0.0f)
		{
			return;
		}

		UIRect rect = GetLayoutRect();
		UIRect uv = m_UVRect;
		const float fill = m_FillAmount;
		switch (m_FillDirection)
		{
		case UIFillDirection::LeftToRight:
			rect.Width *= fill;
			uv.Width *= fill;
			break;
		case UIFillDirection::RightToLeft:
			rect.X += rect.Width * (1.0f - fill);
			rect.Width *= fill;
			uv.X += uv.Width * (1.0f - fill);
			uv.Width *= fill;
			break;
		case UIFillDirection::TopToBottom:
			rect.Height *= fill;
			uv.Height *= fill;
			break;
		case UIFillDirection::BottomToTop:
			rect.Y += rect.Height * (1.0f - fill);
			rect.Height *= fill;
			uv.Y += uv.Height * (1.0f - fill);
			uv.Height *= fill;
			break;
		}

		const UIColor color = m_Color * GetTint();
		if (!m_HasNineSlice)
		{
			Draw(painter, rect, uv, color);
			return;
		}
		// 9-slice 게이지: 모서리 모양을 지키도록 전체를 그리고 채운 부분만 잘라 낸다.
		const bool partial = fill < 1.0f;
		if (partial)
		{
			painter.PushClip(rect);
		}
		DrawNineSlice(painter, GetLayoutRect(), color);
		if (partial)
		{
			painter.PopClip();
		}
	}
}
