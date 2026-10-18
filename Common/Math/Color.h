#pragma once

#include "Math/MathUtility.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"

#include <cstdint>

namespace Canape
{
	struct Color32;

	struct Color
	{
		float R = 0.0f;
		float G = 0.0f;
		float B = 0.0f;
		float A = 1.0f;

		constexpr Color() = default;
		constexpr Color(float r, float g, float b, float a = 1.0f) : R(r), G(g), B(b), A(a) {}

		static const Color White;
		static const Color Black;
		static const Color Gray;
		static const Color Red;
		static const Color Green;
		static const Color Blue;
		static const Color Yellow;
		static const Color Cyan;
		static const Color Magenta;
		static const Color Transparent;

		constexpr Color operator+(const Color& rhs) const { return { R + rhs.R, G + rhs.G, B + rhs.B, A + rhs.A }; }
		constexpr Color operator-(const Color& rhs) const { return { R - rhs.R, G - rhs.G, B - rhs.B, A - rhs.A }; }
		constexpr Color operator*(const Color& rhs) const { return { R * rhs.R, G * rhs.G, B * rhs.B, A * rhs.A }; }
		constexpr Color operator*(float scalar) const { return { R * scalar, G * scalar, B * scalar, A * scalar }; }

		constexpr bool operator==(const Color& rhs) const = default;

		constexpr Vector3 ToVector3() const { return { R, G, B }; }
		constexpr Vector4 ToVector4() const { return { R, G, B, A }; }

		constexpr Color WithAlpha(float alpha) const { return { R, G, B, alpha }; }

		static constexpr Color Lerp(const Color& a, const Color& b, float t)
		{
			return {
				Math::Lerp(a.R, b.R, t),
				Math::Lerp(a.G, b.G, t),
				Math::Lerp(a.B, b.B, t),
				Math::Lerp(a.A, b.A, t)
			};
		}

		static Color FromHSV(float hueDegrees, float saturation, float value, float alpha = 1.0f);
		static Color FromSRGB(const Color32& color);
		Color32 ToSRGB() const;

		static float SRGBToLinear(float value);
		static float LinearToSRGB(float value);
	};

	struct Color32
	{
		uint8_t R = 0;
		uint8_t G = 0;
		uint8_t B = 0;
		uint8_t A = 255;

		constexpr Color32() = default;
		constexpr Color32(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : R(r), G(g), B(b), A(a) {}

		constexpr bool operator==(const Color32& rhs) const = default;

		static constexpr Color32 FromHex(uint32_t rgba)
		{
			return {
				static_cast<uint8_t>((rgba >> 24) & 0xFF),
				static_cast<uint8_t>((rgba >> 16) & 0xFF),
				static_cast<uint8_t>((rgba >> 8) & 0xFF),
				static_cast<uint8_t>(rgba & 0xFF)
			};
		}

		constexpr uint32_t ToHex() const
		{
			return (static_cast<uint32_t>(R) << 24) | (static_cast<uint32_t>(G) << 16) | (static_cast<uint32_t>(B) << 8) | static_cast<uint32_t>(A);
		}

		Color ToLinear() const { return Color::FromSRGB(*this); }
	};

	inline constexpr Color Color::White{ 1.0f, 1.0f, 1.0f, 1.0f };
	inline constexpr Color Color::Black{ 0.0f, 0.0f, 0.0f, 1.0f };
	inline constexpr Color Color::Gray{ 0.5f, 0.5f, 0.5f, 1.0f };
	inline constexpr Color Color::Red{ 1.0f, 0.0f, 0.0f, 1.0f };
	inline constexpr Color Color::Green{ 0.0f, 1.0f, 0.0f, 1.0f };
	inline constexpr Color Color::Blue{ 0.0f, 0.0f, 1.0f, 1.0f };
	inline constexpr Color Color::Yellow{ 1.0f, 1.0f, 0.0f, 1.0f };
	inline constexpr Color Color::Cyan{ 0.0f, 1.0f, 1.0f, 1.0f };
	inline constexpr Color Color::Magenta{ 1.0f, 0.0f, 1.0f, 1.0f };
	inline constexpr Color Color::Transparent{ 0.0f, 0.0f, 0.0f, 0.0f };
}
