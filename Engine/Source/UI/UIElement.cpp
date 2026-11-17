#include "UI/UIElement.h"
#include "UI/UIPainter.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace Canape
{
	UIElement::UIElement(std::string name)
		: m_Id(AllocateId())
		, m_Name(std::move(name))
	{
	}

	UIElement::~UIElement() = default;

	uint64_t UIElement::AllocateId()
	{
		static uint64_t nextId = 1; // 단일 스레드, 재사용하지 않음. 0은 "없음".
		return nextId++;
	}

	UIElement* UIElement::AddChild(std::unique_ptr<UIElement> child)
	{
		if (!child || child->m_Parent)
		{
			return nullptr;
		}
		child->m_Parent = this;
		m_Children.push_back(std::move(child));
		return m_Children.back().get();
	}

	void UIElement::RemoveFromParent()
	{
		if (m_Parent)
		{
			m_PendingRemove = true;
		}
	}

	UIElement* UIElement::FindByName(std::string_view name)
	{
		if (m_PendingRemove)
		{
			return nullptr;
		}
		if (m_Name == name)
		{
			return this;
		}
		for (const auto& child : m_Children)
		{
			if (UIElement* found = child->FindByName(name))
			{
				return found;
			}
		}
		return nullptr;
	}

	UIElement* UIElement::FindById(uint64_t id)
	{
		if (id == 0 || m_PendingRemove)
		{
			return nullptr;
		}
		if (m_Id == id)
		{
			return this;
		}
		for (const auto& child : m_Children)
		{
			if (UIElement* found = child->FindById(id))
			{
				return found;
			}
		}
		return nullptr;
	}

	void UIElement::Layout(const UIRect& parentRect, float scale)
	{
		const float anchorLeft = parentRect.X + parentRect.Width * m_AnchorMin.X;
		const float anchorRight = parentRect.X + parentRect.Width * m_AnchorMax.X;
		const float anchorTop = parentRect.Y + parentRect.Height * m_AnchorMin.Y;
		const float anchorBottom = parentRect.Y + parentRect.Height * m_AnchorMax.Y;

		const float width = (anchorRight - anchorLeft) + m_Size.X * scale;
		const float height = (anchorBottom - anchorTop) + m_Size.Y * scale;
		const float pivotX = anchorLeft + (anchorRight - anchorLeft) * m_Pivot.X + m_Position.X * scale;
		const float pivotY = anchorTop + (anchorBottom - anchorTop) * m_Pivot.Y + m_Position.Y * scale;

		// 모서리를 정수 픽셀에 맞춰 경계가 번지지 않게 한다.
		const float left = std::round(pivotX - width * m_Pivot.X);
		const float top = std::round(pivotY - height * m_Pivot.Y);
		const float right = std::round(pivotX - width * m_Pivot.X + width);
		const float bottom = std::round(pivotY - height * m_Pivot.Y + height);
		m_LayoutRect = { left, top, std::max(right - left, 0.0f), std::max(bottom - top, 0.0f) };

		for (const auto& child : m_Children)
		{
			child->Layout(m_LayoutRect, scale);
		}
	}

	void UIElement::Paint(UIPainter& painter) const
	{
		if (!m_Visible || m_PendingRemove)
		{
			return;
		}
		OnPaint(painter);
		for (const auto& child : m_Children)
		{
			child->Paint(painter);
		}
	}

	UIElement* UIElement::HitTest(const Vector2& point)
	{
		if (!m_Visible || m_PendingRemove)
		{
			return nullptr;
		}
		// 나중에 그려진 자식이 위에 있다.
		for (const auto& child : m_Children | std::views::reverse)
		{
			if (UIElement* hit = child->HitTest(point))
			{
				return hit;
			}
		}
		return m_HitTestable && m_LayoutRect.Contains(point) ? this : nullptr;
	}

	void UIElement::FlushRemovals()
	{
		std::erase_if(m_Children, [](const std::unique_ptr<UIElement>& child) { return child->m_PendingRemove; });
		for (const auto& child : m_Children)
		{
			child->FlushRemovals();
		}
	}
}
