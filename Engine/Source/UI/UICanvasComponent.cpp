#include "UI/UICanvasComponent.h"

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

	void UICanvasComponent::Layout(const Vector2& targetSize)
	{
		m_Root->FlushRemovals();
		m_Scale = ComputeCanvasScale(targetSize, m_ReferenceResolution, m_MatchWidthOrHeight);
		m_Root->Layout({ 0.0f, 0.0f, targetSize.X, targetSize.Y }, m_Scale);
	}
}
