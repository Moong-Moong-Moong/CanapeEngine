#pragma once

#include "Math/MathUtility.h"

#include <DirectXMath.h>

namespace Canape
{
	struct Vector2
	{
		float X = 0.0f;
		float Y = 0.0f;

		constexpr Vector2() = default;
		constexpr Vector2(float x, float y) : X(x), Y(y) {}

		static const Vector2 Zero;

		DirectX::XMVECTOR ToXM() const { return DirectX::XMVectorSet(X, Y, 0.0f, 0.0f); }

		static Vector2 FromXM(DirectX::FXMVECTOR v)
		{
			DirectX::XMFLOAT2 result;
			DirectX::XMStoreFloat2(&result, v);
			return { result.x, result.y };
		}

		constexpr Vector2 operator+(const Vector2& rhs) const { return { X + rhs.X, Y + rhs.Y }; }
		constexpr Vector2 operator-(const Vector2& rhs) const { return { X - rhs.X, Y - rhs.Y }; }
		constexpr Vector2 operator*(float scalar) const { return { X * scalar, Y * scalar }; }
		constexpr Vector2 operator/(float scalar) const { return { X / scalar, Y / scalar }; }

		float Length() const { return DirectX::XMVectorGetX(DirectX::XMVector2Length(ToXM())); }
	};

	inline constexpr Vector2 Vector2::Zero{ 0.0f, 0.0f };
}
