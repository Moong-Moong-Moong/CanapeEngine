#include "UI/UISystem.h"
#include "Asset/AssetManager.h"
#include "CNDebug/DebugLog.h"
#include "Input/InputManager.h"
#include "Scene/Scene.h"
#include "UI/Text/UIFont.h"
#include "UI/UICanvasComponent.h"
#include "UI/UIWorld.h"

#include <cmath>

#include <algorithm>

namespace Canape
{
	void UISystem::SetPointerViewport(const UIRect& clientRect, bool active)
	{
		m_PointerViewport = clientRect;
		m_UsePointerViewport = true;
		m_PointerViewportActive = active;
	}

	void UISystem::ResetPointerViewport()
	{
		m_PointerViewport = {};
		m_UsePointerViewport = false;
		m_PointerViewportActive = true;
	}

	std::vector<UICanvasComponent*> UISystem::CollectCanvases(Scene* scene, UICanvasMode mode) const
	{
		std::vector<UICanvasComponent*> canvases;
		if (!scene)
		{
			return canvases;
		}
		for (UICanvasComponent* canvas : scene->GetUICanvases())
		{
			if (Scene::IsRunnable(canvas) && canvas->GetMode() == mode)
			{
				canvases.push_back(canvas);
			}
		}
		std::ranges::stable_sort(canvases, {}, &UICanvasComponent::GetSortOrder);
		return canvases;
	}

	UIPointerState UISystem::MapPointer(const InputManager& input) const
	{
		const Input& raw = input.GetInput();
		UIPointerState pointer;
		pointer.PrimaryDown = raw.MouseButtons[static_cast<size_t>(MouseButton::Left)];

		Vector2 position{ static_cast<float>(raw.MousePosition.X), static_cast<float>(raw.MousePosition.Y) };
		if (m_UsePointerViewport)
		{
			if (!m_PointerViewportActive || m_PointerViewport.IsEmpty())
			{
				return pointer;
			}
			position = position - Vector2{ m_PointerViewport.X, m_PointerViewport.Y };
			position.X *= m_TargetSize.X / m_PointerViewport.Width;
			position.Y *= m_TargetSize.Y / m_PointerViewport.Height;
		}
		pointer.Position = position;
		pointer.Inside = UIRect{ 0.0f, 0.0f, m_TargetSize.X, m_TargetSize.Y }.Contains(position);
		return pointer;
	}

	bool UISystem::GetPointerNdc(const InputManager& input, float& ndcX, float& ndcY) const
	{
		const UIPointerState pointer = MapPointer(input);
		if (!pointer.Inside || m_TargetSize.X <= 0.0f || m_TargetSize.Y <= 0.0f)
		{
			return false;
		}
		ndcX = pointer.Position.X / m_TargetSize.X * 2.0f - 1.0f;
		ndcY = 1.0f - pointer.Position.Y / m_TargetSize.Y * 2.0f;
		return true;
	}

	void UISystem::ProcessInput(Scene* scene, InputManager& input)
	{
		if (scene != m_LastScene)
		{
			m_Router.Reset();
			m_LastScene = scene;
		}

		// 레이아웃은 직전 BuildDrawList 결과를 쓴다(화면에 보인 그대로 판정).
		m_Roots.clear();
		for (UICanvasComponent* canvas : CollectCanvases(scene, UICanvasMode::ScreenSpace))
		{
			m_Roots.push_back(&canvas->GetRoot());
		}
		m_Router.Update(m_Roots, MapPointer(input));
		m_Roots.clear(); // 콜백 이후 캔버스가 사라질 수 있으므로 보관하지 않는다.

		input.SetPointerCapturedByUI(m_Router.IsCapturingPointer());
	}

	void UISystem::EnsureDefaultFont()
	{
		if (m_DefaultFontTried || UIFont::GetDefault())
		{
			return;
		}
		m_DefaultFontTried = true;
		std::string error;
		std::shared_ptr<UIFont> font = UIFont::LoadFromFile(AssetManager::GetAssetFile(kDefaultFontPath), error);
		if (!font)
		{
			CN_LOG_RUNTIME("UI", "Default font load failed: ", error);
			return;
		}
		UIFont::SetDefault(std::move(font));
	}

	void UISystem::PaintWorldCanvases(const std::vector<UICanvasComponent*>& canvases, const RenderView& view,
		const UITextureResolver& resolver)
	{
		struct WorldCanvas
		{
			UICanvasComponent* Canvas;
			Matrix4 CanvasToWorld;
			Vector3 Center;
			float Depth;
		};
		std::vector<WorldCanvas> items;
		items.reserve(canvases.size());
		for (UICanvasComponent* canvas : canvases)
		{
			const Vector2& size = canvas->GetCanvasSize();
			if (size.X <= 0.0f || size.Y <= 0.0f)
			{
				continue;
			}
			const Matrix4 canvasToWorld = canvas->GetCanvasToWorld();
			const Vector2& pivot = canvas->GetCanvasPivot();
			const Vector3 center = canvasToWorld.TransformPoint({ size.X * pivot.X, size.Y * pivot.Y, 0.0f });
			items.push_back({ canvas, canvasToWorld, center, view.view.TransformPoint(center).Z });
		}
		// 반투명끼리 올바르게 겹치도록 먼 캔버스부터 그린다(같은 거리면 정렬 순서).
		std::ranges::stable_sort(items, std::ranges::greater{}, &WorldCanvas::Depth);

		const Matrix4 viewProjection = view.view * view.projection;
		for (const WorldCanvas& item : items)
		{
			const float raster = ComputeRasterFactor(item.CanvasToWorld, item.Center, view.view, view.projection, m_TargetSize.Y);
			item.Canvas->LayoutWorld(raster);

			UIDrawTransform transform;
			const Matrix4 clipFromCanvas = item.CanvasToWorld * viewProjection;
			std::copy_n(clipFromCanvas.Data(), 16, transform.ClipFromCanvas);
			transform.DepthTest = item.Canvas->GetDepthTest();
			const auto index = static_cast<uint32_t>(m_DrawList.Transforms.size());
			m_DrawList.Transforms.push_back(transform);

			// 클립·컬링은 캔버스 픽셀 기준이다(화면 시저로 바꿀 수 없으므로 CPU에서만 잘라 낸다).
			const Vector2& size = item.Canvas->GetCanvasSize();
			const UIClipRect canvasRect{ 0, 0, static_cast<int32_t>(std::ceil(size.X)), static_cast<int32_t>(std::ceil(size.Y)) };
			UIPainter painter(m_DrawList, canvasRect, &resolver);
			painter.SetTransform(index);
			painter.SetGlyphAtlas(&m_Atlas);
			painter.SetSolidSource(m_Atlas.GetTexture(), m_Atlas.GetWhiteUV());
			item.Canvas->GetRoot().Paint(painter);
		}
	}

	void UISystem::BuildDrawList(Scene* scene, uint32_t targetWidth, uint32_t targetHeight, const UITextureResolver& resolver,
		const UITextureDevice& device, const RenderView* view)
	{
		m_DrawList.Clear();
		m_TargetSize = { static_cast<float>(targetWidth), static_cast<float>(targetHeight) };
		const std::vector<UICanvasComponent*> screenCanvases = CollectCanvases(scene, UICanvasMode::ScreenSpace);
		std::vector<UICanvasComponent*> worldCanvases;
		if (view)
		{
			worldCanvases = CollectCanvases(scene, UICanvasMode::WorldSpace);
		}
		if (targetWidth == 0 || targetHeight == 0 || (screenCanvases.empty() && worldCanvases.empty()))
		{
			return;
		}

		EnsureDefaultFont();
		m_Atlas.BeginFrame(device);

		if (!worldCanvases.empty())
		{
			PaintWorldCanvases(worldCanvases, *view, resolver);
		}

		const UIClipRect fullTarget{ 0, 0, static_cast<int32_t>(targetWidth), static_cast<int32_t>(targetHeight) };
		UIPainter painter(m_DrawList, fullTarget, &resolver);
		painter.SetGlyphAtlas(&m_Atlas);
		// 단색도 아틀라스의 흰 텍셀로 그려서 글자와 한 드로우로 묶는다.
		painter.SetSolidSource(m_Atlas.GetTexture(), m_Atlas.GetWhiteUV());
		for (UICanvasComponent* canvas : screenCanvases)
		{
			canvas->Layout(m_TargetSize);
			canvas->GetRoot().Paint(painter);
		}

		// 이번 프레임에 새로 구운 글자를 그리기 전에 올린다.
		m_Atlas.Flush(device);
	}
}
