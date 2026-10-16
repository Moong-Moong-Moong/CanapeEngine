#include "Asset/AssetManager.h"
#include "Asset/Mesh/FbxImporter.h"
#include "Asset/Texture/TextureImporter.h"

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
			std::filesystem::path file;
			std::string utf8File; // Absolute, normalized, generic separators.
			std::string key;      // utf8File lowercased: Windows paths are case-insensitive.
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

		// 파일 경로를 절대 경로 변환
		// 문자열 표현 UTF-8 통일
		// 대문자 -> 소문자 변환 (Windows에서는 파일 대소문자 구분 X)
		bool ResolveAssetPath(const std::string& path, ResolvedAssetPath& resolved, std::string& error)
		{
			std::error_code pathError;
			try
			{
				resolved.file = std::filesystem::absolute(std::filesystem::path(std::u8string(path.begin(), path.end())), pathError).lexically_normal();
			}
			catch (const std::system_error&)
			{
				error = path + ": Decode asset path failed";
				return false;
			}
			if (pathError)
			{
				error = path + ": Resolve asset path failed (" + pathError.message() + ')';
				return false;
			}
			resolved.utf8File = ToUtf8(resolved.file.generic_u8string());
			resolved.key = ToLowerAscii(resolved.utf8File);
			return true;
		}

		std::string GetLowerExtension(const std::string& utf8File)
		{
			return ToLowerAscii(ToUtf8(std::filesystem::path(std::u8string(utf8File.begin(), utf8File.end())).extension().u8string()));
		}

		void LogAssetError(const char* operation, const std::string& error)
		{
			OutputDebugStringA(("[AssetManager] " + std::string(operation) + ": " + error + "\n").c_str());
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

		// Destroying an asset can release handles it owns (a material's texture) into other maps.
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

	AssetManager::AssetManager(AssetGpuUploader& gpuUploader)
		: m_GpuUploader(gpuUploader)
	{
	}

	// Mesh

	MeshAssetHandle AssetManager::GetMeshHandle(const std::string& path, const AssetOwnerInfo& owner, std::string* outError)
	{
		if (outError) outError->clear();

		ResolvedAssetPath resolved;
		std::string error;
		if (!ResolveAssetPath(path, resolved, error))
		{
			LogAssetError("Failed to load mesh", error);
			return {};
		}

		MeshAssetId id = FindMesh(resolved.key);
		if (!id.IsValid())
		{
			id = LoadMesh(path, resolved.key, resolved.utf8File, outError);
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

	MeshAssetId AssetManager::LoadMesh(const std::string& path, const std::string& pathKey, const std::string& filePath,
		std::string* outError)
	{
		const std::string extension = GetLowerExtension(filePath);

		std::string error;
		std::unique_ptr<MeshAsset> mesh;
		if (extension == ".fbx")
		{
			mesh = FbxImporter::LoadMesh(filePath, &error);
		}
		else
		{
			error = path + ": Unsupported mesh file extension '" + extension + "'";
			LogAssetError("Failed to load mesh", error);
			if (outError) *outError = std::move(error);
			return {};
		}

		if (!mesh)
		{
			LogAssetError("Failed to load mesh", error);
			return {};
		}
		if (!m_GpuUploader.UploadMesh(*mesh, error))
		{
			LogAssetError("Failed to upload mesh", path + ": " + error);
			return {};
		}

		const MeshAssetId id = mesh->GetAssetId();
		m_MeshIdByPath.emplace(pathKey, id);
		m_MeshesById.emplace(id, AssetEntry<MeshAsset>{ std::move(mesh), filePath, pathKey });
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
		// GPU resources go first; the CPU entry is the source they were built from.
		m_GpuUploader.ReleaseMeshes(unused);
		EraseAssets(m_MeshIdByPath, m_MeshesById, unused);
		return unused.size();
	}

	// Texture

	TextureAssetHandle AssetManager::GetTextureHandle(const std::string& path, const AssetOwnerInfo& owner, std::string* outError)
	{
		if (outError) outError->clear();

		ResolvedAssetPath resolved;
		std::string error;
		if (!ResolveAssetPath(path, resolved, error))
		{
			LogAssetError("Failed to load texture", error);
			return {};
		}

		TextureAssetId id = FindTexture(resolved.key);
		if (!id.IsValid())
		{
			id = LoadTexture(path, resolved.key, resolved.utf8File, outError);
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

	TextureAssetId AssetManager::LoadTexture(const std::string& path, const std::string& pathKey, const std::string& filePath,
		std::string* outError)
	{
		const std::string extension = GetLowerExtension(filePath);

		std::string error;
		std::unique_ptr<TextureAsset> texture;
		if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp")
		{
			texture = TextureImporter::LoadTexture(std::filesystem::path(std::u8string(filePath.begin(), filePath.end())), &error);
		}
		else
		{
			error = path + ": Unsupported texture file extension '" + extension + "'";
			LogAssetError("Failed to load texture", error);
			if (outError) *outError = std::move(error);
			return {};
		}

		if (!texture)
		{
			LogAssetError("Failed to load texture", error);
			return {};
		}
		if (!m_GpuUploader.UploadTexture(*texture, error))
		{
			LogAssetError("Failed to upload texture", path + ": " + error);
			return {};
		}

		const TextureAssetId id = texture->GetAssetId();
		m_TextureIdByPath.emplace(pathKey, id);
		m_TexturesById.emplace(id, AssetEntry<TextureAsset>{ std::move(texture), filePath, pathKey });
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
		m_GpuUploader.ReleaseTextures(unused);
		EraseAssets(m_TextureIdByPath, m_TexturesById, unused);
		return unused.size();
	}

	// Material

	MaterialAssetHandle AssetManager::CreateMaterial(const std::string& name, const MaterialDesc& desc,
		const TextureAssetHandle& baseColorTexture, const AssetOwnerInfo& owner, std::string* outError)
	{
		if (outError) outError->clear();
		auto fail = [&](std::string error) -> MaterialAssetHandle
		{
			LogAssetError("Failed to create material", error);
			if (outError) *outError = std::move(error);
			return {};
		};

		if (name.empty()) return fail("Material name must not be empty");
		if (FindMaterial(name).IsValid()) return fail("Material '" + name + "' already exists");

		// The caller loads the texture first; the material takes its own reference to it.
		TextureAssetHandle texture;
		if (baseColorTexture.IsValid())
		{
			if (!GetTexture(baseColorTexture)) return fail("Material '" + name + "' references a released texture");
			texture = CreateTextureHandle(baseColorTexture.GetId(), { owner.sceneName, "Material:" + name, "MaterialAsset" });
		}

		auto material = std::make_unique<MaterialAsset>(desc, std::move(texture));
		std::string error;
		if (!m_GpuUploader.UploadMaterial(*material, error))
		{
			LogAssetError("Failed to upload material", name + ": " + error);
			return {};
		}

		const MaterialAssetId id = material->GetAssetId();
		m_MaterialIdByName.emplace(name, id);
		m_MaterialsById.emplace(id, AssetEntry<MaterialAsset>{ std::move(material), name, name });
		return CreateMaterialHandle(id, owner);
	}

	MaterialAssetHandle AssetManager::GetMaterialHandle(const std::string& name, const AssetOwnerInfo& owner, std::string* outError)
	{
		if (outError) outError->clear();

		const MaterialAssetId id = FindMaterial(name);
		if (!id.IsValid())
		{
			std::string error = "Material '" + name + "' does not exist";
			LogAssetError("Failed to get material", error);
			if (outError) *outError = std::move(error);
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
		m_GpuUploader.ReleaseMaterials(unused);
		EraseAssets(m_MaterialIdByName, m_MaterialsById, unused);
		return unused.size();
	}
}
