#pragma once

#include "DX11Renderer/Data/UIDrawList.h"
#include "UI/UIPainter.h"
#include "UI/UIPointerRouter.h"

#include <cstdint>
#include <vector>

namespace Canape
{
	class InputManager;
	class Scene;
	class UICanvasComponent;
	class UIElement;

	//***** 활성 씬의 UICanvasComponent들을 모아 입력을 나누고 그릴 목록을 만든다.
	//*** 프레임 순서: InputManager::Update → ProcessInput → 씬 업데이트(스크립트) → BuildDrawList → Render
	class UISystem
	{
	public:
		//*** 창 클라이언트 좌표 중 UI 타깃이 표시되는 영역. 에디터 씬 뷰가 매 프레임 지정한다.
		//*** active가 false면 포인터가 UI에 닿지 않는다(씬 뷰에 마우스가 없을 때 등).
		void SetPointerViewport(const UIRect& clientRect, bool active);
		//*** 게임: UI 타깃 = 창 전체
		void ResetPointerViewport();

		void ProcessInput(Scene* scene, InputManager& input);
		void BuildDrawList(Scene* scene, uint32_t targetWidth, uint32_t targetHeight, const UITextureResolver& resolver);

		const UIDrawList& GetDrawList() const { return m_DrawList; }
		bool IsPointerOverUI() const { return m_Router.IsCapturingPointer(); }

	private:
		//*** 정렬 순서대로(나중이 위) 실행 가능한 캔버스
		std::vector<UICanvasComponent*> CollectCanvases(Scene* scene) const;
		UIPointerState MapPointer(const InputManager& input) const;

		UIPointerRouter m_Router;
		UIDrawList m_DrawList;
		std::vector<UIElement*> m_Roots;
		Scene* m_LastScene = nullptr;
		Vector2 m_TargetSize;
		UIRect m_PointerViewport;
		bool m_UsePointerViewport = false;
		bool m_PointerViewportActive = true;
	};
}
