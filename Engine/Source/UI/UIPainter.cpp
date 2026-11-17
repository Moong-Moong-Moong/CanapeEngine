#include "UI/UIPainter.h"

namespace Canape
{
	UIPainter::UIPainter(UIDrawList& list, const UIClipRect& clip, const UITextureResolver* resolver)
		: m_List(list)
		, m_Clip(clip)
		, m_Resolver(resolver)
	{
	}

	void UIPainter::AddQuad(const UIRect& rect, const UIRect& uv, UIColor color, TextureAssetId texture)
	{
		if (rect.IsEmpty() || color.A == 0)
		{
			return;
		}

		TextureResourceId resource;
		if (texture.IsValid())
		{
			resource = m_Resolver && *m_Resolver ? (*m_Resolver)(texture) : TextureResourceId{};
			if (!resource.IsValid())
			{
				return;
			}
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
			if (last.Texture == resource && last.Clip == m_Clip && last.IndexOffset + last.IndexCount == indexOffset)
			{
				last.IndexCount += 6;
				return;
			}
		}
		m_List.Commands.push_back({ resource, m_Clip, indexOffset, 6 });
	}
}
