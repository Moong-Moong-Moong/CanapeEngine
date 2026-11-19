#pragma once

#include "DX11Renderer/Resource/DX11ResourceId.h"
#include "UI/UITypes.h"

#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace Canape
{
	class UIFont;

	//***** UI가 GPU 텍스처를 만들고 고칠 때 쓰는 통로(Application이 Renderer로 연결, 테스트는 가짜로 대체)
	struct UITextureDevice
	{
		// RGBA8_UNORM, 꽉 채운 width * height * 4 바이트
		std::function<TextureResourceId(uint32_t width, uint32_t height, std::span<const uint8_t> rgba)> Create;
		std::function<bool(TextureResourceId texture, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
			std::span<const uint8_t> rgba)> Update;
	};

	//*** 아틀라스 안의 글리프 하나(픽셀 크기 하나 기준)
	struct UIAtlasGlyph
	{
		UIRect UV;           // 아틀라스 안 영역(0~1)
		int32_t OffsetX = 0; // 베이스라인 원점에서 비트맵 왼쪽 위까지
		int32_t OffsetY = 0;
		uint32_t Width = 0;  // 0이면 그릴 것 없음(공백)
		uint32_t Height = 0;
	};

	//***** 필요한 글리프만 그때그때 굽는 텍스처 아틀라스(한 줄씩 채우는 shelf 방식)
	//*** 흰색 + 알파(coverage)로 저장하므로 정점 색이 곧 글자 색이 된다.
	//*** 가득 차면 다음 BeginFrame에 비우고 다시 굽는다. 그 프레임에 자리가 없던 글자는 한 프레임 빠진다.
	class UIGlyphAtlas
	{
	public:
		static constexpr uint32_t kSize = 1024;
		static constexpr uint32_t kPadding = 1;
		static constexpr uint32_t kWhiteBlock = 4; // 왼쪽 위에 단색용 흰 블록

		UIGlyphAtlas();

		//*** 프레임 시작: 예약된 비우기 처리, GPU 텍스처가 없으면 만든다.
		void BeginFrame(const UITextureDevice& device);
		//*** 이번 프레임에 새로 구운 영역을 GPU에 올린다(바뀐 영역을 감싸는 사각형 한 번).
		void Flush(const UITextureDevice& device);

		//*** 없으면 구워서 넣는다. 자리가 없으면 nullptr.
		const UIAtlasGlyph* GetGlyph(const UIFont& font, uint32_t pixelSize, uint32_t glyph);

		TextureResourceId GetTexture() const { return m_Texture; }
		//*** 단색 사각형에 쓸 흰 텍셀 중심 UV
		Vector2 GetWhiteUV() const;

		size_t GetGlyphCount() const { return m_Glyphs.size(); }
		bool IsResetPending() const { return m_ResetPending; }
		std::span<const uint8_t> GetPixels() const { return m_Pixels; }

	private:
		void Reset();
		bool Allocate(uint32_t width, uint32_t height, uint32_t& x, uint32_t& y);
		void MarkDirty(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

		std::vector<uint8_t> m_Pixels; // RGBA8, kSize * kSize
		std::unordered_map<uint64_t, UIAtlasGlyph> m_Glyphs;
		TextureResourceId m_Texture;
		uint32_t m_ShelfX = 0;
		uint32_t m_ShelfY = 0;
		uint32_t m_ShelfHeight = 0;
		uint32_t m_DirtyLeft = 0, m_DirtyTop = 0, m_DirtyRight = 0, m_DirtyBottom = 0;
		bool m_ResetPending = false;
	};
}
