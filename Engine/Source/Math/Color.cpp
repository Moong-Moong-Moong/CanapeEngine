#include "Math/Color.h"

namespace Canape
{
	namespace
	{
		uint8_t ToByte(float value)
		{
			return static_cast<uint8_t>(Math::Round(Math::Saturate(value) * 255.0f));
		}
	}

	float Color::SRGBToLinear(float value)
	{
		return value <= 0.04045f ? value / 12.92f : Math::Pow((value + 0.055f) / 1.055f, 2.4f);
	}

	float Color::LinearToSRGB(float value)
	{
		return value <= 0.0031308f ? value * 12.92f : 1.055f * Math::Pow(value, 1.0f / 2.4f) - 0.055f;
	}

	Color Color::FromHSV(float hueDegrees, float saturation, float value, float alpha)
	{
		const float h = Math::Wrap(hueDegrees, 0.0f, 360.0f) / 60.0f;
		const float c = value * saturation;
		const float x = c * (1.0f - Math::Abs(Math::Fmod(h, 2.0f) - 1.0f));
		const float m = value - c;

		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;

		switch (static_cast<int>(h))
		{
		case 0: r = c; g = x; break;
		case 1: r = x; g = c; break;
		case 2: g = c; b = x; break;
		case 3: g = x; b = c; break;
		case 4: r = x; b = c; break;
		default: r = c; b = x; break;
		}

		return { r + m, g + m, b + m, alpha };
	}

	Color Color::FromSRGB(const Color32& color)
	{
		return {
			SRGBToLinear(color.R / 255.0f),
			SRGBToLinear(color.G / 255.0f),
			SRGBToLinear(color.B / 255.0f),
			color.A / 255.0f
		};
	}

	Color32 Color::ToSRGB() const
	{
		return {
			ToByte(LinearToSRGB(R)),
			ToByte(LinearToSRGB(G)),
			ToByte(LinearToSRGB(B)),
			ToByte(A)
		};
	}
}
