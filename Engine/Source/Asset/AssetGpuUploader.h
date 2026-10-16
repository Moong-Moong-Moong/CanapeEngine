#pragma once

#include "Asset/AssetId.h"

#include <span>
#include <string>

namespace Canape
{
	class MaterialAsset;
	class MeshAsset;
	class TextureAsset;

	// GPU side of asset loading, implemented by the renderer. AssetManager uploads at
	// CPU load time and requests releases by asset ID, without depending on Renderer.
	// Upload and release order between dependent assets is the caller's responsibility:
	// a material's texture must be uploaded first and released after it.
	class AssetGpuUploader
	{
	public:
		virtual ~AssetGpuUploader() = default;

		// Each Upload creates GPU resources for a newly loaded asset. Returns false and sets error on failure.
		virtual bool UploadMesh(const MeshAsset& mesh, std::string& error) = 0;
		virtual bool UploadTexture(const TextureAsset& texture, std::string& error) = 0;
		virtual bool UploadMaterial(const MaterialAsset& material, std::string& error) = 0;

		// Each Release destroys GPU resources of unloaded assets. Unknown IDs are ignored.
		virtual void ReleaseMeshes(std::span<const MeshAssetId> meshes) = 0;
		virtual void ReleaseTextures(std::span<const TextureAssetId> textures) = 0;
		virtual void ReleaseMaterials(std::span<const MaterialAssetId> materials) = 0;
	};
}
