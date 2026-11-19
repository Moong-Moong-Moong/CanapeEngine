#include "UI/UIText.h"
#include "UI/Text/UIFont.h"
#include "UI/Text/UIGlyphAtlas.h"
#include "UI/UIPainter.h"

#include <algorithm>
#include <cmath>

namespace Canape
{
	namespace
	{
		float AlignFactor(UITextAlign align)
		{
			switch (align)
			{
			case UITextAlign::Center: return 0.5f;
			case UITextAlign::End: return 1.0f;
			default: return 0.0f;
			}
		}
	}

	UIText::UIText(std::string name)
		: UIElement(std::move(name))
	{
	}

	UIText::~UIText() = default;

	void UIText::SetText(std::string text)
	{
		if (text == m_Text)
		{
			return;
		}
		m_Text = std::move(text);
		m_Codepoints = DecodeUtf8(m_Text);
		++m_Version;
	}

	void UIText::SetFont(std::shared_ptr<UIFont> font)
	{
		m_Font = std::move(font);
	}

	const UIFont* UIText::ResolveFont() const
	{
		return m_Font ? m_Font.get() : UIFont::GetDefault().get();
	}

	uint32_t UIText::GetPixelSize(float scale) const
	{
		return static_cast<uint32_t>(std::clamp(std::round(m_FontSize * scale), 1.0f, 512.0f));
	}

	const UITextLayout& UIText::Ensure(Cache& cache, const UIFont& font, uint32_t pixelSize, float maxWidth) const
	{
		if (cache.FontId != font.GetId() || cache.PixelSize != pixelSize || cache.MaxWidth != maxWidth
			|| cache.LineSpacing != m_LineSpacing || cache.Version != m_Version)
		{
			cache.Layout = LayoutText(font, m_Codepoints, static_cast<float>(pixelSize), maxWidth, m_LineSpacing);
			cache.FontId = font.GetId();
			cache.PixelSize = pixelSize;
			cache.MaxWidth = maxWidth;
			cache.LineSpacing = m_LineSpacing;
			cache.Version = m_Version;
		}
		return cache.Layout;
	}

	Vector2 UIText::GetPreferredSize(float scale) const
	{
		const UIFont* font = ResolveFont();
		if (!font)
		{
			return UIElement::GetPreferredSize(scale);
		}
		const UITextLayout& layout = Ensure(m_MeasureCache, *font, GetPixelSize(scale), 0.0f);
		return { std::ceil(layout.Size.X), std::ceil(layout.Size.Y) };
	}

	float UIText::GetPreferredHeight(float width, float scale) const
	{
		const UIFont* font = ResolveFont();
		if (!font || !m_Wrap)
		{
			return GetPreferredSize(scale).Y;
		}
		// 배치 직후 같은 폭으로 그리므로 그리기용 캐시를 그대로 채워 둔다.
		return std::ceil(Ensure(m_PaintCache, *font, GetPixelSize(scale), width).Size.Y);
	}

	void UIText::OnLayout()
	{
		if (const UIFont* font = ResolveFont())
		{
			Ensure(m_PaintCache, *font, GetPixelSize(GetLayoutScale()), m_Wrap ? GetLayoutRect().Width : 0.0f);
		}
	}

	void UIText::OnPaint(UIPainter& painter) const
	{
		const UIFont* font = ResolveFont();
		UIGlyphAtlas* atlas = painter.GetGlyphAtlas();
		if (!font || !atlas || m_Codepoints.empty())
		{
			return;
		}
		const UIRect& rect = GetLayoutRect();
		const uint32_t pixelSize = GetPixelSize(GetLayoutScale());
		const UITextLayout& layout = Ensure(m_PaintCache, *font, pixelSize, m_Wrap ? rect.Width : 0.0f);

		const float top = rect.Y + (rect.Height - layout.Size.Y) * AlignFactor(m_VerticalAlign);
		const float horizontal = AlignFactor(m_HorizontalAlign);
		for (size_t index = 0; index < layout.Lines.size(); ++index)
		{
			const UITextLine& line = layout.Lines.at(index);
			const float lineX = rect.X + (rect.Width - line.Width) * horizontal;
			const float baseline = std::round(top + layout.Ascent + layout.LineHeight * static_cast<float>(index));
			for (uint32_t k = 0; k < line.Count; ++k)
			{
				const UITextGlyph& glyph = layout.Glyphs.at(line.First + k);
				const UIAtlasGlyph* entry = atlas->GetGlyph(*font, pixelSize, glyph.Glyph);
				if (!entry || entry->Width == 0)
				{
					continue;
				}
				// 정수 픽셀에 맞춰 1:1로 샘플링되게 한다(번짐 방지).
				const float x = std::round(lineX + glyph.X) + static_cast<float>(entry->OffsetX);
				const float y = baseline + static_cast<float>(entry->OffsetY);
				painter.AddQuadResource({ x, y, static_cast<float>(entry->Width), static_cast<float>(entry->Height) },
					entry->UV, m_Color, atlas->GetTexture());
			}
		}
	}
}
