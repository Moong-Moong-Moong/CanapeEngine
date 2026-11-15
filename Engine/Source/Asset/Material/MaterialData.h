#pragma once
#include "DX11Renderer/Data/MaterialDesc.h"
#include "DX11Renderer/Data/TextureData.h"
#include <cstddef>
#include <string>
namespace Canape
{
	// 머티리얼이 참조하는 텍스처 슬롯. MaterialAsset의 텍스처 배열 인덱스로 사용한다.
	enum class MaterialTextureSlot : size_t
	{
		BaseColor,
		MetallicRoughness, // glTF 규격: G = roughness, B = metallic (셰이더 ORM 텍스처와 같은 채널 배치)
		Normal,
		Occlusion,
		Emissive,
		Count
	};

	inline constexpr size_t kMaterialTextureSlotCount = static_cast<size_t>(MaterialTextureSlot::Count);

	// 색상 텍스처는 sRGB, 데이터 텍스처(노멀/ORM 등)는 linear UNORM으로 로드해야 한다.
	inline constexpr TextureFormat GetMaterialTextureFormat(MaterialTextureSlot slot)
	{
		return slot == MaterialTextureSlot::BaseColor || slot == MaterialTextureSlot::Emissive
			? TextureFormat::RGBA8_SRGB : TextureFormat::RGBA8_UNORM;
	}

	struct MaterialData
	{
		std::string name;
		bool doubleSided = false;
		MaterialDesc desc;
		std::string baseColorTexture;
		std::string metallicRoughnessTexture;
		std::string normalTexture;
		std::string occlusionTexture;
		std::string emissiveTexture;

		const std::string& GetTexturePath(MaterialTextureSlot slot) const
		{
			switch (slot)
			{
			case MaterialTextureSlot::BaseColor:         return baseColorTexture;
			case MaterialTextureSlot::MetallicRoughness: return metallicRoughnessTexture;
			case MaterialTextureSlot::Normal:            return normalTexture;
			case MaterialTextureSlot::Occlusion:         return occlusionTexture;
			case MaterialTextureSlot::Emissive:          return emissiveTexture;
			default:                                     break;
			}
			static const std::string empty;
			return empty;
		}
	};
}
