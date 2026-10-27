#pragma once

#include <DirectXMath.h>

namespace Canape
{
	struct Vector4
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Z = 0.0f;
		float W = 0.0f;

		constexpr Vector4() = default;
		constexpr Vector4(float x, float y, float z, float w) : X(x), Y(y), Z(z), W(w) {}

		DirectX::XMVECTOR ToXM() const { return DirectX::XMVectorSet(X, Y, Z, W); }

		static Vector4 FromXM(DirectX::FXMVECTOR v)
		{
			DirectX::XMFLOAT4 result;
			DirectX::XMStoreFloat4(&result, v);
			return { result.x, result.y, result.z, result.w };
		}
	};
}
