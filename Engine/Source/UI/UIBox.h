#pragma once

#include "UI/UIElement.h"

namespace Canape
{
	enum class UIBoxDirection : uint8_t
	{
		Horizontal,
		Vertical
	};

	//*** 진행 방향과 수직인 축에서 자식을 어디에 둘지
	enum class UIBoxAlign : uint8_t
	{
		Start,
		Center,
		End,
		Stretch // 박스 안쪽 폭(또는 높이)을 꽉 채움
	};

	//***** 자식을 한 줄로 차례대로 놓는 컨테이너
	//*** 자식의 앵커·위치는 무시하고, 진행 방향 크기는 자식의 GetPreferredSize로 정한다.
	//*** 숨긴 자식은 자리를 차지하지 않는다. 박스 자체 크기는 앵커·Size, 또는 SetFitContent(true)로 내용에 맞춘다.
	class UIBox : public UIElement
	{
	public:
		UIBox(UIBoxDirection direction, std::string name);

		UIBoxDirection GetDirection() const { return m_Direction; }

		//*** 자식 사이 간격(캔버스 기준 픽셀)
		float GetSpacing() const { return m_Spacing; }
		void SetSpacing(float spacing) { m_Spacing = spacing; }

		//*** 박스 안쪽 여백(캔버스 기준 픽셀)
		const UIEdges& GetPadding() const { return m_Padding; }
		void SetPadding(const UIEdges& padding) { m_Padding = padding; }

		UIBoxAlign GetChildAlign() const { return m_ChildAlign; }
		void SetChildAlign(UIBoxAlign align) { m_ChildAlign = align; }

		Vector2 GetPreferredSize(float scale) const override;

	protected:
		void LayoutChildren(float scale) override;

	private:
		UIBoxDirection m_Direction;
		float m_Spacing = 0.0f;
		UIEdges m_Padding;
		UIBoxAlign m_ChildAlign = UIBoxAlign::Start;
	};

	class UIHorizontalBox : public UIBox
	{
	public:
		explicit UIHorizontalBox(std::string name = {}) : UIBox(UIBoxDirection::Horizontal, std::move(name)) {}
	};

	class UIVerticalBox : public UIBox
	{
	public:
		explicit UIVerticalBox(std::string name = {}) : UIBox(UIBoxDirection::Vertical, std::move(name)) {}
	};
}
