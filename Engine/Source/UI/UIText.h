#pragma once

#include "UI/Text/UITextLayout.h"
#include "UI/UIElement.h"

#include <memory>
#include <string>

namespace Canape
{
	class UIFont;

	//*** 가로: Start=왼쪽, Center, End=오른쪽 / 세로: Start=위, Center, End=아래
	enum class UITextAlign : uint8_t
	{
		Start,
		Center,
		End
	};

	//***** UTF-8 텍스트. 글리프는 UISystem의 아틀라스에 필요한 것만 구워서 그린다.
	//*** 글자 크기는 캔버스 기준 픽셀(em)이며, 캔버스 스케일을 곱한 뒤 정수 픽셀로 맞춰 굽는다.
	//*** 폰트를 지정하지 않으면 UIFont::GetDefault()를 쓴다.
	class UIText : public UIElement
	{
	public:
		explicit UIText(std::string name = {});
		~UIText() override;

		const std::string& GetText() const { return m_Text; }
		void SetText(std::string text);

		const std::shared_ptr<UIFont>& GetFont() const { return m_Font; }
		void SetFont(std::shared_ptr<UIFont> font);

		float GetFontSize() const { return m_FontSize; }
		void SetFontSize(float size) { m_FontSize = size; }

		UIColor GetColor() const { return m_Color; }
		void SetColor(UIColor color) { m_Color = color; }

		UITextAlign GetHorizontalAlign() const { return m_HorizontalAlign; }
		void SetHorizontalAlign(UITextAlign align) { m_HorizontalAlign = align; }
		UITextAlign GetVerticalAlign() const { return m_VerticalAlign; }
		void SetVerticalAlign(UITextAlign align) { m_VerticalAlign = align; }

		//*** 켜면 요소 폭에 맞춰 공백에서 줄을 바꾼다(영문 단어, 한글 어절). \n은 항상 줄바꿈.
		bool GetWrap() const { return m_Wrap; }
		void SetWrap(bool wrap) { m_Wrap = wrap; }

		//*** 줄 높이 배수(1 = 폰트 기본)
		float GetLineSpacing() const { return m_LineSpacing; }
		void SetLineSpacing(float spacing) { m_LineSpacing = spacing; }

		//*** 줄바꿈 없이 잰 글자 영역(타깃 픽셀)
		Vector2 GetPreferredSize(float scale) const override;
		//*** 줄바꿈이 켜져 있으면 그 폭에서 줄바꿈한 높이
		float GetPreferredHeight(float width, float scale) const override;

		//*** 마지막으로 배치한 결과(테스트·디버깅용)
		const UITextLayout& GetTextLayout() const { return m_PaintCache.Layout; }
		uint32_t GetPixelSize(float scale) const;

	protected:
		void OnLayout() override;
		void OnPaint(UIPainter& painter) const override;

	private:
		struct Cache
		{
			uint32_t FontId = 0;
			uint32_t PixelSize = 0;
			float MaxWidth = -1.0f;
			float LineSpacing = 0.0f;
			uint64_t Version = 0;
			UITextLayout Layout;
		};

		const UIFont* ResolveFont() const;
		const UITextLayout& Ensure(Cache& cache, const UIFont& font, uint32_t pixelSize, float maxWidth) const;

		std::string m_Text;
		std::u32string m_Codepoints;
		uint64_t m_Version = 1;
		std::shared_ptr<UIFont> m_Font;
		float m_FontSize = 24.0f;
		UIColor m_Color;
		UITextAlign m_HorizontalAlign = UITextAlign::Start;
		UITextAlign m_VerticalAlign = UITextAlign::Start;
		bool m_Wrap = false;
		float m_LineSpacing = 1.0f;

		mutable Cache m_PaintCache;   // 요소 폭으로 줄바꿈한 결과
		mutable Cache m_MeasureCache; // 줄바꿈 없이 잰 결과
	};
}
