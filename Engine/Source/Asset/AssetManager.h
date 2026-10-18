#pragma once

#include "Asset/AssetGpuUploader.h"
#include "Asset/AssetHandle.h"
#include "Asset/AssetTracker.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/TextureAsset.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace Canape
{
	template <typename T>
	struct AssetEntry
	{
		std::unique_ptr<T> asset;
		std::string path;    // Resolved absolute path (material: its name), for display.
		std::string pathKey; // Lookup key in the by-path (material: by-name) map.
		uint32_t referenceCount = 0;
		bool unloadRequested = false; // Set when referenceCount reaches zero; cleared on re-acquire.
	};

	template <typename T>
	using AssetMap = std::unordered_map<AssetId<T>, AssetEntry<T>, AssetIdHash<T>>;

	// Owns CPU assets, reference counted through AssetHandle. Every asset is uploaded to the
	// GPU when it is loaded or created, and released only through the ReleaseUnused methods.
	// Single-threaded. Must outlive every handle it issued.
	class AssetManager
	{
	public:
		explicit AssetManager(AssetGpuUploader& gpuUploader);

		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		// UTF-8 path. Loads and uploads the mesh on first use, then adds a reference.
		// Unsupported extensions return an invalid handle and set outError.
		// Any other load or upload failure returns only an invalid handle.
		MeshAssetHandle GetMeshHandle(const std::string& path, const AssetOwnerInfo& owner, std::string* outError = nullptr);
		// Null for invalid or released handles.
		const MeshAsset* GetMesh(const MeshAssetHandle& handle) const;

		// Same rules as GetMeshHandle. Images are converted to RGBA8 and treated as sRGB BaseColor data.
		TextureAssetHandle GetTextureHandle(const std::string& path, const AssetOwnerInfo& owner, std::string* outError = nullptr);
		const TextureAsset* GetTexture(const TextureAssetHandle& handle) const;

		// Creates an immutable material keyed by name and returns the creator's reference.
		// baseColorTexture may be invalid (white); otherwise it must already be loaded, and the
		// material keeps its own reference to it. Empty or duplicate names set outError.
		MaterialAssetHandle CreateMaterial(const std::string& name, const MaterialDesc& desc,
			const TextureAssetHandle& baseColorTexture, const AssetOwnerInfo& owner, std::string* outError = nullptr);
		// Adds a reference to an existing material. Unknown names set outError.
		MaterialAssetHandle GetMaterialHandle(const std::string& name, const AssetOwnerInfo& owner, std::string* outError = nullptr);
		const MaterialAsset* GetMaterial(const MaterialAssetHandle& handle) const;

		// Each unloads every asset of its type whose reference count is zero: asks the GPU uploader
		// to release them by asset ID, then deletes the CPU entries. Returns the count released.
		// Call timing is up to the caller; never while a frame is being drawn.
		// Unloading a material drops its texture reference; release textures after materials.
		size_t ReleaseUnusedMeshes();
		size_t ReleaseUnusedMaterials();
		size_t ReleaseUnusedTextures();

		const AssetTracker& GetTracker() const { return m_Tracker; }
		void PrintAssetRecords() const { m_Tracker.PrintRecord(); }

	private:
		template <typename T>
		friend class AssetHandle;

		MeshAssetId FindMesh(const std::string& pathKey) const;
		TextureAssetId FindTexture(const std::string& pathKey) const;
		MaterialAssetId FindMaterial(const std::string& name) const;

		MeshAssetId LoadMesh(const std::string& path, const std::string& pathKey, const std::string& filePath,
			std::string* outError);
		TextureAssetId LoadTexture(const std::string& path, const std::string& pathKey, const std::string& filePath,
			std::string* outError);

		MeshAssetHandle CreateMeshHandle(MeshAssetId id, const AssetOwnerInfo& owner);
		TextureAssetHandle CreateTextureHandle(TextureAssetId id, const AssetOwnerInfo& owner);
		MaterialAssetHandle CreateMaterialHandle(MaterialAssetId id, const AssetOwnerInfo& owner);

		// Called by AssetHandle::Reset.
		void Release(MeshAssetId id, AssetRecordId recordId);
		void Release(TextureAssetId id, AssetRecordId recordId);
		void Release(MaterialAssetId id, AssetRecordId recordId);

		// Shared by every asset type; defined in AssetManager.cpp.
		template <typename T>
		AssetHandle<T> CreateHandle(AssetMap<T>& entries, AssetType type, AssetId<T> id, const AssetOwnerInfo& owner);
		template <typename T>
		void ReleaseReference(AssetMap<T>& entries, AssetId<T> id, AssetRecordId recordId);

		AssetGpuUploader& m_GpuUploader;
		AssetTracker m_Tracker;

		// Textures are declared before materials so materials, which hold texture handles,
		// are destroyed first while the texture entries and the tracker still exist.
		std::unordered_map<std::string, TextureAssetId> m_TextureIdByPath;
		std::unordered_map<TextureAssetId, AssetEntry<TextureAsset>, AssetIdHash<TextureAsset>> m_TexturesById;

		std::unordered_map<std::string, MaterialAssetId> m_MaterialIdByName;
		std::unordered_map<MaterialAssetId, AssetEntry<MaterialAsset>, AssetIdHash<MaterialAsset>> m_MaterialsById;

		std::unordered_map<std::string, MeshAssetId> m_MeshIdByPath;
		std::unordered_map<MeshAssetId, AssetEntry<MeshAsset>, AssetIdHash<MeshAsset>> m_MeshesById;
	};

	template <typename T>
	void AssetHandle<T>::Reset()
	{
		if (m_Manager)
		{
			m_Manager->Release(m_Id, m_RecordId);
		}
		Clear();
	}
}
