#include "UI/UIPointerRouter.h"
#include "UI/UIElement.h"

#include <ranges>

namespace Canape
{
	UIElement* UIPointerRouter::Find(std::span<UIElement* const> roots, uint64_t id)
	{
		if (id == 0)
		{
			return nullptr;
		}
		for (UIElement* root : roots)
		{
			if (UIElement* found = root ? root->FindById(id) : nullptr)
			{
				return found;
			}
		}
		return nullptr;
	}

	void UIPointerRouter::Update(std::span<UIElement* const> roots, const UIPointerState& pointer)
	{
		const bool heldAtStart = m_PressedId != 0;

		UIElement* hit = nullptr;
		if (pointer.Inside)
		{
			for (UIElement* root : roots | std::views::reverse)
			{
				if (root && (hit = root->HitTest(pointer.Position)) != nullptr)
				{
					break;
				}
			}
		}
		const uint64_t hitId = hit ? hit->GetId() : 0;

		//***** hover
		if (hitId != m_HoveredId)
		{
			if (UIElement* previous = Find(roots, m_HoveredId))
			{
				previous->m_Hovered = false;
				previous->OnPointerLeave();
			}
			m_HoveredId = hitId;
			if (hit)
			{
				hit->m_Hovered = true;
				hit->OnPointerEnter();
			}
		}

		//***** press / release
		const bool pressedEdge = pointer.PrimaryDown && !m_WasDown;
		const bool releasedEdge = !pointer.PrimaryDown && m_WasDown;
		m_WasDown = pointer.PrimaryDown;

		if (pressedEdge && hit)
		{
			m_PressedId = hitId;
			hit->m_Pressed = true;
			hit->OnPointerDown();
		}
		if (releasedEdge && m_PressedId != 0)
		{
			UIElement* pressed = Find(roots, m_PressedId);
			m_PressedId = 0;
			if (pressed)
			{
				pressed->m_Pressed = false;
				// 클릭 콜백이 트리를 바꿀 수 있으므로 마지막에 호출한다.
				pressed->OnPointerUp(pressed == hit);
			}
		}

		// 뗀 프레임까지 붙잡아서, UI에서 시작한 클릭의 release가 게임으로 새지 않게 한다.
		m_Capturing = hit != nullptr || heldAtStart || m_PressedId != 0;
	}

	void UIPointerRouter::Reset()
	{
		m_HoveredId = 0;
		m_PressedId = 0;
		m_Capturing = false;
		// m_WasDown은 유지: 누른 채로 씬이 바뀌어도 새 트리에 가짜 press가 생기지 않는다.
	}
}
