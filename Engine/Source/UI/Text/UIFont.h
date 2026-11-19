#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Canape
{
	//*** 픽셀 크기 하나에서의 글리프 비트맵 정보. 좌표는 베이스라인 기준, +Y 아래.
	struct UIGlyphBitmap
	{
		int32_t OffsetX = 0;
		int32_t OffsetY = 0;
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Coverage; // Width * Height, 0~255
	};

	//*** 픽셀 크기 하나에서의 줄 간격 정보(픽셀)
	struct UIFontMetrics
	{
		float Ascent = 0.0f;  // 베이스라인 위로 올라가는 높이(양수)
		float Descent = 0.0f; // 베이스라인 아래로 내려가는 깊이(양수)
		float LineGap = 0.0f;
		float LineHeight() const { return Ascent + Descent + LineGap; }
	};

	//***** TrueType 폰트 파일 하나(stb_truetype)
	//*** 글자 크기는 em 기준 픽셀(CSS font-size와 같은 의미)이다.
	class UIFont
	{
	public:
		~UIFont();
		UIFont(const UIFont&) = delete;
		UIFont& operator=(const UIFont&) = delete;

		//*** 실패하면 nullptr. error에 이유를 남긴다.
		static std::shared_ptr<UIFont> LoadFromFile(const std::filesystem::path& path, std::string& error);
		static std::shared_ptr<UIFont> LoadFromMemory(std::vector<uint8_t> bytes, std::string& error);

		//*** UIText가 폰트를 지정하지 않았을 때 쓰는 기본 폰트(UISystem이 지정)
		static void SetDefault(std::shared_ptr<UIFont> font);
		static const std::shared_ptr<UIFont>& GetDefault();

		//*** 글리프 아틀라스 키용 고유 번호(재사용하지 않음)
		uint32_t GetId() const { return m_Id; }

		uint32_t FindGlyph(char32_t codepoint) const; // 없으면 0(.notdef)
		UIFontMetrics GetMetrics(float pixelSize) const;
		float GetAdvance(uint32_t glyph, float pixelSize) const;
		float GetKerning(uint32_t left, uint32_t right, float pixelSize) const;
		UIGlyphBitmap Rasterize(uint32_t glyph, float pixelSize) const;

	private:
		struct Data;
		UIFont();
		float ScaleFor(float pixelSize) const;

		std::unique_ptr<Data> m_Data;
		uint32_t m_Id = 0;
	};
}
