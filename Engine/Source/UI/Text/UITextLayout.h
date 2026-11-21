#pragma once

#include "Math/Vector2.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Canape
{
	class UIFont;

	//*** 잘못된 바이트는 U+FFFD로 바꾼다.
	std::u32string DecodeUtf8(std::string_view text);

	struct UITextGlyph
	{
		uint32_t Glyph = 0;
		float X = 0.0f; // 줄 시작부터 펜 위치(픽셀)
	};

	struct UITextLine
	{
		uint32_t First = 0; // UITextLayout::Glyphs 안 시작 위치
		uint32_t Count = 0;
		float Width = 0.0f; // 줄 끝 공백은 빼고 잰 폭
	};

	//***** 픽셀 크기 하나에서 배치한 결과. 줄 정렬은 그릴 때 한다.
	struct UITextLayout
	{
		std::vector<UITextGlyph> Glyphs;
		std::vector<UITextLine> Lines;
		float Ascent = 0.0f;
		float LineHeight = 0.0f;
		Vector2 Size; // 가장 긴 줄의 폭 × 줄 수 높이
	};

	//*** maxWidth <= 0이면 \n에서만 줄을 바꾼다.
	//*** 자동 줄바꿈은 공백에서 끊는다(영문은 단어, 한글은 어절). 한 단어가 폭보다 길면 글자 단위로 자른다.
	UITextLayout LayoutText(const UIFont& font, std::u32string_view text, float pixelSize, float maxWidth,
		float lineSpacing = 1.0f);
}
