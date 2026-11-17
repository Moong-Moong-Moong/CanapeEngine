#pragma once

#include "Scene/Component/Component.h"
#include "UI/UIElement.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace Canape
{
	//***** 화면 공간 UI 트리 하나를 씬에 붙이는 컴포넌트
	//*** 루트는 타깃 전체를 덮고 입력을 받지 않는다. 스크립트가 루트 아래에 요소를 만든다.
	//*** 1단계에서는 코드로만 구성하므로 씬 파일에 저장되지 않는다.
	class UICanvasComponent final : public Component
	{
		RTTR_ENABLE(Component)

	public:
		UICanvasComponent();
		explicit UICanvasComponent(const Vector2& referenceResolution);
		~UICanvasComponent() override;

		UIElement& GetRoot() { return *m_Root; }
		const UIElement& GetRoot() const { return *m_Root; }

		template<typename T>
		T* Find(std::string_view name) { return m_Root->Find<T>(name); }

		//*** 이 해상도에서 위치·크기가 1:1 픽셀이 된다. 0이면 스케일 1 고정.
		const Vector2& GetReferenceResolution() const { return m_ReferenceResolution; }
		void SetReferenceResolution(const Vector2& resolution) { m_ReferenceResolution = resolution; }

		//*** 0 = 너비 기준, 1 = 높이 기준(기본)
		float GetMatchWidthOrHeight() const { return m_MatchWidthOrHeight; }
		void SetMatchWidthOrHeight(float match) { m_MatchWidthOrHeight = match; }

		//*** 큰 값이 위에 그려지고 입력도 먼저 받는다.
		int32_t GetSortOrder() const { return m_SortOrder; }
		void SetSortOrder(int32_t order) { m_SortOrder = order; }

		float GetScale() const { return m_Scale; }

		//***** UISystem 전용
		void Layout(const Vector2& targetSize);

	private:
		std::unique_ptr<UIElement> m_Root;
		Vector2 m_ReferenceResolution{ 1920.0f, 1080.0f };
		float m_MatchWidthOrHeight = 1.0f;
		int32_t m_SortOrder = 0;
		float m_Scale = 1.0f;
	};
}
