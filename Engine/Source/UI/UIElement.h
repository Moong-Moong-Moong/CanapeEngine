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

		//*** 마지막 레이아웃 결과(타깃 픽셀, 정수로 맞춤)
		const UIRect& GetLayoutRect() const { return m_LayoutRect; }

		//***** 표시·입력
		bool IsVisible() const { return m_Visible; }
		void SetVisible(bool visible) { m_Visible = visible; }

		//*** 켜져 있으면 포인터를 받고, 그 아래의 게임 입력을 막는다.
		bool IsHitTestable() const { return m_HitTestable; }
		void SetHitTestable(bool hitTestable) { m_HitTestable = hitTestable; }

		bool IsHovered() const { return m_Hovered; }
		bool IsPressed() const { return m_Pressed; }

		//***** UI 시스템 내부용
		void Layout(const UIRect& parentRect, float scale);
		void Paint(UIPainter& painter) const;
		//*** 맨 위(나중에 그려진) 요소부터 검사
		UIElement* HitTest(const Vector2& point);
		void FlushRemovals();

	protected:
		virtual void OnPaint(UIPainter& painter) const { (void)painter; }

		virtual void OnPointerEnter() {}
		virtual void OnPointerLeave() {}
		virtual void OnPointerDown() {}
		//*** inside: 뗀 위치가 이 요소 위인지(클릭 판정용)
		virtual void OnPointerUp(bool inside) { (void)inside; }

	private:
		friend class UIPointerRouter;

		static uint64_t AllocateId();

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
