#include "UI/UICanvasComponent.h"
#include "Scene/Component/TransformComponent.h"
#include "UI/UIWorld.h"

namespace Canape
{
	UICanvasComponent::UICanvasComponent()
		: m_Root(std::make_unique<UIElement>("Root"))
	{
		m_Root->SetAnchors({ 0.0f, 0.0f }, { 1.0f, 1.0f });
		m_Root->SetSize({ 0.0f, 0.0f });
	}

	UICanvasComponent::UICanvasComponent(const Vector2& referenceResolution)
		: UICanvasComponent()
	{
		m_ReferenceResolution = referenceResolution;
	}

	UICanvasComponent::~UICanvasComponent() = default;

	Matrix4 UICanvasComponent::GetCanvasToWorld() const
	{
		const Matrix4 canvasToLocal = MakeCanvasToLocal(m_CanvasSize, m_CanvasPivot, m_WorldUnitsPerPixel);
		const TransformComponent* transform = GetTransform();
		return transform ? canvasToLocal * transform->GetLocalToWorldMatrix() : canvasToLocal;
	}

	void UICanvasComponent::Layout(const Vector2& targetSize)
	{
		m_Root->FlushRemovals();
		m_Scale = ComputeCanvasScale(targetSize, m_ReferenceResolution, m_MatchWidthOrHeight);
		m_RasterFactor = 1.0f;
		m_Root->Layout({ 0.0f, 0.0f, targetSize.X, targetSize.Y }, m_Scale);
	}

	void UICanvasComponent::LayoutWorld(float rasterFactor)
	{
		m_Root->FlushRemovals();
		m_Scale = 1.0f; // 월드 캔버스는 캔버스 픽셀 그대로 배치한다.
		m_RasterFactor = rasterFactor;
		m_Root->Layout({ 0.0f, 0.0f, m_CanvasSize.X, m_CanvasSize.Y }, 1.0f, rasterFactor);
	}
}
