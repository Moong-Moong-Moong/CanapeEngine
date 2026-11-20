// Standalone CPU test for the UI core (layout, painting, pointer routing, text, boxes); not part of Runtime.vcxproj.
// Compiled together with the UI sources by ValidateUI.ps1. No renderer, scene or RTTR dependency.
// Run from the repository root: the text checks load Game/Content/Fonts/Pretendard-Regular.ttf.
#include "UI/Text/UIFont.h"
#include "UI/Text/UIGlyphAtlas.h"
#include "UI/Text/UITextLayout.h"
#include "UI/UIBox.h"
#include "UI/UIButton.h"
#include "UI/UIElement.h"
#include "UI/UIImage.h"
#include "UI/UIPainter.h"
#include "UI/UIPointerRouter.h"
#include "UI/UIText.h"
#include "UI/UIWorld.h"

#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Canape;

namespace
{
	void Require(bool value, const char* message)
	{
		if (!value)
			throw std::runtime_error(message);
	}

	bool Near(float a, float b) { return std::abs(a - b) < 1e-4f; }

	void ExpectRect(const UIRect& rect, float x, float y, float w, float h, const char* what)
	{
		if (!(Near(rect.X, x) && Near(rect.Y, y) && Near(rect.Width, w) && Near(rect.Height, h)))
		{
			std::cerr << what << ": got {" << rect.X << ',' << rect.Y << ',' << rect.Width << ',' << rect.Height
					  << "} expected {" << x << ',' << y << ',' << w << ',' << h << "}\n";
			throw std::runtime_error(what);
		}
	}

	// Same setup as UICanvasComponent's root.
	void SetupRoot(UIElement& root)
	{
		root.SetAnchors({ 0, 0 }, { 1, 1 });
		root.SetSize({ 0, 0 });
	}

	void ScaleChecks()
	{
		Require(Near(ComputeCanvasScale({ 960, 540 }, { 1920, 1080 }, 1.0f), 0.5f), "half resolution scale");
		Require(Near(ComputeCanvasScale({ 1920, 540 }, { 1920, 1080 }, 1.0f), 0.5f), "match height");
		Require(Near(ComputeCanvasScale({ 1920, 540 }, { 1920, 1080 }, 0.0f), 1.0f), "match width");
		Require(Near(ComputeCanvasScale({ 1920, 540 }, { 1920, 1080 }, 0.5f), std::sqrt(0.5f)), "log-space blend");
		Require(Near(ComputeCanvasScale({ 800, 600 }, { 0, 0 }, 1.0f), 1.0f), "no reference resolution");
		std::cout << "UI scale PASS\n";
	}

	void LayoutChecks()
	{
		UIElement root("Root");
		SetupRoot(root);
		auto* topRight = root.AddChild<UIElement>("TopRight");
		topRight->SetAnchor({ 1, 0 });
		topRight->SetPivot({ 1, 0 });
		topRight->SetPosition({ -32, 32 });
		topRight->SetSize({ 160, 56 });

		auto* panel = root.AddChild<UIElement>("Panel");
		panel->SetPosition({ 32, 32 });
		panel->SetSize({ 420, 36 });
		auto* inset = panel->AddChild<UIElement>("Inset");
		inset->SetAnchors({ 0, 0 }, { 1, 1 });
		inset->SetPivot({ 0.5f, 0.5f });
		inset->SetSize({ -8, -8 });

		auto* centered = root.AddChild<UIElement>("Centered");
		centered->SetAnchor({ 0.5f, 0.5f });
		centered->SetPivot({ 0.5f, 0.5f });
		centered->SetSize({ 101, 51 }); // odd size: edges must still land on whole pixels

		root.Layout({ 0, 0, 800, 600 }, 0.5f);
		ExpectRect(root.GetLayoutRect(), 0, 0, 800, 600, "root stretches over the target");
		ExpectRect(topRight->GetLayoutRect(), 704, 16, 80, 28, "top-right anchor with pivot and scale");
		ExpectRect(panel->GetLayoutRect(), 16, 16, 210, 18, "top-left default anchor");
		ExpectRect(inset->GetLayoutRect(), 18, 18, 206, 14, "stretch child with negative size");
		const UIRect& c = centered->GetLayoutRect();
		Require(c.X == std::round(c.X) && c.Right() == std::round(c.Right()), "edges snapped to whole pixels");
		Require(std::abs(c.X + c.Width * 0.5f - 400.0f) <= 0.5f && std::abs(c.Y + c.Height * 0.5f - 300.0f) <= 0.5f,
			"centered element stays centered after snapping");

		Require(root.FindByName("Inset") == inset, "FindByName finds a grandchild");
		Require(root.Find<UIElement>("Missing") == nullptr, "FindByName misses unknown names");
		Require(root.FindById(inset->GetId()) == inset, "FindById");
		Require(inset->GetParent() == panel, "parent link");
		std::cout << "UI layout PASS\n";
	}

	void PaintChecks()
	{
		UIElement root("Root");
		SetupRoot(root);
		auto* back = root.AddChild<UIImage>("Back");
		back->SetSize({ 100, 10 });
		back->SetColor({ 10, 20, 30, 255 });
		auto* fill = root.AddChild<UIImage>("Fill");
		fill->SetPosition({ 0, 20 });
		fill->SetSize({ 100, 10 });
		fill->SetFillAmount(0.25f);
		auto* reverseFill = root.AddChild<UIImage>("ReverseFill");
		reverseFill->SetPosition({ 0, 40 });
		reverseFill->SetSize({ 100, 10 });
		reverseFill->SetFillDirection(UIFillDirection::RightToLeft);
		reverseFill->SetFillAmount(0.5f);
		auto* empty = root.AddChild<UIImage>("Empty");
		empty->SetFillAmount(0.0f);
		auto* hidden = root.AddChild<UIImage>("Hidden");
		hidden->SetVisible(false);
		auto* transparent = root.AddChild<UIImage>("Transparent");
		transparent->SetColor({ 255, 255, 255, 0 });
		auto* textured = root.AddChild<UIImage>("Textured");
		textured->SetTexture(TextureAssetId{ 7 });
		auto* notUploaded = root.AddChild<UIImage>("NotUploaded");
		notUploaded->SetTexture(TextureAssetId{ 8 });
		root.AddChild<UIImage>("AfterTexture");
		auto* removed = root.AddChild<UIImage>("Removed");
		removed->RemoveFromParent();

		root.Layout({ 0, 0, 200, 100 }, 1.0f);
		const UITextureResolver resolver = [](TextureAssetId id) { return id.value == 7 ? TextureResourceId{ 70 } : TextureResourceId{}; };
		UIDrawList list;
		UIPainter painter(list, { 0, 0, 200, 100 }, &resolver);
		root.Paint(painter);

		// back + fill + reverseFill + textured + afterTexture = 5 quads; solid ones merge around the texture.
		Require(list.Vertices.size() == 5 * 4 && list.Indices.size() == 5 * 6, "five quads painted");
		Require(list.Commands.size() == 3, "solid quads merge; texture splits the batch");
		Require(list.Commands[0].IndexCount == 18 && !list.Commands[0].Texture.IsValid(), "first solid batch");
		Require(list.Commands[1].Texture == TextureResourceId{ 70 } && list.Commands[1].IndexOffset == 18, "texture batch");
		Require(list.Commands[2].IndexOffset == 24 && list.Commands[2].IndexCount == 6, "solid batch after texture");
		Require(list.Vertices[0].Color == UIColor(10, 20, 30, 255).Pack(), "vertex color packed R-low");
		Require(Near(list.Vertices[5].Position[0], 25.0f) && Near(list.Vertices[5].UV[0], 0.25f), "left-to-right fill crops rect and UV");
		Require(Near(list.Vertices[8].Position[0], 50.0f) && Near(list.Vertices[8].UV[0], 0.5f), "right-to-left fill keeps the right half");
		Require(list.Indices[6] == 4, "indices address the shared vertex array");

		root.FlushRemovals();
		Require(root.GetChildren().size() == 9 && root.FindByName("Removed") == nullptr, "FlushRemovals drops removed children");
		Require((UIColor(255, 128, 0, 255) * UIColor(128, 255, 255, 128)) == UIColor(128, 128, 0, 128), "color modulate");
		std::cout << "UI paint PASS\n";
	}

	struct ProbeButton final : UIButton
	{
		using UIButton::UIButton;
		int enters = 0;
		int leaves = 0;
		void OnPointerEnter() override { ++enters; }
		void OnPointerLeave() override { ++leaves; }
	};

	void PointerChecks()
	{
		UIElement root("Root");
		SetupRoot(root);
		auto* hud = root.AddChild<UIImage>("Hud"); // not hit-testable: never blocks
		hud->SetSize({ 200, 100 });
		auto* button = root.AddChild<ProbeButton>("Button");
		button->SetPosition({ 10, 10 });
		button->SetSize({ 50, 20 });
		int clicks = 0;
		button->OnClicked = [&] { ++clicks; };
		auto* top = root.AddChild<UIButton>("Top"); // overlaps the right part of Button, drawn later
		top->SetPosition({ 40, 10 });
		top->SetSize({ 50, 20 });
		int topClicks = 0;
		top->OnClicked = [&] { ++topClicks; };
		root.Layout({ 0, 0, 200, 100 }, 1.0f);

		std::array<UIElement*, 1> roots{ &root };
		UIPointerRouter router;
		auto step = [&](float x, float y, bool down, bool inside = true) {
			router.Update(roots, UIPointerState{ { x, y }, inside, down });
		};

		step(100, 80, false);
		Require(!router.IsCapturingPointer() && router.GetHoveredId() == 0, "non-interactive HUD does not capture");

		step(20, 15, false);
		Require(button->IsHovered() && button->enters == 1 && router.IsCapturingPointer(), "hover enters the button");
		step(20, 15, true);
		Require(button->IsPressed() && clicks == 0, "press does not click yet");
		step(20, 15, false);
		Require(clicks == 1 && !button->IsPressed(), "release inside clicks");

		step(20, 15, true);
		step(150, 80, true);
		Require(button->IsPressed() && !button->IsHovered() && button->leaves == 1, "press keeps capture while dragging out");
		Require(router.IsCapturingPointer(), "held press keeps capturing outside the button");
		step(150, 80, false);
		Require(clicks == 1, "release outside cancels the click");
		Require(router.IsCapturingPointer(), "release frame is still captured");
		step(150, 80, false);
		Require(!router.IsCapturingPointer(), "capture ends after the release frame");

		step(45, 15, true);
		step(45, 15, false);
		Require(topClicks == 1 && clicks == 1, "later sibling is on top");

		// Pressing on empty space and releasing over a button must not click it.
		step(150, 80, true);
		step(20, 15, false);
		Require(clicks == 1, "press must start on the button");

		button->SetInteractable(false);
		step(20, 15, true);
		step(20, 15, false);
		Require(clicks == 1 && router.IsCapturingPointer(), "disabled button blocks input without clicking");
		button->SetInteractable(true);

		step(20, 15, false, false);
		Require(router.GetHoveredId() == 0 && !button->IsHovered(), "pointer outside the target hovers nothing");

		// Removing the button from its own click handler is safe; it disappears from hit tests at once.
		button->OnClicked = [&] { ++clicks; button->RemoveFromParent(); };
		step(20, 15, false);
		step(20, 15, true);
		step(20, 15, false);
		Require(clicks == 2, "self-removing click handler ran");
		step(20, 15, false);
		Require(router.GetHoveredId() == 0 && !router.IsCapturingPointer(), "removed button no longer hit");
		root.FlushRemovals();
		step(25, 20, false); // hovered/pressed ids pointing at a destroyed element must be harmless
		router.Reset();
		Require(router.GetHoveredId() == 0 && router.GetPressedId() == 0, "Reset clears state");
		std::cout << "UI pointer routing PASS\n";
	}
}

namespace
{
	//***** 2단계: 텍스트, 아틀라스, 9-slice, 클리핑, 박스

	struct FakeDevice
	{
		int creates = 0;
		std::vector<UIRect> updates;
		UITextureDevice device;

		FakeDevice()
		{
			device.Create = [this](uint32_t width, uint32_t height, std::span<const uint8_t> rgba) {
				Require(width == UIGlyphAtlas::kSize && height == UIGlyphAtlas::kSize && rgba.size() == size_t(width) * height * 4,
					"atlas texture created whole");
				++creates;
				return TextureResourceId{ 42 };
			};
			device.Update = [this](TextureResourceId texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
				std::span<const uint8_t> rgba) {
				Require(texture == TextureResourceId{ 42 } && rgba.size() == size_t(width) * height * 4, "atlas update is tightly packed");
				updates.push_back({ float(x), float(y), float(width), float(height) });
				return true;
			};
		}
	};

	std::shared_ptr<UIFont> LoadTestFont()
	{
		std::string error;
		auto font = UIFont::LoadFromFile("Game/Content/Fonts/Pretendard-Regular.ttf", error);
		if (!font)
			throw std::runtime_error("font load failed (run from the repository root): " + error);
		return font;
	}

	size_t CountNonSpace(const UITextLayout& layout, uint32_t spaceGlyph)
	{
		size_t count = 0;
		for (const UITextGlyph& glyph : layout.Glyphs)
			count += glyph.Glyph != spaceGlyph ? 1 : 0;
		return count;
	}

	void TextChecks(const UIFont& font)
	{
		const std::u32string decoded = DecodeUtf8("A\xEA\xB0\x80\xF0\x9F\x98\x80");
		Require(decoded == std::u32string{ U'A', U'가', U'\U0001F600' }, "UTF-8 decode: ASCII, Hangul, 4-byte");
		Require(DecodeUtf8("\xFF") == std::u32string{ U'�' }, "invalid lead byte");
		Require(DecodeUtf8("a\xE0\xA0") == std::u32string{ U'a', U'�' }, "truncated sequence");
		Require(DecodeUtf8("\xC0\xAF") == std::u32string{ U'�' }, "overlong encoding");
		Require(DecodeUtf8("\xED\xA0\x80") == std::u32string{ U'�' }, "surrogate rejected");

		Require(font.FindGlyph(U'A') != 0 && font.FindGlyph(U'가') != 0, "font has Latin and Hangul glyphs");
		const UIFontMetrics metrics = font.GetMetrics(20.0f);
		Require(metrics.Ascent > 0 && metrics.Descent > 0 && metrics.LineHeight() >= 20.0f, "font metrics");

		const uint32_t space = font.FindGlyph(U' ');
		UITextLayout single = LayoutText(font, U"Hello world", 20.0f, 0.0f);
		Require(single.Lines.size() == 1 && single.Glyphs.size() == 11, "single line keeps every glyph");
		Require(Near(single.Size.Y, single.LineHeight) && single.Size.X > 50.0f, "single line size");
		Require(LayoutText(font, U"a\nb", 20.0f, 0.0f).Lines.size() == 2, "explicit newline");
		Require(LayoutText(font, U"a\n", 20.0f, 0.0f).Lines.size() == 2, "trailing newline adds an empty line");
		Require(LayoutText(font, U"", 20.0f, 0.0f).Lines.empty(), "empty text has no lines");

		const std::u32string paragraph = DecodeUtf8(
			"\xEB\xB2\x84\xED\x8A\xBC\xEC\x9D\x84 \xEB\x88\x84\xEB\xA5\xB4\xEB\xA9\xB4 \xEC\xB2\xB4\xEB\xA0\xA5\xEC\x9D\xB4 "
			"\xED\x9A\x8C\xEB\xB3\xB5\xEB\x90\xA9\xEB\x8B\x88\xEB\x8B\xA4. Long English words wrap at word boundaries.");
		const float maxWidth = 160.0f;
		const UITextLayout wrapped = LayoutText(font, paragraph, 20.0f, maxWidth);
		Require(wrapped.Lines.size() >= 3, "paragraph wraps into several lines");
		size_t nonSpace = 0;
		for (char32_t c : paragraph)
			nonSpace += c != U' ' ? 1 : 0;
		Require(CountNonSpace(wrapped, space) == nonSpace, "wrapping loses no glyph");
		for (size_t i = 0; i < wrapped.Lines.size(); ++i)
		{
			const UITextLine& line = wrapped.Lines.at(i);
			Require(line.Width <= maxWidth + 0.01f, "every wrapped line fits the width");
			if (i > 0 && line.Count > 0)
				Require(wrapped.Glyphs.at(line.First).Glyph != space, "wrapped line does not start with a space");
		}

		const std::u32string longWord(14, U'가');
		const UITextLayout split = LayoutText(font, longWord, 20.0f, 60.0f);
		Require(split.Lines.size() >= 4 && split.Glyphs.size() == 14, "word longer than the width splits by character");
		for (const UITextLine& line : split.Lines)
			Require(line.Width <= 60.01f, "character split lines fit");
		std::cout << "UI text decode/layout/wrap PASS\n";
	}

	void AtlasChecks(const UIFont& font)
	{
		FakeDevice fake;
		UIGlyphAtlas atlas;
		atlas.BeginFrame(fake.device);
		Require(fake.creates == 1 && atlas.GetTexture() == TextureResourceId{ 42 }, "atlas creates its texture once");
		const auto pixels = atlas.GetPixels();
		Require(pixels[3] == 255 && pixels[(3 * UIGlyphAtlas::kSize + 3) * 4 + 3] == 255, "white block for solid quads");
		Require(pixels[(UIGlyphAtlas::kWhiteBlock) * 4 + 3] == 0, "atlas starts empty beside the white block");
		atlas.Flush(fake.device);
		Require(fake.updates.empty(), "nothing to upload right after creation");

		const UIAtlasGlyph* a = atlas.GetGlyph(font, 24, font.FindGlyph(U'가'));
		const UIAtlasGlyph* b = atlas.GetGlyph(font, 24, font.FindGlyph(U'A'));
		Require(a && b && a->Width > 0 && b->Width > 0, "glyphs rasterized");
		Require(atlas.GetGlyph(font, 24, font.FindGlyph(U'가')) == a, "cached glyph reused");
		Require(atlas.GetGlyph(font, 25, font.FindGlyph(U'가')) != a, "pixel size is part of the key");
		const UIAtlasGlyph* space = atlas.GetGlyph(font, 24, font.FindGlyph(U' '));
		Require(space && space->Width == 0, "space has nothing to draw");
		const UIRect ra{ a->UV.X * UIGlyphAtlas::kSize, a->UV.Y * UIGlyphAtlas::kSize, float(a->Width), float(a->Height) };
		const UIRect rb{ b->UV.X * UIGlyphAtlas::kSize, b->UV.Y * UIGlyphAtlas::kSize, float(b->Width), float(b->Height) };
		Require(Intersect(ra, rb).IsEmpty(), "packed glyphs do not overlap");
		Require(ra.X >= UIGlyphAtlas::kWhiteBlock || ra.Y >= UIGlyphAtlas::kWhiteBlock, "glyphs avoid the white block");

		atlas.Flush(fake.device);
		Require(fake.updates.size() == 1, "one upload per frame covers new glyphs");
		const UIRect& dirty = fake.updates.front();
		Require(Near(Intersect(dirty, ra).Width, ra.Width) && Near(Intersect(dirty, rb).Height, rb.Height), "upload covers new glyphs");
		atlas.Flush(fake.device);
		Require(fake.updates.size() == 1, "no upload without new glyphs");

		// 가득 채우기: 큰 글자를 계속 요청하면 자리가 없어 다음 프레임에 비운다.
		const UIAtlasGlyph* last = nullptr;
		char32_t c = U'가';
		for (int i = 0; i < 2000 && (last = atlas.GetGlyph(font, 160, font.FindGlyph(c))) != nullptr; ++i)
			++c;
		Require(last == nullptr && atlas.IsResetPending(), "full atlas schedules a reset");
		atlas.BeginFrame(fake.device);
		Require(!atlas.IsResetPending() && atlas.GetGlyphCount() == 0, "reset clears the atlas next frame");
		atlas.Flush(fake.device);
		Require(Near(fake.updates.back().Width, float(UIGlyphAtlas::kSize)), "reset uploads the whole atlas");
		std::cout << "UI glyph atlas PASS\n";
	}

	void TextElementChecks(const std::shared_ptr<UIFont>& font)
	{
		UIFont::SetDefault(font);
		UIElement root("Root");
		SetupRoot(root);
		auto* label = root.AddChild<UIText>("Label");
		label->SetAnchors({ 0, 0 }, { 1, 1 });
		label->SetSize({ 0, 0 });
		label->SetText("HP 72%");
		label->SetFontSize(20.0f);
		label->SetColor({ 10, 200, 30, 255 });
		label->SetHorizontalAlign(UITextAlign::Center);
		label->SetVerticalAlign(UITextAlign::Center);
		root.Layout({ 0, 0, 400, 100 }, 1.0f);
		Require(label->GetPixelSize(1.0f) == 20 && label->GetPixelSize(0.5f) == 10, "font size follows canvas scale");
		const Vector2 preferred = label->GetPreferredSize(1.0f);
		Require(preferred.X > 30.0f && preferred.X < 200.0f && preferred.Y >= 20.0f, "label preferred size");

		FakeDevice fake;
		UIGlyphAtlas atlas;
		atlas.BeginFrame(fake.device);
		UIDrawList list;
		UIPainter painter(list, { 0, 0, 400, 100 });
		painter.SetGlyphAtlas(&atlas);
		painter.SetSolidSource(atlas.GetTexture(), atlas.GetWhiteUV());
		root.Paint(painter);
		Require(list.Vertices.size() == 5 * 4, "one quad per visible glyph (space skipped)");
		Require(list.Commands.size() == 1 && list.Commands[0].Texture == TextureResourceId{ 42 }, "text draws from the atlas in one batch");
		Require(list.Vertices[0].Color == UIColor(10, 200, 30, 255).Pack(), "text color on vertices");
		float left = 1e9f, right = -1e9f, top = 1e9f, bottom = -1e9f;
		for (const UIVertex& v : list.Vertices)
		{
			left = std::min(left, v.Position[0]);
			right = std::max(right, v.Position[0]);
			top = std::min(top, v.Position[1]);
			bottom = std::max(bottom, v.Position[1]);
			Require(v.Position[0] == std::round(v.Position[0]) && v.Position[1] == std::round(v.Position[1]), "glyph quads on whole pixels");
		}
		Require(std::abs((left + right) * 0.5f - 200.0f) < 6.0f, "centered horizontally");
		Require(std::abs((top + bottom) * 0.5f - 50.0f) < 8.0f, "centered vertically");

		// 단색은 아틀라스의 흰 텍셀로 그려 글자와 같은 배치에 들어간다.
		auto* back = root.AddChild<UIImage>("Back");
		back->SetSize({ 50, 10 });
		list.Clear();
		root.Layout({ 0, 0, 400, 100 }, 1.0f);
		UIPainter mixed(list, { 0, 0, 400, 100 });
		mixed.SetGlyphAtlas(&atlas);
		mixed.SetSolidSource(atlas.GetTexture(), atlas.GetWhiteUV());
		root.Paint(mixed);
		Require(list.Commands.size() == 1, "text and solid quads share one draw");
		Require(Near(list.Vertices.back().UV[0], atlas.GetWhiteUV().X), "solid quad samples the white texel");
		std::cout << "UI text element PASS\n";
	}

	void NineSliceAndClipChecks()
	{
		UIElement root("Root");
		SetupRoot(root);
		auto* panel = root.AddChild<UIImage>("Panel");
		panel->SetSize({ 100, 60 });
		panel->SetNineSlice({ UIEdges(0.25f), UIEdges(10.0f) });
		root.Layout({ 0, 0, 400, 200 }, 1.0f);
		UIDrawList list;
		UIPainter painter(list, { 0, 0, 400, 200 });
		root.Paint(painter);
		Require(list.Vertices.size() == 9 * 4, "nine-slice draws nine quads");
		Require(Near(list.Vertices[1].Position[0], 10.0f) && Near(list.Vertices[1].UV[0], 0.25f), "corner keeps its size and UV");
		const UIVertex& centerTL = list.Vertices[4 * 4];
		const UIVertex& centerBR = list.Vertices[4 * 4 + 2];
		Require(Near(centerTL.Position[0], 10.0f) && Near(centerBR.Position[0], 90.0f) && Near(centerBR.Position[1], 50.0f),
			"center cell stretches");
		Require(Near(centerTL.UV[0], 0.25f) && Near(centerBR.UV[0], 0.75f), "center cell UV");

		panel->SetSize({ 12, 12 });
		root.Layout({ 0, 0, 400, 200 }, 1.0f);
		list.Clear();
		UIPainter small(list, { 0, 0, 400, 200 });
		root.Paint(small);
		Require(list.Vertices.size() == 4 * 4 && Near(list.Vertices[1].Position[0], 6.0f), "borders shrink to fit a small rect");

		panel->SetSize({ 100, 60 });
		panel->SetFillAmount(0.5f);
		root.Layout({ 0, 0, 400, 200 }, 1.0f);
		list.Clear();
		UIPainter gauge(list, { 0, 0, 400, 200 });
		root.Paint(gauge);
		Require(!list.Commands.empty() && list.Commands.back().Clip == UIClipRect{ 0, 0, 50, 60 }, "nine-slice gauge clips to the fill");
		Require(list.Vertices.size() < 9 * 4, "cells outside the fill are culled");

		// 자식 잘라 내기
		UIElement clipRoot("Root");
		SetupRoot(clipRoot);
		auto* box = clipRoot.AddChild<UIImage>("Box");
		box->SetSize({ 50, 50 });
		box->SetClipChildren(true);
		auto* inside = box->AddChild<UIButton>("Partly");
		inside->SetPosition({ 40, 40 });
		inside->SetSize({ 30, 30 });
		auto* outside = box->AddChild<UIImage>("Outside");
		outside->SetPosition({ 100, 100 });
		outside->SetSize({ 10, 10 });
		clipRoot.Layout({ 0, 0, 400, 200 }, 1.0f);
		list.Clear();
		UIPainter clipped(list, { 0, 0, 400, 200 });
		clipped.PushClip({ 20, 0, 400, 200 });
		clipRoot.Paint(clipped);
		clipped.PopClip();
		Require(list.Vertices.size() == 2 * 4, "fully clipped child is culled");
		Require(list.Commands.back().Clip == UIClipRect{ 20, 0, 50, 50 }, "nested clips intersect");
		Require(clipRoot.HitTest({ 45, 45 }) == inside, "child hit inside the clip");
		Require(clipRoot.HitTest({ 60, 60 }) == nullptr, "child not hit outside the clip");
		std::cout << "UI nine-slice and clipping PASS\n";
	}

	void BoxChecks(const std::shared_ptr<UIFont>& font)
	{
		UIFont::SetDefault(font);
		UIElement root("Root");
		SetupRoot(root);
		auto* column = root.AddChild<UIVerticalBox>("Column");
		column->SetSize({ 200, 300 });
		column->SetPadding(UIEdges(10));
		column->SetSpacing(5);
		column->SetChildAlign(UIBoxAlign::Stretch);
		auto* first = column->AddChild<UIElement>("First");
		first->SetSize({ 50, 20 });
		auto* hidden = column->AddChild<UIElement>("Hidden");
		hidden->SetSize({ 40, 40 });
		hidden->SetVisible(false);
		auto* second = column->AddChild<UIElement>("Second");
		second->SetSize({ 80, 30 });
		second->SetPosition({ 500, 500 }); // 박스 안에서는 무시
		auto* text = column->AddChild<UIText>("Text");
		text->SetText("one two three four five six seven eight nine ten");
		text->SetFontSize(20);
		text->SetWrap(true);

		auto* row = root.AddChild<UIHorizontalBox>("Row");
		row->SetPosition({ 300, 0 });
		row->SetFitContent(true);
		row->SetPadding(UIEdges(2));
		row->SetSpacing(4);
		row->SetChildAlign(UIBoxAlign::Center);
		auto* small = row->AddChild<UIElement>("Small");
		small->SetSize({ 50, 20 });
		auto* big = row->AddChild<UIElement>("Big");
		big->SetSize({ 80, 30 });

		root.Layout({ 0, 0, 800, 600 }, 1.0f);
		ExpectRect(first->GetLayoutRect(), 10, 10, 180, 20, "vertical box: stretch + padding");
		ExpectRect(second->GetLayoutRect(), 10, 35, 180, 30, "vertical box: spacing, hidden child takes no space");
		const UIRect& textRect = text->GetLayoutRect();
		Require(Near(textRect.Y, 70) && Near(textRect.Width, 180), "wrapped text placed after siblings");
		Require(textRect.Height > text->GetPreferredSize(1.0f).Y, "wrapped text height follows the box width");
		Require(text->GetTextLayout().Lines.size() >= 2, "text wraps inside the box");
		ExpectRect(row->GetLayoutRect(), 300, 0, 138, 34, "horizontal box fits its content");
		ExpectRect(small->GetLayoutRect(), 302, 7, 50, 20, "horizontal box: cross-axis center");
		ExpectRect(big->GetLayoutRect(), 356, 2, 80, 30, "horizontal box: spacing");

		root.Layout({ 0, 0, 1600, 1200 }, 2.0f);
		ExpectRect(row->GetLayoutRect(), 600, 0, 276, 68, "box spacing and padding scale with the canvas");
		std::cout << "UI boxes PASS\n";
	}
}

namespace
{
	//***** 4-1단계: 월드 공간 캔버스

	void WorldChecks(const std::shared_ptr<UIFont>& font)
	{
		const Matrix4 canvasToLocal = MakeCanvasToLocal({ 400, 200 }, { 0.5f, 0.5f }, 0.01f);
		const Vector3 topLeft = canvasToLocal.TransformPoint({ 0, 0, 0 });
		const Vector3 bottomRight = canvasToLocal.TransformPoint({ 400, 200, 0 });
		const Vector3 center = canvasToLocal.TransformPoint({ 200, 100, 0 });
		Require(Near(topLeft.X, -2) && Near(topLeft.Y, 1) && Near(topLeft.Z, 0), "canvas top-left maps to local (-2, 1)");
		Require(Near(bottomRight.X, 2) && Near(bottomRight.Y, -1), "canvas +Y down maps to local -Y");
		Require(Near(center.X, 0) && Near(center.Y, 0), "pivot lands on the local origin");

		// 90도 시야(투영 [1][1] = 1), 화면 높이 1000: 0.01m 픽셀은 거리 d에서 5/d 화면 픽셀
		const Matrix4 projection = Matrix4::Perspective(3.14159265f * 0.5f, 16.0f / 9.0f, 0.1f, 100.0f);
		const Matrix4 view = Matrix4::Identity;
		const auto factorAt = [&](float distance) {
			const Matrix4 canvasToWorld = canvasToLocal * Matrix4::Translation({ 0, 0, distance });
			return ComputeRasterFactor(canvasToWorld, { 0, 0, distance }, view, projection, 1000.0f);
		};
		Require(Near(factorAt(5.0f), 1.0f), "1:1 density keeps the raster factor at 1");
		Require(Near(factorAt(2.5f), 2.0f) && Near(factorAt(1.25f), 4.0f), "closer canvases raster at higher factors");
		Require(Near(factorAt(3.5f), 2.0f), "density 1.43 rounds up to 2 (never below the screen)");
		Require(Near(factorAt(15.0f), 0.5f), "density 0.33 rounds up to 1/2");
		Require(Near(factorAt(4.9f), 2.0f) && Near(factorAt(5.1f), 1.0f), "rounding up switches just past 1:1");
		Require(Near(factorAt(20.0f), 0.25f) && Near(factorAt(100.0f), 0.25f), "far canvases clamp to 1/4");
		Require(Near(factorAt(0.1f), 4.0f), "near canvases clamp to 4");
		Require(Near(factorAt(-5.0f), 1.0f), "behind the camera falls back to 1");
		const Matrix4 scaled = canvasToLocal * Matrix4::Scale(2.0f) * Matrix4::Translation({ 0, 0, 5 });
		Require(Near(ComputeRasterFactor(scaled, { 0, 0, 5 }, view, projection, 1000.0f), 2.0f), "object scale counts toward density");

		// 굽는 배율 2: 글자 크기는 두 배로 굽지만 레이아웃 크기는 그대로
		UIFont::SetDefault(font);
		const auto measure = [](float raster, float& width, uint32_t& pixelSize, size_t& quads, float& quadWidth) {
			UIElement root("Root");
			SetupRoot(root);
			auto* label = root.AddChild<UIText>("Label");
			label->SetFitContent(true);
			label->SetText("Canape UI");
			label->SetFontSize(20.0f);
			root.Layout({ 0, 0, 400, 100 }, 1.0f, raster);
			width = label->GetLayoutRect().Width;
			pixelSize = label->GetPixelSize(1.0f);
			FakeDevice fake;
			UIGlyphAtlas atlas;
			atlas.BeginFrame(fake.device);
			UIDrawList list;
			UIPainter painter(list, { 0, 0, 400, 100 });
			painter.SetGlyphAtlas(&atlas);
			root.Paint(painter);
			quads = list.Vertices.size() / 4;
			float left = 1e9f, right = -1e9f;
			for (const UIVertex& v : list.Vertices)
			{
				left = std::min(left, v.Position[0]);
				right = std::max(right, v.Position[0]);
			}
			quadWidth = right - left;
		};
		float width1 = 0, width2 = 0, ink1 = 0, ink2 = 0;
		uint32_t pixels1 = 0, pixels2 = 0;
		size_t quads1 = 0, quads2 = 0;
		measure(1.0f, width1, pixels1, quads1, ink1);
		measure(2.0f, width2, pixels2, quads2, ink2);
		Require(pixels1 == 20 && pixels2 == 40, "raster factor doubles the baked pixel size");
		Require(quads1 == quads2 && quads1 == 8, "same glyph count at both factors");
		Require(std::abs(width1 - width2) <= 2.0f, "layout width independent of the raster factor");
		Require(std::abs(ink1 - ink2) <= 2.0f, "drawn text keeps its canvas size");

		// 변환이 다르면 같은 텍스처·클립이어도 배치를 나눈다.
		UIDrawList list;
		UIPainter painter(list, { 0, 0, 100, 100 });
		painter.SetTransform(5);
		painter.AddQuad({ 0, 0, 10, 10 }, { 0, 0, 1, 1 }, UIColor::White());
		painter.AddQuad({ 10, 0, 10, 10 }, { 0, 0, 1, 1 }, UIColor::White());
		painter.SetTransform(6);
		painter.AddQuad({ 20, 0, 10, 10 }, { 0, 0, 1, 1 }, UIColor::White());
		Require(list.Commands.size() == 2 && list.Commands[0].Transform == 5 && list.Commands[0].IndexCount == 12
			&& list.Commands[1].Transform == 6, "commands carry and split on the transform");

		// 박스 자식도 부모의 굽는 배율을 받는다.
		UIElement root("Root");
		SetupRoot(root);
		auto* column = root.AddChild<UIVerticalBox>("Column");
		column->SetSize({ 200, 200 });
		auto* text = column->AddChild<UIText>("Text");
		text->SetText("A");
		root.Layout({ 0, 0, 400, 400 }, 1.0f, 2.0f);
		Require(Near(text->GetRasterFactor(), 2.0f), "boxes pass the raster factor to children");
		std::cout << "UI world canvas math PASS\n";
	}
}

int main()
{
	try
	{
		ScaleChecks();
		LayoutChecks();
		PaintChecks();
		PointerChecks();
		const std::shared_ptr<UIFont> font = LoadTestFont();
		TextChecks(*font);
		AtlasChecks(*font);
		TextElementChecks(font);
		NineSliceAndClipChecks();
		BoxChecks(font);
		WorldChecks(font);
		UIFont::SetDefault(nullptr);
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
