#pragma once

#include "DX11Renderer/Data/UIDrawList.h"
#include "UI/Text/UIGlyphAtlas.h"
#include "UI/UIPainter.h"
#include "UI/UIPointerRouter.h"

#include <cstdint>
#include <memory>
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
		//*** device: 글리프 아틀라스 텍스처를 만들고 새로 구운 글자를 올리는 통로
		void BuildDrawList(Scene* scene, uint32_t targetWidth, uint32_t targetHeight, const UITextureResolver& resolver,
			const UITextureDevice& device);

		const UIDrawList& GetDrawList() const { return m_DrawList; }
		bool IsPointerOverUI() const { return m_Router.IsCapturingPointer(); }
		UIGlyphAtlas& GetGlyphAtlas() { return m_Atlas; }

		//*** 처음 그릴 때 이 폰트를 기본 폰트로 읽는다(에셋 루트 기준 경로).
		static constexpr const char* kDefaultFontPath = "Content/Fonts/Pretendard-Regular.ttf";

	private:
		//*** 정렬 순서대로(나중이 위) 실행 가능한 캔버스
		std::vector<UICanvasComponent*> CollectCanvases(Scene* scene) const;
		UIPointerState MapPointer(const InputManager& input) const;

		void EnsureDefaultFont();

		UIPointerRouter m_Router;
		UIDrawList m_DrawList;
		UIGlyphAtlas m_Atlas;
		bool m_DefaultFontTried = false;
		std::vector<UIElement*> m_Roots;
		Scene* m_LastScene = nullptr;
		Vector2 m_TargetSize;
		UIRect m_PointerViewport;
		bool m_UsePointerViewport = false;
		bool m_PointerViewportActive = true;
	};
}
