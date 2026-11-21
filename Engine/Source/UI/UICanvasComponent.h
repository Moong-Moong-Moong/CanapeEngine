#pragma once

#include "Math/Matrix4.h"
#include "Scene/Component/Component.h"
#include "UI/UIElement.h"

#include <cstdint>
#include <memory>
#include <string_view>

namespace Canape
{
	enum class UICanvasMode : uint8_t
	{
		ScreenSpace, // 화면 HUD: 렌더 타깃 픽셀, 항상 맨 위
		WorldSpace   // 월드 패널: 붙은 GameObject의 Transform을 따르고, 씬 물체에 가려질 수 있다
	};

	//***** UI 트리 하나를 씬에 붙이는 컴포넌트
	//*** 루트는 캔버스 전체를 덮고 입력을 받지 않는다. 스크립트가 루트 아래에 요소를 만든다.
	//*** 코드로만 구성하므로 씬 파일에 저장되지 않는다.
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

		UICanvasMode GetMode() const { return m_Mode; }
		void SetMode(UICanvasMode mode) { m_Mode = mode; }

		//*** 큰 값이 위에 그려지고 입력도 먼저 받는다(같은 모드끼리).
		int32_t GetSortOrder() const { return m_SortOrder; }
		void SetSortOrder(int32_t order) { m_SortOrder = order; }

		//***** 화면 공간
		//*** 이 해상도에서 위치·크기가 1:1 픽셀이 된다. 0이면 스케일 1 고정.
		const Vector2& GetReferenceResolution() const { return m_ReferenceResolution; }
		void SetReferenceResolution(const Vector2& resolution) { m_ReferenceResolution = resolution; }

		//*** 0 = 너비 기준, 1 = 높이 기준(기본)
		float GetMatchWidthOrHeight() const { return m_MatchWidthOrHeight; }
		void SetMatchWidthOrHeight(float match) { m_MatchWidthOrHeight = match; }

		float GetScale() const { return m_Scale; }

		//***** 월드 공간
		//*** 캔버스 크기(캔버스 픽셀). 루트가 이 사각형을 덮는다.
		const Vector2& GetCanvasSize() const { return m_CanvasSize; }
		void SetCanvasSize(const Vector2& size) { m_CanvasSize = size; }

		//*** 캔버스 1픽셀의 월드 길이(오브젝트 스케일이 곱해진다). 예: 400×200, 0.005 → 2m×1m
		float GetWorldUnitsPerPixel() const { return m_WorldUnitsPerPixel; }
		void SetWorldUnitsPerPixel(float units) { m_WorldUnitsPerPixel = units; }

		//*** 캔버스 안에서 오브젝트 원점에 올 점(0~1). 기본 가운데.
		const Vector2& GetCanvasPivot() const { return m_CanvasPivot; }
		void SetCanvasPivot(const Vector2& pivot) { m_CanvasPivot = pivot; }

		//*** 끄면 씬 물체에 가려지지 않는다.
		bool GetDepthTest() const { return m_DepthTest; }
		void SetDepthTest(bool depthTest) { m_DepthTest = depthTest; }

		//*** 캔버스 픽셀 → 월드(오브젝트 Transform 포함)
		Matrix4 GetCanvasToWorld() const;
		//*** 마지막 프레임에 글자를 구운 배율(1/4~4)
		float GetRasterFactor() const { return m_RasterFactor; }

		//***** UISystem 전용
		void Layout(const Vector2& targetSize);
		void LayoutWorld(float rasterFactor);

	private:
		std::unique_ptr<UIElement> m_Root;
		UICanvasMode m_Mode = UICanvasMode::ScreenSpace;
		int32_t m_SortOrder = 0;

		Vector2 m_ReferenceResolution{ 1920.0f, 1080.0f };
		float m_MatchWidthOrHeight = 1.0f;
		float m_Scale = 1.0f;

		Vector2 m_CanvasSize{ 400.0f, 200.0f };
		float m_WorldUnitsPerPixel = 0.005f;
		Vector2 m_CanvasPivot{ 0.5f, 0.5f };
		bool m_DepthTest = true;
		float m_RasterFactor = 1.0f;
	};
}
