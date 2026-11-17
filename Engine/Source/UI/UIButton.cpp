#include "UI/UIButton.h"

namespace Canape
{
	UIButton::UIButton(std::string name)
		: UIImage(std::move(name))
	{
		SetHitTestable(true);
	}

	UIColor UIButton::GetTint() const
	{
		if (!m_Interactable)
		{
			return m_StateColors.Disabled;
		}
		if (IsPressed() && IsHovered())
		{
			return m_StateColors.Pressed;
		}
		if (IsHovered() || IsPressed())
		{
			return m_StateColors.Hovered;
		}
		return m_StateColors.Normal;
	}

	void UIButton::OnPointerUp(bool inside)
	{
		if (!inside || !m_Interactable || !OnClicked)
		{
			return;
		}
		// 콜백이 OnClicked를 바꿔도 안전하도록 복사해서 호출
		const std::function<void()> callback = OnClicked;
		callback();
	}
}
