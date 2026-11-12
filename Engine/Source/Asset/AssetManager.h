#pragma once

#include "Asset/AssetHandle.h"
#include "Asset/AssetTracker.h"
#include "Asset/Material/MaterialAsset.h"
#include "Asset/Material/MaterialFile.h"
#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Texture/CubemapAsset.h"
#include "Asset/Texture/TextureAsset.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace Canape
{
	class GpuResourceRegistry;

	template <typename T>
	struct AssetEntry
	{
		std::unique_ptr<T> asset;
		std::string path; // 에셋 import에 사용된 원본 파일 경로, 에셋 트래커 출력용
		std::string pathKey; // 소문자화 시킨 파일 경로, Key값으로 사용하기 위해, 대소문자 구분 없음
		uint32_t referenceCount = 0;
		bool unloadRequested = false;
		bool gpuUploaded = false;
	};

	template <typename T>
	using AssetMap = std::unordered_map<AssetId<T>, AssetEntry<T>, AssetIdHash<T>>;

	//***** CPU 자원을 관리
	class AssetManager
	{
	public:
		explicit AssetManager(GpuResourceRegistry& gpuRegistry);

		AssetManager(const AssetManager&) = delete;
		AssetManager& operator=(const AssetManager&) = delete;

		static void SetAssetRoot(const std::filesystem::path& root);
		static std::filesystem::path GetAssetFile(const std::string& path);
		static std::string ToAssetPath(const std::string& path);

		//***** Release
		size_t ReleaseUnusedAssets();

		//***** Mesh
		MeshAssetHandle GetMeshHandle(const std::string& path, const AssetOwnerInfo& owner);
		const MeshAsset* GetMesh(const MeshAssetHandle& handle) const;

		//***** Texture
		// 같은 파일이라도 format이 다르면 별도 에셋으로 캐시된다.
		TextureAssetHandle GetTextureHandle(const std::string& path, const AssetOwnerInfo& owner,
			TextureFormat format = TextureFormat::RGBA8_SRGB);
		const TextureAsset* GetTexture(const TextureAssetHandle& handle) const;

		CubemapAssetHandle GetCubemapHandle(const std::string& path, const AssetOwnerInfo& owner);
		const CubemapAsset* GetCubemap(const CubemapAssetHandle& handle) const;

		//***** Material
		// data의 텍스처 경로들을 슬롯별 format으로 로드해서 머티리얼을 만든다. 같은 이름이 있으면 기존 것을 반환.
		MaterialAssetHandle CreateMaterial(const std::string& name, const MaterialData& data, const AssetOwnerInfo& owner);
		MaterialAssetHandle GetMaterialHandle(const std::string& name, const AssetOwnerInfo& owner);
		bool UpdateMaterial(const std::string& name, const MaterialData& data);
		const MaterialAsset* GetMaterial(const MaterialAssetHandle& handle) const;

		//***** Asset Tracker
		const AssetTracker& GetTracker() const { return m_Tracker; }
		void PrintAssetRecords() const;

	private:
		template <typename T>
		friend class AssetHandle;

		size_t ReleaseUnusedMeshes();
		size_t ReleaseUnusedMaterials();
		size_t ReleaseUnusedTextures();
		size_t ReleaseUnusedCubemaps();

		MeshAssetId FindMesh(const std::string& pathKey) const;
		TextureAssetId FindTexture(const std::string& pathKey) const;
		CubemapAssetId FindCubemap(const std::string& pathKey) const;
		MaterialAssetId FindMaterial(const std::string& name) const;

		MaterialAssetHandle LoadMaterialFile(const std::string& assetPath, const AssetOwnerInfo& owner);
		MeshAssetId LoadMesh(const std::string& path, const std::string& pathKey, const std::string& filePath);
		// 메쉬 머티리얼 슬롯의 기본 데이터를 슬롯 경로(.mat)에 저장한다. 파일이 이미 있으면 건너뛴다.
		void WriteDefaultMaterialFiles(const MeshAsset& mesh);
		TextureAssetId LoadTexture(const std::string& path, const std::string& pathKey, const std::string& filePath, TextureFormat format);
		MaterialTextures LoadMaterialTextures(const std::string& materialName, const MaterialData& data, const std::string& sceneName);
		CubemapAssetId LoadCubemap(const std::string& path, const std::string& pathKey, const std::string& filePath);

		MeshAssetHandle CreateMeshHandle(MeshAssetId id, const AssetOwnerInfo& owner);
		TextureAssetHandle CreateTextureHandle(TextureAssetId id, const AssetOwnerInfo& owner);
		CubemapAssetHandle CreateCubemapHandle(CubemapAssetId id, const AssetOwnerInfo& owner);
		MaterialAssetHandle CreateMaterialHandle(MaterialAssetId id, const AssetOwnerInfo& owner);

		void Release(MeshAssetId id, AssetRecordId recordId);
		void Release(TextureAssetId id, AssetRecordId recordId);
		void Release(CubemapAssetId id, AssetRecordId recordId);
		void Release(MaterialAssetId id, AssetRecordId recordId);

		template <typename T>
		AssetHandle<T> CreateHandle(AssetMap<T>& entries, AssetType type, AssetId<T> id, const AssetOwnerInfo& owner);
		template <typename T>
		void ReleaseReference(AssetMap<T>& entries, AssetId<T> id, AssetRecordId recordId);

		GpuResourceRegistry& m_GpuRegistry;
		AssetTracker m_Tracker;

		std::unordered_map<std::string, TextureAssetId> m_TextureIdByPath;
		std::unordered_map<TextureAssetId, AssetEntry<TextureAsset>, AssetIdHash<TextureAsset>> m_TexturesById;

		std::unordered_map<std::string, CubemapAssetId> m_CubemapIdByPath;
		std::unordered_map<CubemapAssetId, AssetEntry<CubemapAsset>, AssetIdHash<CubemapAsset>> m_CubemapsById;

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
