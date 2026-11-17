// Standalone CPU test for the UI core (layout, painting, pointer routing); not part of Runtime.vcxproj.
// Compiled together with the UI sources by ValidateUI.ps1. No renderer, scene or RTTR dependency.
#include "UI/UIButton.h"
#include "UI/UIElement.h"
#include "UI/UIImage.h"
#include "UI/UIPainter.h"
#include "UI/UIPointerRouter.h"

#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

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

int main()
{
	try
	{
		ScaleChecks();
		LayoutChecks();
		PaintChecks();
		PointerChecks();
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "FAIL: " << e.what() << '\n';
		return 1;
	}
}
