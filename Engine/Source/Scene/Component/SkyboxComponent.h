#pragma once

#include "Scene/Component/Component.h"
#include "Asset/AssetManager.h"
#include "DX11Renderer/Scene/RenderScene.h"
#include "Math/Vector3.h"

#include <string>

namespace Canape
{
	class SkyboxComponent final : public Component
	{
	public:
		SkyboxComponent() = default;

		bool SetCubemap(const std::string& path);
		const CubemapAssetHandle& GetCubemap() const { return m_Cubemap; }

		float GetBrightness() const { return m_Brightness; }
		void SetBrightness(float brightness) { m_Brightness = brightness; }

		float GetRotation() const { return m_RotationRadians; }
		void SetRotation(float radians) { m_RotationRadians = radians; }

		const Vector3& GetAmbientColor() const { return m_AmbientColor; }
		void SetAmbientColor(const Vector3& color) { m_AmbientColor = color; }

		float GetAmbientIntensity() const { return m_AmbientIntensity; }
		void SetAmbientIntensity(float intensity) { m_AmbientIntensity = intensity; }

		RenderSkybox BuildRenderSkybox() const;

	private:
		CubemapAssetHandle m_Cubemap;
		float m_Brightness = 1.0f;
		float m_RotationRadians = 0.0f;
		Vector3 m_AmbientColor{ 1.0f, 1.0f, 1.0f };
		float m_AmbientIntensity = 0.1f;
	};
}
