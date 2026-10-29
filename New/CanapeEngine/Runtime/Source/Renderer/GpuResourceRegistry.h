#pragma once
#include "Asset/AssetHandle.h"
#include "DX11Renderer.h"
#include <span>
#include <unordered_map>
namespace Canape
{
	class GpuResourceRegistry
	{
	public:
		explicit GpuResourceRegistry(DX11Renderer& renderer);

		GpuResourceRegistry(const GpuResourceRegistry&) = delete;
		GpuResourceRegistry& operator=(const GpuResourceRegistry&) = delete;

		bool UpLoadMeshResource();

		MeshResourceId CreateMeshResource(const MeshAsset& mesh);
		TextureResourceId CreateTextureResource(const TextureAsset& texture);
		MaterialResourceId CreateMaterialResource(const MaterialAsset& material);

		MeshResourceId GetResourceId(MeshAssetId assetId) const;
		TextureResourceId GetResourceId(TextureAssetId assetId) const;
		MaterialResourceId GetResourceId(MaterialAssetId assetId) const;

		void ReleaseMeshes(std::span<const MeshAssetId> meshes);
		void ReleaseTextures(std::span<const TextureAssetId> textures);
		void ReleaseMaterials(std::span<const MaterialAssetId> materials);

	private:
		DX11Renderer& m_Renderer;

		std::unordered_map<MeshAssetId, MeshResourceId, AssetIdHash<MeshAsset>> m_mesh;
		std::unordered_map<TextureAssetId, TextureResourceId, AssetIdHash<TextureAsset>> m_texture;
		std::unordered_map<MaterialAssetId, MaterialResourceId, AssetIdHash<MaterialAsset>> m_material;
	};
}
