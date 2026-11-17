#pragma once

#include "UI/UITypes.h"

#include <cstdint>
#include <span>

namespace Canape
{
	class UIElement;

	//*** 한 프레임의 포인터 상태(UI 타깃 픽셀 좌표)
	struct UIPointerState
	{
		Vector2 Position;
		bool Inside = false;      // 좌표가 UI 타깃 안에 있고 UI가 포인터를 받아도 되는지
		bool PrimaryDown = false; // 왼쪽 버튼(위치와 상관없이 실제 상태)
	};

	//***** 히트 테스트와 hover/press/click 상태 기계
	//*** 요소 포인터 대신 ID를 기억하므로 요소나 캔버스가 사라져도 안전하다.
	class UIPointerRouter
	{
	public:
		//*** roots: 그리는 순서(나중이 위)
		void Update(std::span<UIElement* const> roots, const UIPointerState& pointer);

		//*** 이 프레임에 UI가 포인터를 쓰는지: 상호작용 요소 위에 있거나, 누른 상태를 붙잡고 있음
		bool IsCapturingPointer() const { return m_Capturing; }

		uint64_t GetHoveredId() const { return m_HoveredId; }
		uint64_t GetPressedId() const { return m_PressedId; }

		//*** 씬 전환 등으로 트리가 통째로 바뀌었을 때. 콜백 없이 상태만 지운다.
		void Reset();

	private:
		static UIElement* Find(std::span<UIElement* const> roots, uint64_t id);

		uint64_t m_HoveredId = 0;
		uint64_t m_PressedId = 0;
		bool m_WasDown = false;
		bool m_Capturing = false;
	};
}
