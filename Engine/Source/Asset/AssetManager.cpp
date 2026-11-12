#include "Asset/AssetManager.h"
#include "Asset/Material/MaterialFile.h"
#include "Asset/Mesh/FbxImporter.h"
#include "Asset/Importer/glTF/CNglTFImporter.h"
#include "Asset/Mesh/PrimitiveMeshes.h"
#include "Asset/Texture/CubemapImporter.h"
#include "Asset/Texture/TextureImporter.h"
#include "CNDebug/DebugLog.h"
#include "Renderer/GpuResourceRegistry.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <filesystem>
#include <string_view>
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
			std::string assetPath;
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

		// TODO: [Editor] Offer the built-in meshes in the mesh picker.
		// Reserved paths are matched case-insensitively; keys are lowercase with '/' separators.
		constexpr std::string_view BuiltinMeshPrefix = "builtin/";

		struct BuiltinMeshEntry
		{
			std::string_view key;
			std::string_view assetPath;
			PrimitiveMeshData (*build)();
		};

		PrimitiveMeshData BuildDefaultSphere()
		{
			return PrimitiveMeshes::BuildSphere();
		}

		constexpr BuiltinMeshEntry BuiltinMeshes[] =
		{
			{ "builtin/cube", "Builtin/Cube", &PrimitiveMeshes::BuildCube },
			{ "builtin/sphere", "Builtin/Sphere", &BuildDefaultSphere },
			{ "builtin/plane", "Builtin/Plane", &PrimitiveMeshes::BuildPlane },
		};

		std::string NormalizeSeparators(std::string text)
		{
			for (char& c : text)
			{
				if (c == '\\') c = '/';
			}
			return text;
		}

		bool IsBuiltinMeshKey(const std::string& key)
		{
			return key.starts_with(BuiltinMeshPrefix);
		}

		std::string CanonicalBuiltinPath(const std::string& key, const std::string& fallback)
		{
			for (const BuiltinMeshEntry& entry : BuiltinMeshes)
			{
				if (entry.key == key)
				{
					return std::string(entry.assetPath);
				}
			}
			return fallback;
		}

		std::unique_ptr<MeshAsset> CreateBuiltinMesh([[maybe_unused]] const std::string& path, const std::string& pathKey)
		{
			for (const BuiltinMeshEntry& entry : BuiltinMeshes)
			{
				if (entry.key == pathKey)
				{
					PrimitiveMeshData data = entry.build();
					return std::make_unique<MeshAsset>(
                        std::move(data.vertices),
                        std::move(data.indices),
                        std::vector<SubMesh>{}
                    );
				}
			}
			CN_LOG_RUNTIME("Asset", "메쉬 에셋 로드 오류 : 알 수 없는 내장 메쉬, ", path);
			return nullptr;
		}

		std::filesystem::path& GetAssetRoot()
		{
			static std::filesystem::path root = []
			{
				std::wstring buffer(MAX_PATH, L'\0');
				for (;;)
				{
					const DWORD length = GetModuleFileNameW(
                        nullptr,
                        buffer.data(),
                        static_cast<DWORD>(buffer.size())
                    );
					if (length == 0)
					{
						std::error_code error;
						return std::filesystem::current_path(error).lexically_normal();
					}
					if (length < buffer.size())
					{
						buffer.resize(length);
						break;
					}
					buffer.resize(buffer.size() * 2);
				}
				return std::filesystem::path(buffer).parent_path().lexically_normal();
			}();
			return root;
		}

		std::filesystem::path ToAbsoluteAssetFile(const std::string& path)
		{
			const std::filesystem::path input(std::u8string(path.begin(), path.end()));
			return (input.is_absolute() ? input : GetAssetRoot() / input).lexically_normal();
		}

		bool TryMakeRelativeAssetPath(const std::filesystem::path& file, std::string& assetPath)
		{
			const std::filesystem::path relative = file.lexically_relative(GetAssetRoot());
			if (relative.empty() || *relative.begin() == "..")
			{
				assetPath = ToUtf8(file.generic_u8string());
				return false;
			}
			assetPath = ToUtf8(relative.generic_u8string());
			return true;
		}

		bool ResolveAssetPath(const std::string& path, ResolvedAssetPath& resolved)
		{
			//*** 에셋 경로 가공
			//std::u8string, utf-8, char8_t
			//std::filesystem::absolute, 상대 -> 절대
			//std::filesystem::path.lexically_normal(), 경로 정규화
			if (path.empty())
			{
				return false;
			}

			const std::filesystem::path file = ToAbsoluteAssetFile(path);
			resolved.utf8File = ToUtf8(file.generic_u8string());
			resolved.key = ToLowerAscii(resolved.utf8File);
			TryMakeRelativeAssetPath(file, resolved.assetPath);
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

	void AssetManager::SetAssetRoot(const std::filesystem::path& root)
	{
		GetAssetRoot() = std::filesystem::absolute(root).lexically_normal();
	}

	std::filesystem::path AssetManager::GetAssetFile(const std::string& path)
	{
		return ToAbsoluteAssetFile(path);
	}

	std::string AssetManager::ToAssetPath(const std::string& path)
	{
		if (path.empty())
		{
			return {};
		}

		std::string assetPath;
		if (!TryMakeRelativeAssetPath(ToAbsoluteAssetFile(path), assetPath))
		{
			CN_LOG_RUNTIME("Asset", "에셋 경로가 실행 폴더 밖에 있어 절대 경로로 유지됨 : ", assetPath);
		}
		return assetPath;
	}

	// Mesh
	MeshAssetHandle AssetManager::GetMeshHandle(const std::string& path, const AssetOwnerInfo& owner)
	{
		const std::string builtinPath = NormalizeSeparators(path);
		const std::string builtinKey = ToLowerAscii(builtinPath);
		if (IsBuiltinMeshKey(builtinKey))
		{
			MeshAssetId builtinId = FindMesh(builtinKey);
			if (!builtinId.IsValid())
			{
				const std::string canonicalPath = CanonicalBuiltinPath(builtinKey, builtinPath);
				builtinId = LoadMesh(canonicalPath, builtinKey, canonicalPath);
				if (!builtinId.IsValid())
				{
					return {};
				}
			}
			return CreateMeshHandle(builtinId, owner);
		}

		ResolvedAssetPath resolved;
		if (!ResolveAssetPath(path, resolved))
		{
			CN_LOG_RUNTIME("Asset", "메쉬 에셋 경로 오류 : ", path);
			return {};
		}

		MeshAssetId id = FindMesh(resolved.key);
		if (!id.IsValid())
		{
			id = LoadMesh(resolved.assetPath, resolved.key, resolved.utf8File);
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
		std::unique_ptr<MeshAsset> mesh;
		if (IsBuiltinMeshKey(pathKey))
		{
			mesh = CreateBuiltinMesh(path, pathKey);
			if (!mesh)
			{
				return {};
			}
		}
		else
		{
			const std::string extension = GetLowerExtension(filePath);
			if (extension == ".fbx")
			{
				mesh = FbxImporter::LoadMesh(filePath);
			}

			else if (extension == ".gltf")
			{
				CNglTF cnglTF;
				CNglTFImporter glTFImporter;
				if (glTFImporter.ReadglTF(filePath, cnglTF))
				{
					mesh = glTFImporter.LoadMeshAsset(cnglTF);
				}
				if (mesh)
				{
					WriteDefaultMaterialFiles(*mesh);
				}
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
		}
		if (!m_GpuRegistry.CreateMeshResource(*mesh).IsValid())
		{
			CN_LOG_RUNTIME("Asset", "메쉬 에셋 Gpu 로드 실패, ", path);
			return {};
		}

		const MeshAssetId id = mesh->GetAssetId();
		m_MeshIdByPath.emplace(pathKey, id);
		m_MeshesById.emplace(id, AssetEntry<MeshAsset>{ std::move(mesh), path, pathKey }).first->second.gpuUploaded = true;
		return id;
	}	

	void AssetManager::WriteDefaultMaterialFiles(const MeshAsset& mesh)
	{
		for (const MaterialData& slot : mesh.GetMaterialSlots())
		{
			if (!MaterialFile::IsMaterialPath(slot.name))
			{
				continue;
			}

			// 이미 있으면 사용자가 편집했을 수 있으므로 덮어쓰지 않는다.
			const std::filesystem::path file = GetAssetFile(slot.name);
			std::error_code error;
			if (std::filesystem::exists(file, error))
			{
				continue;
			}

			if (MaterialFile::Save(file, slot))
			{
				CN_LOG_RUNTIME("Asset", "메쉬 기본 머티리얼 파일 생성 : ", slot.name);
			}
			else
			{
				CN_LOG_RUNTIME("Asset", "메쉬 기본 머티리얼 파일 생성 실패 : ", slot.name);
			}
		}
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

	TextureAssetHandle AssetManager::GetTextureHandle(const std::string& path, const AssetOwnerInfo& owner, TextureFormat format)
	{
		ResolvedAssetPath resolved;
		if (!ResolveAssetPath(path, resolved))
		{
			CN_LOG_RUNTIME("Asset", "텍스처 에셋 경로 오류 : ", path);
			return {};
		}

		// 같은 파일을 sRGB(색상)와 UNORM(데이터)로 동시에 쓸 수 있으므로 format별로 캐시 키를 분리한다.
		const std::string key = format == TextureFormat::RGBA8_UNORM ? resolved.key + "|unorm" : resolved.key;
		TextureAssetId id = FindTexture(key);
		if (!id.IsValid())
		{
			id = LoadTexture(resolved.assetPath, key, resolved.utf8File, format);
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
		TextureFormat format)
	{
		const std::string extension = GetLowerExtension(filePath);

		std::unique_ptr<TextureAsset> texture;
		if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp")
		{
			texture = TextureImporter::LoadTexture(std::filesystem::path(std::u8string(filePath.begin(), filePath.end())), nullptr, format);
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
		m_TexturesById.emplace(id, AssetEntry<TextureAsset>{ std::move(texture), path, pathKey }).first->second.gpuUploaded = true;
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

	CubemapAssetHandle AssetManager::GetCubemapHandle(const std::string& path, const AssetOwnerInfo& owner)
	{
		ResolvedAssetPath resolved;
		if (!ResolveAssetPath(path, resolved))
		{
			CN_LOG_RUNTIME("Asset", "큐브맵 에셋 경로 오류 : ", path);
			return {};
		}

		CubemapAssetId id = FindCubemap(resolved.key);
		if (!id.IsValid())
		{
			id = LoadCubemap(resolved.assetPath, resolved.key, resolved.utf8File);
			if (!id.IsValid())
			{
				return {};
			}
		}
		return CreateCubemapHandle(id, owner);
	}

	const CubemapAsset* AssetManager::GetCubemap(const CubemapAssetHandle& handle) const
	{
		return FindAsset(m_CubemapsById, handle);
	}

	CubemapAssetId AssetManager::FindCubemap(const std::string& pathKey) const
	{
		return FindAssetId(m_CubemapIdByPath, pathKey);
	}

	CubemapAssetId AssetManager::LoadCubemap(const std::string& path, const std::string& pathKey, const std::string& filePath)
	{
		const std::string extension = GetLowerExtension(filePath);

		std::unique_ptr<CubemapAsset> cubemap;
		std::string error;
		if (extension == ".dds")
		{
			cubemap = CubemapImporter::LoadCubemap(std::filesystem::path(std::u8string(filePath.begin(), filePath.end())), &error);
		}
		else
		{
			CN_LOG_RUNTIME("Asset", "큐브맵 에셋 로드 오류 : 지원하지 않는 확장자, ", path);
			return {};
		}

		if (!cubemap)
		{
			CN_LOG_RUNTIME("Asset", "큐브맵 에셋 로드 오류 : Import 실패, ", error);
			return {};
		}
		if (!m_GpuRegistry.CreateCubemapResource(*cubemap).IsValid())
		{
			CN_LOG_RUNTIME("Asset", "큐브맵 에셋 Gpu 로드 실패, ", path);
			return {};
		}

		const CubemapAssetId id = cubemap->GetAssetId();
		m_CubemapIdByPath.emplace(pathKey, id);
		m_CubemapsById.emplace(id, AssetEntry<CubemapAsset>{ std::move(cubemap), path, pathKey }).first->second.gpuUploaded = true;
		return id;
	}

	CubemapAssetHandle AssetManager::CreateCubemapHandle(CubemapAssetId id, const AssetOwnerInfo& owner)
	{
		return CreateHandle(m_CubemapsById, AssetType::Cubemap, id, owner);
	}

	void AssetManager::Release(CubemapAssetId id, AssetRecordId recordId)
	{
		ReleaseReference(m_CubemapsById, id, recordId);
	}

	size_t AssetManager::ReleaseUnusedCubemaps()
	{
		const std::vector<CubemapAssetId> unused = CollectUnused(m_CubemapsById);
		if (unused.empty())
		{
			return 0;
		}
		m_GpuRegistry.ReleaseCubemaps(unused);
		EraseAssets(m_CubemapIdByPath, m_CubemapsById, unused);
		return unused.size();
	}

	// Material

	MaterialAssetHandle AssetManager::CreateMaterial(const std::string& name, const MaterialData& data, const AssetOwnerInfo& owner)
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

		auto material = std::make_unique<MaterialAsset>(data.desc, LoadMaterialTextures(name, data, owner.sceneName));
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
		const bool isFile = MaterialFile::IsMaterialPath(name);
		const std::string key = isFile ? ToAssetPath(name) : name;
		const MaterialAssetId id = FindMaterial(key);
		if (id.IsValid())
		{
			return CreateMaterialHandle(id, owner);
		}
		if (isFile)
		{
			return LoadMaterialFile(key, owner);
		}

		CN_LOG_RUNTIME("Asset", "머티리얼 에셋 조회 오류 : 존재하지 않음, ", name);
		return {};
	}

	MaterialAssetHandle AssetManager::LoadMaterialFile(const std::string& assetPath, const AssetOwnerInfo& owner)
	{
		MaterialData data;
		if (!MaterialFile::Load(GetAssetFile(assetPath), data))
		{
			CN_LOG_RUNTIME("Asset", "머티리얼 파일 로드 실패 : ", assetPath);
			return {};
		}

		return CreateMaterial(assetPath, data, owner);
	}

	MaterialTextures AssetManager::LoadMaterialTextures(const std::string& materialName, const MaterialData& data,
		const std::string& sceneName)
	{
		// 텍스처 핸들의 소유자는 머티리얼. 머티리얼이 살아 있는 동안 텍스처도 유지된다.
		const AssetOwnerInfo textureOwner{ sceneName, "Material:" + materialName, "MaterialAsset" };

		MaterialTextures textures;
		for (size_t index = 0; index < kMaterialTextureSlotCount; ++index)
		{
			const MaterialTextureSlot slot = static_cast<MaterialTextureSlot>(index);
			const std::string& path = data.GetTexturePath(slot);
			if (path.empty())
			{
				continue;
			}

			// 로드 실패 시 해당 슬롯만 비워두고 머티리얼은 계속 만든다. (실패 로그는 GetTextureHandle에서 출력)
			textures[index] = GetTextureHandle(path, textureOwner, GetMaterialTextureFormat(slot));
		}
		return textures;
	}

	bool AssetManager::UpdateMaterial(const std::string& name, const MaterialData& data)
	{
		const std::string key = MaterialFile::IsMaterialPath(name) ? ToAssetPath(name) : name;
		const MaterialAssetId id = FindMaterial(key);
		const auto found = m_MaterialsById.find(id);
		if (found == m_MaterialsById.end())
		{
			return false;
		}

		MaterialAsset& material = *found->second.asset;
		material.SetDesc(data.desc);
		material.SetTextures(LoadMaterialTextures(key, data, ""));

		const MaterialAssetId ids[] = { id };
		m_GpuRegistry.ReleaseMaterials(ids);
		return m_GpuRegistry.CreateMaterialResource(material).IsValid();
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
		statuses.reserve(m_MeshesById.size() + m_MaterialsById.size() + m_TexturesById.size() + m_CubemapsById.size());
		AppendAssetStatuses(m_MeshesById, AssetType::Mesh, statuses);
		AppendAssetStatuses(m_MaterialsById, AssetType::Material, statuses);
		AppendAssetStatuses(m_TexturesById, AssetType::Texture, statuses);
		AppendAssetStatuses(m_CubemapsById, AssetType::Cubemap, statuses);
		m_Tracker.PrintRecord(statuses);
	}
	size_t AssetManager::ReleaseUnusedAssets()
	{
		size_t released = ReleaseUnusedMeshes();
		released += ReleaseUnusedMaterials();
		released += ReleaseUnusedTextures();
		released += ReleaseUnusedCubemaps();
		return released;
	}
}
