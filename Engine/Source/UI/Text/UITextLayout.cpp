#include "UI/Text/UITextLayout.h"
#include "UI/Text/UIFont.h"

#include <algorithm>

namespace Canape
{
	std::u32string DecodeUtf8(std::string_view text)
	{
		constexpr char32_t replacement = 0xFFFD;
		std::u32string result;
		result.reserve(text.size());
		size_t i = 0;
		while (i < text.size())
		{
			const auto lead = static_cast<uint8_t>(text[i]);
			size_t length = 0;
			char32_t codepoint = 0;
			if (lead < 0x80) { codepoint = lead; length = 1; }
			else if ((lead & 0xE0) == 0xC0) { codepoint = lead & 0x1F; length = 2; }
			else if ((lead & 0xF0) == 0xE0) { codepoint = lead & 0x0F; length = 3; }
			else if ((lead & 0xF8) == 0xF0) { codepoint = lead & 0x07; length = 4; }
			else { result.push_back(replacement); ++i; continue; }

			if (i + length > text.size())
			{
				result.push_back(replacement);
				break;
			}
			bool valid = true;
			for (size_t k = 1; k < length; ++k)
			{
				const auto next = static_cast<uint8_t>(text[i + k]);
				if ((next & 0xC0) != 0x80)
				{
					valid = false;
					break;
				}
				codepoint = (codepoint << 6) | (next & 0x3F);
			}
			// 너무 긴 표현, 서로게이트, 범위 밖 값은 잘못된 입력으로 본다.
			constexpr char32_t minimum[] = { 0, 0, 0x80, 0x800, 0x10000 };
			if (!valid || codepoint < minimum[length] || (codepoint >= 0xD800 && codepoint <= 0xDFFF) || codepoint > 0x10FFFF)
			{
				result.push_back(replacement);
				i += valid ? length : 1;
				continue;
			}
			result.push_back(codepoint);
			i += length;
		}
		return result;
	}

	namespace
	{
		bool IsSpace(char32_t c) { return c == U' ' || c == U'\t'; }
		bool IsBreak(char32_t c) { return c == U'\n' || c == U'\r'; }
		constexpr uint32_t kNoGlyph = UINT32_MAX;
	}

	UITextLayout LayoutText(const UIFont& font, std::u32string_view text, float pixelSize, float maxWidth, float lineSpacing)
	{
		UITextLayout out;
		const UIFontMetrics metrics = font.GetMetrics(pixelSize);
		out.Ascent = metrics.Ascent;
		out.LineHeight = metrics.LineHeight() * lineSpacing;
		if (text.empty() || pixelSize <= 0.0f)
		{
			return out;
		}

		const bool wrap = maxWidth > 0.0f;
		const uint32_t spaceGlyph = font.FindGlyph(U' ');
		const float spaceAdvance = font.GetAdvance(spaceGlyph, pixelSize);

		uint32_t lineStart = 0;
		float pen = 0.0f;
		float lineWidth = 0.0f;
		uint32_t previous = kNoGlyph;
		bool wrappedLine = false;

		const auto lineHasContent = [&] { return out.Glyphs.size() > lineStart; };
		const auto finishLine = [&](bool wrapped)
		{
			out.Lines.push_back({ lineStart, static_cast<uint32_t>(out.Glyphs.size()) - lineStart, lineWidth });
			lineStart = static_cast<uint32_t>(out.Glyphs.size());
			pen = 0.0f;
			lineWidth = 0.0f;
			previous = kNoGlyph;
			wrappedLine = wrapped;
		};
		const auto kerning = [&](uint32_t right) { return previous == kNoGlyph ? 0.0f : font.GetKerning(previous, right, pixelSize); };

		std::vector<std::pair<uint32_t, float>> word; // 글리프, 전진 폭
		size_t i = 0;
		while (i < text.size())
		{
			const char32_t c = text[i];
			if (c == U'\n')
			{
				finishLine(false);
				++i;
				continue;
			}
			if (c == U'\r')
			{
				++i;
				continue;
			}
			if (IsSpace(c))
			{
				// 자동 줄바꿈 직후의 줄 앞 공백은 버린다. 문단 첫 공백(들여쓰기)은 남긴다.
				if (!(wrappedLine && !lineHasContent()))
				{
					out.Glyphs.push_back({ spaceGlyph, pen });
					pen += spaceAdvance;
					previous = spaceGlyph;
				}
				++i;
				continue;
			}

			// 단어 하나: 다음 공백이나 줄바꿈 전까지
			size_t end = i;
			word.clear();
			float wordWidth = 0.0f;
			uint32_t wordPrevious = kNoGlyph;
			while (end < text.size() && !IsSpace(text[end]) && !IsBreak(text[end]))
			{
				const uint32_t glyph = font.FindGlyph(text[end]);
				const float advance = font.GetAdvance(glyph, pixelSize);
				if (wordPrevious != kNoGlyph)
				{
					wordWidth += font.GetKerning(wordPrevious, glyph, pixelSize);
				}
				wordWidth += advance;
				word.emplace_back(glyph, advance);
				wordPrevious = glyph;
				++end;
			}

			if (wrap && lineHasContent() && pen + kerning(word.front().first) + wordWidth > maxWidth)
			{
				finishLine(true);
			}
			for (const auto& [glyph, advance] : word)
			{
				const float kern = kerning(glyph);
				// 줄보다 긴 단어는 글자 단위로 자른다.
				if (wrap && lineHasContent() && pen + kern + advance > maxWidth)
				{
					finishLine(true);
					out.Glyphs.push_back({ glyph, 0.0f });
					pen = advance;
				}
				else
				{
					out.Glyphs.push_back({ glyph, pen + kern });
					pen += kern + advance;
				}
				lineWidth = pen;
				previous = glyph;
			}
			i = end;
		}
		finishLine(false);

		float widest = 0.0f;
		for (const UITextLine& line : out.Lines)
		{
			widest = std::max(widest, line.Width);
		}
		out.Size = { widest, out.LineHeight * static_cast<float>(out.Lines.size()) };
		return out;
	}
}
