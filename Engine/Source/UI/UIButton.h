#pragma once

#include "UI/UIImage.h"

#include <functional>

namespace Canape
{
	//*** 상태별로 이미지 색에 곱해지는 틴트
	struct UIButtonColors
	{
		UIColor Normal{ 255, 255, 255, 255 };
		UIColor Hovered{ 235, 235, 235, 255 };
		UIColor Pressed{ 190, 190, 190, 255 };
		UIColor Disabled{ 200, 200, 200, 128 };
	};

	//***** 누른 뒤 같은 버튼 위에서 떼면 OnClicked가 불린다.
	//*** 비활성 상태에서도 포인터는 받아서 아래의 게임 입력을 막는다.
	class UIButton : public UIImage
	{
	public:
		explicit UIButton(std::string name = {});

		std::function<void()> OnClicked;

		bool IsInteractable() const { return m_Interactable; }
		void SetInteractable(bool interactable) { m_Interactable = interactable; }

		const UIButtonColors& GetStateColors() const { return m_StateColors; }
		void SetStateColors(const UIButtonColors& colors) { m_StateColors = colors; }

	protected:
		UIColor GetTint() const override;
		void OnPointerUp(bool inside) override;

	private:
		UIButtonColors m_StateColors;
		bool m_Interactable = true;
	};
}
