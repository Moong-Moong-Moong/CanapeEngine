#pragma once

#include "UI/UITypes.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace Canape
{
	class UIPainter;

	//***** UI 트리의 기본 요소
	//*** 레이아웃은 Unity RectTransform 방식: 부모 사각형 안의 앵커(0~1) + 피벗 + 위치 + 크기.
	//*** 위치·크기 단위는 캔버스 기준 해상도의 픽셀이며 캔버스 스케일이 곱해진다.
	//*** 자식은 부모가 소유한다. 삭제(RemoveFromParent)는 프레임 경계에서 처리되므로
	//*** 이벤트 콜백 안에서 지워도 안전하다.
	class UIElement
	{
	public:
		explicit UIElement(std::string name = {});
		virtual ~UIElement();

		UIElement(const UIElement&) = delete;
		UIElement& operator=(const UIElement&) = delete;

		uint64_t GetId() const { return m_Id; }
		const std::string& GetName() const { return m_Name; }
		void SetName(std::string name) { m_Name = std::move(name); }

		//***** 트리
		UIElement* GetParent() const { return m_Parent; }
		std::span<const std::unique_ptr<UIElement>> GetChildren() const { return m_Children; }

		template<typename T, typename... Args>
		T* AddChild(std::string name, Args&&... args);
		UIElement* AddChild(std::unique_ptr<UIElement> child);

		//*** 다음 프레임 경계에서 부모에게서 제거된다. 그 전까지 그려지지도, 입력을 받지도 않는다.
		void RemoveFromParent();
		bool IsPendingRemove() const { return m_PendingRemove; }

		//*** 이름으로 자신과 자손을 깊이 우선 탐색
		UIElement* FindByName(std::string_view name);
		template<typename T>
		T* Find(std::string_view name) { return dynamic_cast<T*>(FindByName(name)); }
		UIElement* FindById(uint64_t id);

		//***** 레이아웃
		const Vector2& GetAnchorMin() const { return m_AnchorMin; }
		const Vector2& GetAnchorMax() const { return m_AnchorMax; }
		void SetAnchors(const Vector2& anchorMin, const Vector2& anchorMax) { m_AnchorMin = anchorMin; m_AnchorMax = anchorMax; }
		void SetAnchor(const Vector2& anchor) { SetAnchors(anchor, anchor); }

		const Vector2& GetPivot() const { return m_Pivot; }
		void SetPivot(const Vector2& pivot) { m_Pivot = pivot; }

		//*** 앵커 기준점에서 피벗까지의 거리
		const Vector2& GetPosition() const { return m_Position; }
		void SetPosition(const Vector2& position) { m_Position = position; }

		//*** 앵커가 벌어져 있으면(stretch) 앵커 폭에 더해지는 값. 음수면 안쪽으로 줄어든다.
		const Vector2& GetSize() const { return m_Size; }
		void SetSize(const Vector2& size) { m_Size = size; }

		//*** 켜면 크기를 앵커·Size 대신 내용(GetPreferredSize)으로 정한다. 앵커는 최솟값을 한 점으로 쓴다.
		bool IsFitContent() const { return m_FitContent; }
		void SetFitContent(bool fit) { m_FitContent = fit; }

		//*** 내용이 원하는 크기(타깃 픽셀). 기본은 |Size| × 스케일, 텍스트·박스는 내용으로 잰다.
		virtual Vector2 GetPreferredSize(float scale) const;
		//*** 폭이 정해졌을 때 원하는 높이(줄바꿈 텍스트는 폭에 따라 줄 수가 바뀐다). 기본은 GetPreferredSize의 높이.
		virtual float GetPreferredHeight(float width, float scale) const { (void)width; return GetPreferredSize(scale).Y; }

		//*** 마지막 레이아웃 결과(타깃 픽셀, 정수로 맞춤)
		const UIRect& GetLayoutRect() const { return m_LayoutRect; }
		float GetLayoutScale() const { return m_LayoutScale; }

		//*** 켜면 자식을 이 요소의 사각형 밖으로 그리지 않고, 밖에서는 자식이 입력도 받지 않는다.
		bool GetClipChildren() const { return m_ClipChildren; }
		void SetClipChildren(bool clip) { m_ClipChildren = clip; }

		//***** 표시·입력
		bool IsVisible() const { return m_Visible; }
		void SetVisible(bool visible) { m_Visible = visible; }

		//*** 켜져 있으면 포인터를 받고, 그 아래의 게임 입력을 막는다.
		bool IsHitTestable() const { return m_HitTestable; }
		void SetHitTestable(bool hitTestable) { m_HitTestable = hitTestable; }

		bool IsHovered() const { return m_Hovered; }
		bool IsPressed() const { return m_Pressed; }

		//***** UI 시스템 내부용
		//*** 부모 사각형 안에서 앵커로 자기 사각형을 정하고 자식을 배치한다.
		void Layout(const UIRect& parentRect, float scale);
		//*** 컨테이너(박스)가 정해 준 사각형을 그대로 쓰고 자식을 배치한다.
		void ArrangeAt(const UIRect& rect, float scale);
		void Paint(UIPainter& painter) const;
		//*** 맨 위(나중에 그려진) 요소부터 검사
		UIElement* HitTest(const Vector2& point);
		void FlushRemovals();

	protected:
		//*** 자기 사각형이 정해진 뒤 자식을 배치한다. 기본은 각 자식이 자기 앵커로 Layout.
		virtual void LayoutChildren(float scale);
		//*** 자기 사각형이 정해진 직후(자식 배치 전) 불린다. 텍스트가 줄바꿈 폭을 여기서 안다.
		virtual void OnLayout() {}

		virtual void OnPaint(UIPainter& painter) const { (void)painter; }

		virtual void OnPointerEnter() {}
		virtual void OnPointerLeave() {}
		virtual void OnPointerDown() {}
		//*** inside: 뗀 위치가 이 요소 위인지(클릭 판정용)
		virtual void OnPointerUp(bool inside) { (void)inside; }

	private:
		friend class UIPointerRouter;

		static uint64_t AllocateId();
		void SetLayoutRect(float left, float top, float right, float bottom, float scale);

		const uint64_t m_Id;
		std::string m_Name;
		UIElement* m_Parent = nullptr;
		std::vector<std::unique_ptr<UIElement>> m_Children;

		Vector2 m_AnchorMin{ 0.0f, 0.0f };
		Vector2 m_AnchorMax{ 0.0f, 0.0f };
		Vector2 m_Pivot{ 0.0f, 0.0f };
		Vector2 m_Position{ 0.0f, 0.0f };
		Vector2 m_Size{ 100.0f, 100.0f };
		UIRect m_LayoutRect;
		float m_LayoutScale = 1.0f;

		bool m_FitContent = false;
		bool m_ClipChildren = false;
		bool m_Visible = true;
		bool m_HitTestable = false;
		bool m_PendingRemove = false;
		bool m_Hovered = false;
		bool m_Pressed = false;
	};

	template<typename T, typename... Args>
	T* UIElement::AddChild(std::string name, Args&&... args)
	{
		static_assert(std::is_base_of_v<UIElement, T>, "T must derive from UIElement");

		auto child = std::make_unique<T>(std::forward<Args>(args)...);
		child->SetName(std::move(name));
		return static_cast<T*>(AddChild(std::move(child)));
	}
}
