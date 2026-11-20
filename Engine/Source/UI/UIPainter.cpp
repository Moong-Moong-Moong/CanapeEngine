#include "UI/UIPainter.h"

#include <cmath>

namespace Canape
{
	UIPainter::UIPainter(UIDrawList& list, const UIClipRect& clip, const UITextureResolver* resolver)
		: m_List(list)
		, m_ClipStack{ clip }
		, m_Resolver(resolver)
	{
	}

	void UIPainter::PushClip(const UIRect& rect)
	{
		const UIClipRect& current = m_ClipStack.back();
		UIClipRect next;
		next.Left = std::max(current.Left, static_cast<int32_t>(std::floor(rect.X)));
		next.Top = std::max(current.Top, static_cast<int32_t>(std::floor(rect.Y)));
		next.Right = std::min(current.Right, static_cast<int32_t>(std::ceil(rect.Right())));
		next.Bottom = std::min(current.Bottom, static_cast<int32_t>(std::ceil(rect.Bottom())));
		next.Right = std::max(next.Right, next.Left);
		next.Bottom = std::max(next.Bottom, next.Top);
		m_ClipStack.push_back(next);
	}

	void UIPainter::PopClip()
	{
		if (m_ClipStack.size() > 1)
		{
			m_ClipStack.pop_back();
		}
	}

	void UIPainter::SetSolidSource(TextureResourceId texture, const Vector2& uv)
	{
		m_SolidTexture = texture;
		m_SolidUV = uv;
	}

	void UIPainter::AddQuad(const UIRect& rect, const UIRect& uv, UIColor color, TextureAssetId texture)
	{
		if (!texture.IsValid())
		{
			if (m_SolidTexture.IsValid())
			{
				Emit(rect, { m_SolidUV.X, m_SolidUV.Y, 0.0f, 0.0f }, color, m_SolidTexture);
			}
			else
			{
				Emit(rect, uv, color, {}); // 렌더러의 흰 텍스처
			}
			return;
		}
		const TextureResourceId resource = m_Resolver && *m_Resolver ? (*m_Resolver)(texture) : TextureResourceId{};
		if (resource.IsValid())
		{
			Emit(rect, uv, color, resource);
		}
	}

	void UIPainter::AddQuadResource(const UIRect& rect, const UIRect& uv, UIColor color, TextureResourceId texture)
	{
		if (texture.IsValid())
		{
			Emit(rect, uv, color, texture);
		}
	}

	void UIPainter::Emit(const UIRect& rect, const UIRect& uv, UIColor color, TextureResourceId texture)
	{
		if (rect.IsEmpty() || color.A == 0)
		{
			return;
		}
		const UIClipRect& clip = m_ClipStack.back();
		if (clip.Right <= clip.Left || clip.Bottom <= clip.Top
			|| rect.Right() <= static_cast<float>(clip.Left) || rect.X >= static_cast<float>(clip.Right)
			|| rect.Bottom() <= static_cast<float>(clip.Top) || rect.Y >= static_cast<float>(clip.Bottom))
		{
			return; // 클립 밖
		}

		const auto base = static_cast<uint32_t>(m_List.Vertices.size());
		const uint32_t packed = color.Pack();
		m_List.Vertices.push_back({ { rect.X, rect.Y }, { uv.X, uv.Y }, packed });
		m_List.Vertices.push_back({ { rect.Right(), rect.Y }, { uv.Right(), uv.Y }, packed });
		m_List.Vertices.push_back({ { rect.Right(), rect.Bottom() }, { uv.Right(), uv.Bottom() }, packed });
		m_List.Vertices.push_back({ { rect.X, rect.Bottom() }, { uv.X, uv.Bottom() }, packed });

		const auto indexOffset = static_cast<uint32_t>(m_List.Indices.size());
		m_List.Indices.insert(m_List.Indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });

		if (!m_List.Commands.empty())
		{
			UIDrawCommand& last = m_List.Commands.back();
			if (last.Texture == texture && last.Clip == clip && last.Transform == m_Transform
				&& last.IndexOffset + last.IndexCount == indexOffset)
			{
				last.IndexCount += 6;
				return;
			}
		}
		m_List.Commands.push_back({ texture, clip, indexOffset, 6, m_Transform });
	}
}
