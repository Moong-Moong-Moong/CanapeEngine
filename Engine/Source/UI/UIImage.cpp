#include "UI/UIImage.h"
#include "UI/UIPainter.h"

#include <algorithm>

namespace Canape
{
	void UIImage::SetFillAmount(float amount)
	{
		m_FillAmount = std::clamp(amount, 0.0f, 1.0f);
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

		painter.AddQuad(rect, uv, m_Color * GetTint(), m_Texture);
	}
}
