#pragma once

#include "Asset/AssetId.h"

#include <span>
#include <string>

namespace Canape
{
	class MaterialAsset;
	class MeshAsset;
	class TextureAsset;

	//***** GPU에게 자원 할당과 해제를 요청하는 인터페이스
	class AssetGpuUploader
	{
	public:
		virtual ~AssetGpuUploader() = default;

		virtual bool UploadMesh(const MeshAsset& mesh) = 0;
		virtual bool UploadTexture(const TextureAsset& texture) = 0;
		virtual bool UploadMaterial(const MaterialAsset& material) = 0;

		virtual void ReleaseMeshes(std::span<const MeshAssetId> meshes) = 0;
		virtual void ReleaseTextures(std::span<const TextureAssetId> textures) = 0;
		virtual void ReleaseMaterials(std::span<const MaterialAssetId> materials) = 0;
	};
}
