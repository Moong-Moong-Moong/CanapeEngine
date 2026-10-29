#include "Asset/AssetManager.h"
#include "Asset/Mesh/FbxImporter.h"
#include "Asset/Texture/TextureImporter.h"
#include "CNDebug/DebugLog.h"
#include "Renderer/GpuResourceRegistry.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <system_error>
#include <vector>

namespace Canape
{
	namespace
	{
		struct ResolvedAssetPath
		{
			std::string utf8File; // 절대 경로
			std::string key;      // 소문자로 변환된 키 용 경로
		};

		std::string ToLowerAscii(std::string text)
		{
			for (char& c : text)
			{
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			}
			return text;
		}

		std::string ToUtf8(const std::u8string& text)
		{
			return std::string(text.begin(), text.end());
		}

		bool ResolveAssetPath(const std::string& path, ResolvedAssetPath& resolved)
		{
			//*** 에셋 경로 가공
			//std::u8string, utf-8, char8_t
			//std::filesystem::absolute, 상대 -> 절대
			//std::filesystem::path.lexically_normal(), 경로 정규화
			std::error_code pathError;
			const std::filesystem::path file = 
				std::filesystem::absolute(
					std::filesystem::path(std::u8string(path.begin(), path.end())),
					pathError
				).lexically_normal();

			if (pathError)
			{
				return false;
			}

			resolved.utf8File = ToUtf8(file.generic_u8string());
			resolved.key = ToLowerAscii(resolved.utf8File);
			return true;
		}

		//*** 확장자를 소문자로 파싱
		std::string GetLowerExtension(const std::string& utf8File)
		{
			return ToLowerAscii(ToUtf8(std::filesystem::path(std::u8string(utf8File.begin(), utf8File.end())).extension().u8string()));
		}

		template <typename T>
		AssetId<T> FindAssetId(const std::unordered_map<std::string, AssetId<T>>& idByKey, const std::string& key)
		{
			const auto found = idByKey.find(key);
			return found != idByKey.end() ? found->second : AssetId<T>{};
		}

		template <typename T>
		const T* FindAsset(const AssetMap<T>& entries, const AssetHandle<T>& handle)
		{
			if (!handle.IsValid())
			{
				return nullptr;
			}
			const auto found = entries.find(handle.GetId());
			return found != entries.end() ? found->second.asset.get() : nullptr;
		}

		template <typename T>
		std::vector<AssetId<T>> CollectUnused(const AssetMap<T>& entries)
		{
			std::vector<AssetId<T>> unused;
			for (const auto& [id, entry] : entries)
			{
				if (entry.unloadRequested)
				{
					unused.push_back(id);
				}
			}
			return unused;
		}

		template <typename T>
		void EraseAssets(std::unordered_map<std::string, AssetId<T>>& idByKey, AssetMap<T>& entries,
			const std::vector<AssetId<T>>& ids)
		{
			for (const AssetId<T> id : ids)
			{
				const auto found = entries.find(id);
				idByKey.erase(found->second.pathKey);
				entries.erase(found);
			}
		}

		template <typename T>
		void AppendAssetStatuses(const AssetMap<T>& entries, AssetType type, std::vector<AssetStatus>& statuses)
		{
			for (const auto& [id, entry] : entries)
			{
				statuses.push_back({ type, id.value, entry.path, entry.referenceCount, entry.gpuUploaded });
			}
		}
	}

	template <typename T>
	AssetHandle<T> AssetManager::CreateHandle(AssetMap<T>& entries, AssetType type, AssetId<T> id, const AssetOwnerInfo& owner)
	{
		AssetEntry<T>& entry = entries.at(id);
		++entry.referenceCount;
		entry.unloadRequested = false;
		const AssetRecordId recordId = m_Tracker.AddRecord(type, id.value, entry.path, owner);
		return AssetHandle<T>(id, recordId, this);
	}

	template <typename T>
	void AssetManager::ReleaseReference(AssetMap<T>& entries, AssetId<T> id, AssetRecordId recordId)
	{
		m_Tracker.DeleteRecord(recordId);

		const auto found = entries.find(id);
		if (found == entries.end())
		{
			return;
		}
		AssetEntry<T>& entry = found->second;
		if (entry.referenceCount > 0 && --entry.referenceCount == 0)
		{
			entry.unloadRequested = true;
		}
	}

	AssetManager::AssetManager(GpuResourceRegistry& gpuRegistry)
		: m_GpuRegistry(gpuRegistry)
	{
	}

	// Mesh
	MeshAssetHandle AssetManager::GetMeshHandle(const std::string& path, const AssetOwnerInfo& owner)
	{
		ResolvedAssetPath resolved;
		if (!ResolveAssetPath(path, resolved))
		{
			CN_LOG_RUNTIME("Asset", "메쉬 에셋 경로 오류 : ", path);
			return {};
		}

		MeshAssetId id = FindMesh(resolved.key);

		if (!id.IsValid())
		{
			//*** id가 등록되어있지 않은 새로 로드해야하는 에셋
			id = LoadMesh(path, resolved.key, resolved.utf8File);
			if (!id.IsValid())
			{
				return {};
			}
		}
		return CreateMeshHandle(id, owner);
	}

	const MeshAsset* AssetManager::GetMesh(const MeshAssetHandle& handle) const
	{
		return FindAsset(m_MeshesById, handle);
	}

	MeshAssetId AssetManager::FindMesh(const std::string& pathKey) const
	{
		return FindAssetId(m_MeshIdByPath, pathKey);
	}

	MeshAssetId AssetManager::LoadMesh(const std::string& path, const std::string& pathKey, const std::string& filePath)
	{
		const std::string extension = GetLowerExtension(filePath);

		std::unique_ptr<MeshAsset> mesh;
		if (extension == ".fbx")
		{
			mesh = FbxImporter::LoadMesh(filePath);
		}
		else
		{
			CN_LOG_RUNTIME("Asset", "메쉬 에셋 로드 오류 : 지원하지 않는 확장자, ", path);
			return {};
		}

		if (!mesh)
		{
			CN_LOG_RUNTIME("Asset", "메쉬 에셋 로드 오류 : Import 실패, ", path);
			return {};
		}
		if (!m_GpuRegistry.CreateMeshResource(*mesh).IsValid())
		{
			CN_LOG_RUNTIME("Asset", "메쉬 에셋 Gpu 로드 실패, ", path);
			return {};
		}

		const MeshAssetId id = mesh->GetAssetId();
		m_MeshIdByPath.emplace(pathKey, id);
		m_MeshesById.emplace(id, AssetEntry<MeshAsset>{ std::move(mesh), filePath, pathKey }).first->second.gpuUploaded = true;
		return id;
	}	

	MeshAssetHandle AssetManager::CreateMeshHandle(MeshAssetId id, const AssetOwnerInfo& owner)
	{
		return CreateHandle(m_MeshesById, AssetType::Mesh, id, owner);
	}

	void AssetManager::Release(MeshAssetId id, AssetRecordId recordId)
	{
		ReleaseReference(m_MeshesById, id, recordId);
	}

	size_t AssetManager::ReleaseUnusedMeshes()
	{
		const std::vector<MeshAssetId> unused = CollectUnused(m_MeshesById);
		if (unused.empty())
		{
			return 0;
		}

		m_GpuRegistry.ReleaseMeshes(unused);
		EraseAssets(m_MeshIdByPath, m_MeshesById, unused);
		return unused.size();
	}

	// Texture

	TextureAssetHandle AssetManager::GetTextureHandle(const std::string& path, const AssetOwnerInfo& owner)
	{
		ResolvedAssetPath resolved;
		if (!ResolveAssetPath(path, resolved))
		{
			CN_LOG_RUNTIME("Asset", "텍스처 에셋 경로 오류 : ", path);
			return {};
		}

		TextureAssetId id = FindTexture(resolved.key);
		if (!id.IsValid())
		{
			id = LoadTexture(path, resolved.key, resolved.utf8File);
			if (!id.IsValid())
			{
				return {};
			}
		}
		return CreateTextureHandle(id, owner);
	}

	const TextureAsset* AssetManager::GetTexture(const TextureAssetHandle& handle) const
	{
		return FindAsset(m_TexturesById, handle);
	}

	TextureAssetId AssetManager::FindTexture(const std::string& pathKey) const
	{
		return FindAssetId(m_TextureIdByPath, pathKey);
	}

	TextureAssetId AssetManager::LoadTexture(const std::string& path, const std::string& pathKey, const std::string& filePath)
	{
		const std::string extension = GetLowerExtension(filePath);

		std::unique_ptr<TextureAsset> texture;
		if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp")
		{
			texture = TextureImporter::LoadTexture(std::filesystem::path(std::u8string(filePath.begin(), filePath.end())));
		}
		else
		{
			CN_LOG_RUNTIME("Asset", "텍스처 에셋 로드 오류 : 지원하지 않는 확장자, ", path);
			return {};
		}

		if (!texture)
		{
			CN_LOG_RUNTIME("Asset", "텍스처 에셋 로드 오류 : Import 실패, ", path);
			return {};
		}
		if (!m_GpuRegistry.CreateTextureResource(*texture).IsValid())
		{
			CN_LOG_RUNTIME("Asset", "텍스처 에셋 Gpu 로드 실패, ", path);
			return {};
		}

		const TextureAssetId id = texture->GetAssetId();
		m_TextureIdByPath.emplace(pathKey, id);
		m_TexturesById.emplace(id, AssetEntry<TextureAsset>{ std::move(texture), filePath, pathKey }).first->second.gpuUploaded = true;
		return id;
	}

	TextureAssetHandle AssetManager::CreateTextureHandle(TextureAssetId id, const AssetOwnerInfo& owner)
	{
		return CreateHandle(m_TexturesById, AssetType::Texture, id, owner);
	}

	void AssetManager::Release(TextureAssetId id, AssetRecordId recordId)
	{
		ReleaseReference(m_TexturesById, id, recordId);
	}

	size_t AssetManager::ReleaseUnusedTextures()
	{
		const std::vector<TextureAssetId> unused = CollectUnused(m_TexturesById);
		if (unused.empty())
		{
			return 0;
		}
		m_GpuRegistry.ReleaseTextures(unused);
		EraseAssets(m_TextureIdByPath, m_TexturesById, unused);
		return unused.size();
	}

	// Material

	MaterialAssetHandle AssetManager::CreateMaterial(const std::string& name, const MaterialDesc& desc,
		const TextureAssetHandle& baseColorTexture, const AssetOwnerInfo& owner)
	{
		if (name.empty())
		{
			CN_LOG_RUNTIME("Asset", "머티리얼 에셋 생성 오류 : 이름 없음");
			return {};
		}
		if (const MaterialAssetId existing = FindMaterial(name); existing.IsValid())
		{
			CN_LOG_RUNTIME("Asset", "이미 있는 머티리얼 반환: ", name);
			return CreateMaterialHandle(existing, owner);
		}

		TextureAssetHandle texture;
		if (baseColorTexture.IsValid())
		{
			if (!GetTexture(baseColorTexture))
			{
				CN_LOG_RUNTIME("Asset", "머티리얼 에셋 생성 오류 : 해제된 텍스처 참조, ", name);
				return {};
			}
			texture = CreateTextureHandle(baseColorTexture.GetId(), { owner.sceneName, "Material:" + name, "MaterialAsset" });
		}

		auto material = std::make_unique<MaterialAsset>(desc, std::move(texture));
		if (!m_GpuRegistry.CreateMaterialResource(*material).IsValid())
		{
			CN_LOG_RUNTIME("Asset", "머티리얼 에셋 Gpu 로드 실패, ", name);
			return {};
		}

		const MaterialAssetId id = material->GetAssetId();
		m_MaterialIdByName.emplace(name, id);
		m_MaterialsById.emplace(id, AssetEntry<MaterialAsset>{ std::move(material), name, name }).first->second.gpuUploaded = true;
		return CreateMaterialHandle(id, owner);
	}

	MaterialAssetHandle AssetManager::GetMaterialHandle(const std::string& name, const AssetOwnerInfo& owner)
	{
		const MaterialAssetId id = FindMaterial(name);
		if (!id.IsValid())
		{
			CN_LOG_RUNTIME("Asset", "머티리얼 에셋 조회 오류 : 존재하지 않음, ", name);
			return {};
		}
		return CreateMaterialHandle(id, owner);
	}

	const MaterialAsset* AssetManager::GetMaterial(const MaterialAssetHandle& handle) const
	{
		return FindAsset(m_MaterialsById, handle);
	}

	MaterialAssetId AssetManager::FindMaterial(const std::string& name) const
	{
		return FindAssetId(m_MaterialIdByName, name);
	}

	MaterialAssetHandle AssetManager::CreateMaterialHandle(MaterialAssetId id, const AssetOwnerInfo& owner)
	{
		return CreateHandle(m_MaterialsById, AssetType::Material, id, owner);
	}

	void AssetManager::Release(MaterialAssetId id, AssetRecordId recordId)
	{
		ReleaseReference(m_MaterialsById, id, recordId);
	}

	size_t AssetManager::ReleaseUnusedMaterials()
	{
		const std::vector<MaterialAssetId> unused = CollectUnused(m_MaterialsById);
		if (unused.empty())
		{
			return 0;
		}
		m_GpuRegistry.ReleaseMaterials(unused);
		EraseAssets(m_MaterialIdByName, m_MaterialsById, unused);
		return unused.size();
	}

	void AssetManager::PrintAssetRecords() const
	{
		std::vector<AssetStatus> statuses;
		statuses.reserve(m_MeshesById.size() + m_MaterialsById.size() + m_TexturesById.size());
		AppendAssetStatuses(m_MeshesById, AssetType::Mesh, statuses);
		AppendAssetStatuses(m_MaterialsById, AssetType::Material, statuses);
		AppendAssetStatuses(m_TexturesById, AssetType::Texture, statuses);
		m_Tracker.PrintRecord(statuses);
	}
	size_t AssetManager::ReleaseUnusedAssets()
	{
		size_t released = ReleaseUnusedMeshes();
		released += ReleaseUnusedMaterials();
		released += ReleaseUnusedTextures();
		return released;
	}
}
