#include "UI/Text/UIFont.h"

#include <fstream>
#include <iterator>

// stb_truetype를 이 파일 전용(static)으로 넣는다. 쓰지 않는 함수가 제거됐다는 경고(C4505)는
// 번역 단위 끝에서 나오므로 push/pop 밖에서 이 파일 전체에 끈다.
#pragma warning(disable : 4505)
#pragma warning(push)
#pragma warning(disable : 4100 4244 4245 4456 4457 4701 4706)
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include <stb_truetype.h>
#pragma warning(pop)

namespace Canape
{
	struct UIFont::Data
	{
		std::vector<uint8_t> Bytes; // stbtt_fontinfo가 이 버퍼를 가리킨다.
		stbtt_fontinfo Info{};
	};

	namespace
	{
		std::shared_ptr<UIFont>& DefaultFont()
		{
			static std::shared_ptr<UIFont> font;
			return font;
		}
	}

	UIFont::UIFont()
		: m_Data(std::make_unique<Data>())
	{
		static uint32_t nextId = 1;
		m_Id = nextId++;
	}

	UIFont::~UIFont() = default;

	std::shared_ptr<UIFont> UIFont::LoadFromFile(const std::filesystem::path& path, std::string& error)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			error = "Cannot open font file: " + path.string();
			return nullptr;
		}
		std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return LoadFromMemory(std::move(bytes), error);
	}

	std::shared_ptr<UIFont> UIFont::LoadFromMemory(std::vector<uint8_t> bytes, std::string& error)
	{
		if (bytes.empty())
		{
			error = "Font data is empty";
			return nullptr;
		}
		std::shared_ptr<UIFont> font(new UIFont());
		font->m_Data->Bytes = std::move(bytes);
		const unsigned char* data = font->m_Data->Bytes.data();
		const int offset = stbtt_GetFontOffsetForIndex(data, 0);
		if (offset < 0 || !stbtt_InitFont(&font->m_Data->Info, data, offset))
		{
			error = "Not a readable TrueType font";
			return nullptr;
		}
		error.clear();
		return font;
	}

	void UIFont::SetDefault(std::shared_ptr<UIFont> font)
	{
		DefaultFont() = std::move(font);
	}

	const std::shared_ptr<UIFont>& UIFont::GetDefault()
	{
		return DefaultFont();
	}

	float UIFont::ScaleFor(float pixelSize) const
	{
		return stbtt_ScaleForMappingEmToPixels(&m_Data->Info, pixelSize);
	}

	uint32_t UIFont::FindGlyph(char32_t codepoint) const
	{
		return static_cast<uint32_t>(stbtt_FindGlyphIndex(&m_Data->Info, static_cast<int>(codepoint)));
	}

	UIFontMetrics UIFont::GetMetrics(float pixelSize) const
	{
		int ascent = 0, descent = 0, lineGap = 0;
		stbtt_GetFontVMetrics(&m_Data->Info, &ascent, &descent, &lineGap);
		const float scale = ScaleFor(pixelSize);
		return { ascent * scale, -descent * scale, lineGap * scale };
	}

	float UIFont::GetAdvance(uint32_t glyph, float pixelSize) const
	{
		int advance = 0, bearing = 0;
		stbtt_GetGlyphHMetrics(&m_Data->Info, static_cast<int>(glyph), &advance, &bearing);
		return advance * ScaleFor(pixelSize);
	}

	float UIFont::GetKerning(uint32_t left, uint32_t right, float pixelSize) const
	{
		const int kern = stbtt_GetGlyphKernAdvance(&m_Data->Info, static_cast<int>(left), static_cast<int>(right));
		return kern * ScaleFor(pixelSize);
	}

	UIGlyphBitmap UIFont::Rasterize(uint32_t glyph, float pixelSize) const
	{
		const float scale = ScaleFor(pixelSize);
		int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
		stbtt_GetGlyphBitmapBox(&m_Data->Info, static_cast<int>(glyph), scale, scale, &x0, &y0, &x1, &y1);
		UIGlyphBitmap bitmap;
		bitmap.OffsetX = x0;
		bitmap.OffsetY = y0;
		if (x1 <= x0 || y1 <= y0)
		{
			return bitmap; // 공백처럼 그릴 것이 없는 글리프
		}
		bitmap.Width = static_cast<uint32_t>(x1 - x0);
		bitmap.Height = static_cast<uint32_t>(y1 - y0);
		bitmap.Coverage.resize(size_t(bitmap.Width) * bitmap.Height);
		stbtt_MakeGlyphBitmap(&m_Data->Info, bitmap.Coverage.data(), static_cast<int>(bitmap.Width),
			static_cast<int>(bitmap.Height), static_cast<int>(bitmap.Width), scale, scale, static_cast<int>(glyph));
		return bitmap;
	}
}
