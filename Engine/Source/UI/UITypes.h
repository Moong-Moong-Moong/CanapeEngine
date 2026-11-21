#pragma once

#include "Math/Vector2.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Canape
{
	//***** UI 좌표: 렌더 타깃 픽셀, 좌상단 원점, +Y 아래
	struct UIRect
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Width = 0.0f;
		float Height = 0.0f;

		float Right() const { return X + Width; }
		float Bottom() const { return Y + Height; }
		bool IsEmpty() const { return Width <= 0.0f || Height <= 0.0f; }

		// 오른쪽·아래 경계는 포함하지 않는다(픽셀 중심 규칙과 같음).
		bool Contains(const Vector2& point) const
		{
			return point.X >= X && point.Y >= Y && point.X < Right() && point.Y < Bottom();
		}

		bool operator==(const UIRect&) const = default;
	};

	//***** 네 변의 두께(여백, 9-slice 테두리 등)
	struct UIEdges
	{
		float Left = 0.0f;
		float Top = 0.0f;
		float Right = 0.0f;
		float Bottom = 0.0f;

		constexpr UIEdges() = default;
		constexpr UIEdges(float all) : Left(all), Top(all), Right(all), Bottom(all) {}
		constexpr UIEdges(float left, float top, float right, float bottom) : Left(left), Top(top), Right(right), Bottom(bottom) {}

		constexpr float Horizontal() const { return Left + Right; }
		constexpr float Vertical() const { return Top + Bottom; }
		bool operator==(const UIEdges&) const = default;
	};

	//*** 두 사각형의 교집합(겹치지 않으면 크기 0)
	inline UIRect Intersect(const UIRect& a, const UIRect& b)
	{
		const float left = std::max(a.X, b.X);
		const float top = std::max(a.Y, b.Y);
		const float right = std::min(a.Right(), b.Right());
		const float bottom = std::min(a.Bottom(), b.Bottom());
		return { left, top, std::max(right - left, 0.0f), std::max(bottom - top, 0.0f) };
	}

	//***** sRGB 8비트 색, 알파는 straight
	struct UIColor
	{
		uint8_t R = 255;
		uint8_t G = 255;
		uint8_t B = 255;
		uint8_t A = 255;

		constexpr UIColor() = default;
		constexpr UIColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : R(r), G(g), B(b), A(a) {}

		static constexpr UIColor White() { return {}; }

		// UIVertex::Color 형식(R이 하위 바이트)
		constexpr uint32_t Pack() const
		{
			return uint32_t(R) | (uint32_t(G) << 8) | (uint32_t(B) << 16) | (uint32_t(A) << 24);
		}

		// 채널별 곱(틴트)
		constexpr UIColor operator*(const UIColor& rhs) const
		{
			return { Mul(R, rhs.R), Mul(G, rhs.G), Mul(B, rhs.B), Mul(A, rhs.A) };
		}

		bool operator==(const UIColor&) const = default;

	private:
		static constexpr uint8_t Mul(uint8_t a, uint8_t b)
		{
			return static_cast<uint8_t>((uint32_t(a) * b + 127) / 255);
		}
	};

	//***** 캔버스 스케일: 기준 해상도 대비 타깃 크기. match 0 = 너비 기준, 1 = 높이 기준.
	//*** 둘 사이는 로그 공간에서 섞어서 화면비가 달라도 한쪽으로 치우치지 않게 한다.
	inline float ComputeCanvasScale(const Vector2& targetSize, const Vector2& referenceResolution, float matchWidthOrHeight)
	{
		if (referenceResolution.X <= 0.0f || referenceResolution.Y <= 0.0f || targetSize.X <= 0.0f || targetSize.Y <= 0.0f)
		{
			return 1.0f;
		}
		const float match = std::clamp(matchWidthOrHeight, 0.0f, 1.0f);
		const float widthLog = std::log2(targetSize.X / referenceResolution.X);
		const float heightLog = std::log2(targetSize.Y / referenceResolution.Y);
		return std::exp2(widthLog + (heightLog - widthLog) * match);
	}
}
