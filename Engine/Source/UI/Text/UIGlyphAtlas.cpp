#include "UI/Text/UIGlyphAtlas.h"
#include "UI/Text/UIFont.h"

#include <algorithm>

namespace Canape
{
	UIGlyphAtlas::UIGlyphAtlas()
		: m_Pixels(size_t(kSize) * kSize * 4, 0)
	{
		Reset();
	}

	void UIGlyphAtlas::Reset()
	{
		std::fill(m_Pixels.begin(), m_Pixels.end(), uint8_t{ 0 });
		for (uint32_t y = 0; y < kWhiteBlock; ++y)
		{
			std::fill_n(m_Pixels.begin() + (size_t(y) * kSize * 4), size_t(kWhiteBlock) * 4, uint8_t{ 255 });
		}
		m_Glyphs.clear();
		m_ShelfX = kWhiteBlock + kPadding;
		m_ShelfY = 0;
		m_ShelfHeight = kWhiteBlock;
		m_ResetPending = false;
		MarkDirty(0, 0, kSize, kSize);
	}

	Vector2 UIGlyphAtlas::GetWhiteUV() const
	{
		const float center = (kWhiteBlock * 0.5f) / kSize;
		return { center, center };
	}

	void UIGlyphAtlas::MarkDirty(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
	{
		if (m_DirtyRight <= m_DirtyLeft || m_DirtyBottom <= m_DirtyTop)
		{
			m_DirtyLeft = x;
			m_DirtyTop = y;
			m_DirtyRight = x + width;
			m_DirtyBottom = y + height;
			return;
		}
		m_DirtyLeft = std::min(m_DirtyLeft, x);
		m_DirtyTop = std::min(m_DirtyTop, y);
		m_DirtyRight = std::max(m_DirtyRight, x + width);
		m_DirtyBottom = std::max(m_DirtyBottom, y + height);
	}

	bool UIGlyphAtlas::Allocate(uint32_t width, uint32_t height, uint32_t& x, uint32_t& y)
	{
		const uint32_t paddedWidth = width + kPadding;
		const uint32_t paddedHeight = height + kPadding;
		if (paddedWidth > kSize || paddedHeight > kSize)
		{
			return false;
		}
		if (m_ShelfX + paddedWidth > kSize)
		{
			m_ShelfY += m_ShelfHeight + kPadding;
			m_ShelfX = 0;
			m_ShelfHeight = 0;
		}
		if (m_ShelfY + paddedHeight > kSize)
		{
			return false;
		}
		x = m_ShelfX;
		y = m_ShelfY;
		m_ShelfX += paddedWidth;
		m_ShelfHeight = std::max(m_ShelfHeight, height);
		return true;
	}

	void UIGlyphAtlas::BeginFrame(const UITextureDevice& device)
	{
		if (m_ResetPending)
		{
			Reset();
		}
		if (!m_Texture.IsValid() && device.Create)
		{
			m_Texture = device.Create(kSize, kSize, m_Pixels);
			if (m_Texture.IsValid())
			{
				m_DirtyLeft = m_DirtyTop = m_DirtyRight = m_DirtyBottom = 0; // 만들 때 전체를 올렸다.
			}
		}
	}

	void UIGlyphAtlas::Flush(const UITextureDevice& device)
	{
		if (m_DirtyRight <= m_DirtyLeft || m_DirtyBottom <= m_DirtyTop)
		{
			return;
		}
		if (!m_Texture.IsValid() || !device.Update)
		{
			return; // 텍스처가 생기면 BeginFrame의 Create가 전체를 올린다.
		}
		const uint32_t width = m_DirtyRight - m_DirtyLeft;
		const uint32_t height = m_DirtyBottom - m_DirtyTop;
		std::vector<uint8_t> region(size_t(width) * height * 4);
		for (uint32_t row = 0; row < height; ++row)
		{
			const auto source = m_Pixels.begin() + ((size_t(m_DirtyTop) + row) * kSize + m_DirtyLeft) * 4;
			std::copy_n(source, size_t(width) * 4, region.begin() + size_t(row) * width * 4);
		}
		if (device.Update(m_Texture, m_DirtyLeft, m_DirtyTop, width, height, region))
		{
			m_DirtyLeft = m_DirtyTop = m_DirtyRight = m_DirtyBottom = 0;
		}
	}

	const UIAtlasGlyph* UIGlyphAtlas::GetGlyph(const UIFont& font, uint32_t pixelSize, uint32_t glyph)
	{
		const uint64_t key = (uint64_t(font.GetId()) << 48) | (uint64_t(pixelSize & 0xFFFF) << 32) | glyph;
		if (const auto found = m_Glyphs.find(key); found != m_Glyphs.end())
		{
			return &found->second;
		}
		if (m_ResetPending)
		{
			return nullptr;
		}

		UIGlyphBitmap bitmap = font.Rasterize(glyph, static_cast<float>(pixelSize));
		UIAtlasGlyph entry;
		entry.OffsetX = bitmap.OffsetX;
		entry.OffsetY = bitmap.OffsetY;
		if (bitmap.Width > 0 && bitmap.Height > 0)
		{
			uint32_t x = 0, y = 0;
			if (!Allocate(bitmap.Width, bitmap.Height, x, y))
			{
				m_ResetPending = true; // 다음 프레임에 비우고 다시 굽는다.
				return nullptr;
			}
			for (uint32_t row = 0; row < bitmap.Height; ++row)
			{
				uint8_t* destination = m_Pixels.data() + ((size_t(y) + row) * kSize + x) * 4;
				const uint8_t* coverage = bitmap.Coverage.data() + size_t(row) * bitmap.Width;
				for (uint32_t column = 0; column < bitmap.Width; ++column)
				{
					destination[column * 4 + 0] = 255;
					destination[column * 4 + 1] = 255;
					destination[column * 4 + 2] = 255;
					destination[column * 4 + 3] = coverage[column];
				}
			}
			MarkDirty(x, y, bitmap.Width, bitmap.Height);
			entry.Width = bitmap.Width;
			entry.Height = bitmap.Height;
			constexpr float inverse = 1.0f / kSize;
			entry.UV = { x * inverse, y * inverse, bitmap.Width * inverse, bitmap.Height * inverse };
		}
		return &m_Glyphs.emplace(key, entry).first->second;
	}
}
