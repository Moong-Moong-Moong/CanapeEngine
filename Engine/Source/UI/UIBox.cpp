#include "UI/UIBox.h"

#include <algorithm>

namespace Canape
{
	namespace
	{
		bool TakesSpace(const UIElement& child) { return child.IsVisible() && !child.IsPendingRemove(); }
	}

	UIBox::UIBox(UIBoxDirection direction, std::string name)
		: UIElement(std::move(name))
		, m_Direction(direction)
	{
	}

	Vector2 UIBox::GetPreferredSize(float scale) const
	{
		const bool horizontal = m_Direction == UIBoxDirection::Horizontal;
		float main = 0.0f;
		float cross = 0.0f;
		int count = 0;
		for (const auto& child : GetChildren())
		{
			if (!TakesSpace(*child))
			{
				continue;
			}
			const Vector2 size = child->GetPreferredSize(scale);
			main += horizontal ? size.X : size.Y;
			cross = std::max(cross, horizontal ? size.Y : size.X);
			++count;
		}
		if (count > 1)
		{
			main += m_Spacing * scale * static_cast<float>(count - 1);
		}
		const float padX = m_Padding.Horizontal() * scale;
		const float padY = m_Padding.Vertical() * scale;
		return horizontal ? Vector2{ main + padX, cross + padY } : Vector2{ cross + padX, main + padY };
	}

	void UIBox::LayoutChildren(float scale)
	{
		const bool horizontal = m_Direction == UIBoxDirection::Horizontal;
		const UIRect& rect = GetLayoutRect();
		const UIRect content{ rect.X + m_Padding.Left * scale, rect.Y + m_Padding.Top * scale,
			std::max(rect.Width - m_Padding.Horizontal() * scale, 0.0f),
			std::max(rect.Height - m_Padding.Vertical() * scale, 0.0f) };
		const float crossSpace = horizontal ? content.Height : content.Width;

		float cursor = horizontal ? content.X : content.Y;
		for (const auto& child : GetChildren())
		{
			if (!TakesSpace(*child))
			{
				child->ArrangeAt({ content.X, content.Y, 0.0f, 0.0f }, scale);
				continue;
			}
			const Vector2 preferred = child->GetPreferredSize(scale);
			float main = horizontal ? preferred.X : preferred.Y;
			float cross = horizontal ? preferred.Y : preferred.X;
			float offset = 0.0f;
			switch (m_ChildAlign)
			{
			case UIBoxAlign::Center: offset = (crossSpace - cross) * 0.5f; break;
			case UIBoxAlign::End: offset = crossSpace - cross; break;
			case UIBoxAlign::Stretch: cross = crossSpace; break;
			default: break;
			}
			if (!horizontal)
			{
				// 세로 박스: 폭이 정해진 뒤 높이를 묻는다(줄바꿈 텍스트).
				main = child->GetPreferredHeight(cross, scale);
			}
			const UIRect slot = horizontal
				? UIRect{ cursor, content.Y + offset, main, cross }
				: UIRect{ content.X + offset, cursor, cross, main };
			child->ArrangeAt(slot, scale);
			cursor += main + m_Spacing * scale;
		}
	}
}
