#include "Asset/AssetManager.h"
#include "Asset/Material/MaterialFile.h"
#include "Asset/Mesh/FbxImporter.h"
#include "Asset/Mesh/MeshSerializer.h"
#include "Asset/Importer/glTF/CNglTFImporter.h"
#include "Asset/Mesh/PrimitiveMeshes.h"
#include "Asset/Texture/CubemapImporter.h"
#include "Asset/Texture/TextureImporter.h"
#include "Asset/Animation/AnimationClipAsset.h"
#include "Asset/Animation/AnimationClipSerializer.h"
#include "CNDebug/DebugLog.h"
#include "Renderer/GpuResourceRegistry.h"

#include <Windows.h>

#include <filesystem>
#include <functional>
#include <optional>
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

		PrimitiveMeshData BuildDefaultCylinder()
		{
			return PrimitiveMeshes::BuildCylinder();
		}

		constexpr BuiltinMeshEntry BuiltinMeshes[] =
		{
			{ "builtin/cube", "Builtin/Cube", &PrimitiveMeshes::BuildCube },
			{ "builtin/sphere", "Builtin/Sphere", &BuildDefaultSphere },
			{ "builtin/plane", "Builtin/Plane", &PrimitiveMeshes::BuildPlane },
			{ "builtin/cylinder", "Builtin/Cylinder", &BuildDefaultCylinder },
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

		using PathRemap = std::function<std::optional<std::string>(const std::string&)>;

		std::optional<std::string> RemapPrefixed(const std::string& value, const std::string& fromLower, const std::string& to, bool directory)
		{
			const size_t split = value.find_first_of("#|");
			const std::string base = value.substr(0, split);
			const std::string suffix = split == std::string::npos ? std::string() : value.substr(split);
			const std::string lowerBase = ToLowerAscii(base);
			if (!directory)
			{
				return lowerBase == fromLower ? std::optional<std::string>(to + suffix) : std::nullopt;
			}
			if (!lowerBase.starts_with(fromLower + "/"))
			{
				return std::nullopt;
			}
			return to + base.substr(fromLower.size()) + suffix;
		}

		template <typename T>
		size_t RemapCache(std::unordered_map<std::string, AssetId<T>>& idByKey, AssetMap<T>& entries,
			const PathRemap& remapKey, const PathRemap& remapPath)
		{
			std::vector<std::pair<std::string, std::string>> moves;
			for (const auto& [key, id] : idByKey)
			{
				if (std::optional<std::string> moved = remapKey(key))
				{
					moves.emplace_back(key, std::move(*moved));
				}
			}

			size_t remapped = 0;
			for (const auto& [oldKey, newKey] : moves)
			{
				if (idByKey.contains(newKey))
				{
					CN_LOG_RUNTIME("Asset", "에셋 경로 갱신 : 새 경로가 이미 로드되어 있어 이전 캐시를 분리함, ", newKey);
					if (const auto entry = entries.find(idByKey[oldKey]); entry != entries.end())
					{
						entry->second.pathKey.clear();
					}
					idByKey.erase(oldKey);
					continue;
				}

				auto node = idByKey.extract(oldKey);
				const AssetId<T> id = node.mapped();
				node.key() = newKey;
				idByKey.insert(std::move(node));
				if (const auto entry = entries.find(id); entry != entries.end())
				{
					entry->second.pathKey = newKey;
					if (std::optional<std::string> path = remapPath(entry->second.path))
					{
						entry->second.path = std::move(*path);
					}
				}
				++remapped;
			}
			return remapped;
		}

		template <typename T, typename Matcher>
		size_t ForgetCache(std::unordered_map<std::string, AssetId<T>>& idByKey, AssetMap<T>& entries, const Matcher& matches)
		{
			size_t forgotten = 0;
			for (auto it = idByKey.begin(); it != idByKey.end();)
			{
				if (!matches(it->first))
				{
					++it;
					continue;
				}
				if (const auto entry = entries.find(it->second); entry != entries.end())
				{
					entry->second.pathKey.clear();
				}
				it = idByKey.erase(it);
				++forgotten;
			}
			return forgotten;
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

	std::vector<std::string> AssetManager::GetBuiltinMeshPaths()
	{
		std::vector<std::string> paths;
		paths.reserve(std::size(BuiltinMeshes));
		for (const BuiltinMeshEntry& entry : BuiltinMeshes)
		{
			paths.emplace_back(entry.assetPath);
		}
		return paths;
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

		const size_t skinHash = path.find("#skin");
		if (skinHash != std::string::npos)
		{
			ResolvedAssetPath skinFile;
			if (!ResolveAssetPath(path.substr(0, skinHash), skinFile))
			{
				CN_LOG_RUNTIME("Asset", "메쉬 에셋 경로 오류 : ", path);
				return {};
			}

			const std::string subPart = path.substr(skinHash + 1);
			const std::string skinKey = skinFile.key + "#" + subPart;
			MeshAssetId skinId = FindMesh(skinKey);
			if (!skinId.IsValid())
			{
				CN_LOG_RUNTIME("Asset", "cnmesh 변환 필요 (glTF 런타임 로드) : ", path);
				CNglTF cnglTF;
				CNglTFImporter glTFImporter;
				std::unique_ptr<MeshAsset> mesh;
				if (glTFImporter.ReadglTF(skinFile.utf8File, cnglTF))
				{
					mesh = glTFImporter.LoadSkinnedMeshAsset(cnglTF, static_cast<uint32_t>(std::stoul(subPart.substr(4))));
				}
				if (!mesh || mesh->GetVertexFormat() != VertexFormat::Skinned)
				{
					CN_LOG_RUNTIME("Asset", "스킨드 메쉬 에셋 로드 오류 : ", path);
					return {};
				}
				WriteDefaultMaterialFiles(*mesh);
				if (!m_GpuRegistry.CreateMeshResource(*mesh).IsValid())
				{
					CN_LOG_RUNTIME("Asset", "메쉬 에셋 Gpu 로드 실패, ", path);
					return {};
				}

				skinId = mesh->GetAssetId();
				m_MeshIdByPath.emplace(skinKey, skinId);
				m_MeshesById.emplace(skinId, AssetEntry<MeshAsset>{ std::move(mesh), skinFile.assetPath + "#" + subPart, skinKey }).first->second.gpuUploaded = true;
			}
			return CreateMeshHandle(skinId, owner);
		}

		// "모델.gltf#node<인덱스>": 그 노드에 달린 메쉬만, 노드 로컬 공간으로(애니메이션되는 노드의 강체 메쉬용).
		const size_t nodeHash = path.find("#node");
		if (nodeHash != std::string::npos)
		{
			ResolvedAssetPath nodeFile;
			if (!ResolveAssetPath(path.substr(0, nodeHash), nodeFile))
			{
				CN_LOG_RUNTIME("Asset", "메쉬 에셋 경로 오류 : ", path);
				return {};
			}

			const std::string subPart = path.substr(nodeHash + 1);
			const std::string nodeKey = nodeFile.key + "#" + subPart;
			MeshAssetId nodeId = FindMesh(nodeKey);
			if (!nodeId.IsValid())
			{
				CN_LOG_RUNTIME("Asset", "cnmesh 변환 필요 (glTF 런타임 로드) : ", path);
				CNglTF cnglTF;
				CNglTFImporter glTFImporter;
				std::unique_ptr<MeshAsset> mesh;
				if (glTFImporter.ReadglTF(nodeFile.utf8File, cnglTF))
				{
					mesh = glTFImporter.LoadNodeMeshAsset(cnglTF, static_cast<uint32_t>(std::stoul(subPart.substr(4))));
				}
				if (!mesh)
				{
					CN_LOG_RUNTIME("Asset", "노드 메쉬 에셋 로드 오류 : ", path);
					return {};
				}
				WriteDefaultMaterialFiles(*mesh);
				if (!m_GpuRegistry.CreateMeshResource(*mesh).IsValid())
				{
					CN_LOG_RUNTIME("Asset", "메쉬 에셋 Gpu 로드 실패, ", path);
					return {};
				}

				nodeId = mesh->GetAssetId();
				m_MeshIdByPath.emplace(nodeKey, nodeId);
				m_MeshesById.emplace(nodeId, AssetEntry<MeshAsset>{ std::move(mesh), nodeFile.assetPath + "#" + subPart, nodeKey }).first->second.gpuUploaded = true;
			}
			return CreateMeshHandle(nodeId, owner);
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

	bool AssetManager::ExportMesh(const std::string& sourcePath, const std::string& targetPath)
	{
		if (!MeshSerializer::IsStaticMeshPath(targetPath) && !MeshSerializer::IsSkinnedMeshPath(targetPath))
		{
			CN_LOG_RUNTIME("Asset", "메쉬 내보내기 실패 : 대상 확장자는 ", MeshSerializer::StaticExtension, " 또는 ",
				MeshSerializer::SkinnedExtension, " 이어야 함, ", targetPath);
			return false;
		}

		const MeshAssetHandle handle = GetMeshHandle(sourcePath, { "", "Console", "mesh.export" });
		const MeshAsset* mesh = GetMesh(handle);
		if (!mesh)
		{
			CN_LOG_RUNTIME("Asset", "메쉬 내보내기 실패 : 원본 메쉬를 불러올 수 없음, ", sourcePath);
			return false;
		}

		const std::filesystem::path file = GetAssetFile(targetPath);
		if (!MeshSerializer::Save(*mesh, ToUtf8(file.generic_u8string())))
		{
			return false;
		}
		CN_LOG_RUNTIME("Asset", "메쉬 내보내기 : ", sourcePath, " -> ", ToAssetPath(ToUtf8(file.generic_u8string())));
		return true;
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
			if (extension == MeshSerializer::StaticExtension || extension == MeshSerializer::SkinnedExtension)
			{
				mesh = MeshSerializer::Load(filePath);
			}

			else if (extension == ".fbx")
			{
				CN_LOG_RUNTIME("Asset", "cnmesh 변환 필요 (FBX 런타임 로드) : ", path);
				mesh = FbxImporter::LoadMesh(filePath);
			}

			else if (extension == ".gltf")
			{
				CN_LOG_RUNTIME("Asset", "cnmesh 변환 필요 (glTF 런타임 로드) : ", path);
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

	//***** AnimationClipAsset
	AnimationClipAssetHandle AssetManager::GetAnimationClipHandle(const std::string& path, const AssetOwnerInfo& owner)
	{
		if (GetLowerExtension(path) == ".aclip")
		{
			ResolvedAssetPath clipPath;
			if (!ResolveAssetPath(path, clipPath))
			{
				CN_LOG_RUNTIME("Asset", "애니메이션 에셋 경로 오류 : ", path);
				return {};
			}

			AnimationClipAssetId clipId = FindAnimationClip(clipPath.key);
			if (!clipId.IsValid())
			{
				std::unique_ptr<AnimationClipAsset> clip = AnimationClipSerializer::Load(clipPath.utf8File);
				if (!clip)
				{
					return {};
				}
				clipId = clip->GetAssetId();
				m_AnimationClipIdByPath.emplace(clipPath.key, clipId);
				m_AnimationClipsById.emplace(clipId, AssetEntry<AnimationClipAsset>{ std::move(clip), clipPath.assetPath, clipPath.key });
			}
			return CreateAnimationClipHandle(clipId, owner);
		}

		const size_t hash = path.find('#');
		const std::string filePart = path.substr(0, hash);
		const std::string subPart = hash == std::string::npos ? "0" : path.substr(hash + 1);

		ResolvedAssetPath resolved;
		if (!ResolveAssetPath(filePart, resolved))
		{
			CN_LOG_RUNTIME("Asset", "애니메이션 에셋 경로 오류 : ", path);
			return {};
		}

		const std::string key = resolved.key + "#" + subPart;
		AnimationClipAssetId id = FindAnimationClip(key);
		if (!id.IsValid())
		{
			// 처음 요청된 파일이면 그 파일의 클립을 전부 등록
			if (!LoadAnimationClips(resolved.assetPath, resolved.key, resolved.utf8File))
				return {};
			id = FindAnimationClip(key);
			if (!id.IsValid())
			{
				CN_LOG_RUNTIME("Asset", "애니메이션 클립 없음 : ", path);
				return {};
			}
		}
		return CreateAnimationClipHandle(id, owner);
	}

	bool AssetManager::LoadAnimationClips(const std::string& assetPath, const std::string& fileKey, const std::string& filePath)
	{
		CNglTF cnglTF;
		CNglTFImporter importer;
		if (!importer.ReadglTF(filePath, cnglTF))
			return false;

		std::vector<std::unique_ptr<AnimationClipAsset>> clips = importer.LoadAnimationClipAsset(cnglTF);  
		for (size_t i = 0; i < clips.size(); ++i)
		{
			const std::string key = fileKey + "#" + std::to_string(i);
			const std::string path = assetPath + "#" + std::to_string(i);
			const AnimationClipAssetId id = clips[i]->GetAssetId();
			m_AnimationClipIdByPath.emplace(key, id);
			m_AnimationClipsById.emplace(id, AssetEntry<AnimationClipAsset>{ std::move(clips[i]), path, key });
		}
		return !clips.empty();
	}

	const AnimationClipAsset* AssetManager::GetAnimationClip(const AnimationClipAssetHandle& handle) const
	{
		return FindAsset(m_AnimationClipsById, handle);
	}

	AnimationClipAssetId AssetManager::FindAnimationClip(const std::string& pathKey) const
	{
		return FindAssetId(m_AnimationClipIdByPath, pathKey);
	}

	AnimationClipAssetHandle AssetManager::CreateAnimationClipHandle(AnimationClipAssetId id, const AssetOwnerInfo& owner)
	{
		return CreateHandle(m_AnimationClipsById, AssetType::AnimationClip, id, owner);
	}

	void AssetManager::Release(AnimationClipAssetId id, AssetRecordId recordId)
	{
		ReleaseReference(m_AnimationClipsById, id, recordId);
	}

	size_t AssetManager::ReleaseUnusedAnimationClips()
	{
		const std::vector<AnimationClipAssetId> unused = CollectUnused(m_AnimationClipsById);
		EraseAssets(m_AnimationClipIdByPath, m_AnimationClipsById, unused); // GPU 리소스가 없으므로 해제 호출 없음
		return unused.size();
	}

	//***** ParticleEffectAsset
	ParticleEffectAssetHandle AssetManager::GetParticleEffectHandle(const std::string& path, const AssetOwnerInfo& owner)
	{
		ResolvedAssetPath effectPath;
		if (!ResolveAssetPath(path, effectPath))
		{
			CN_LOG_RUNTIME("Asset", "파티클 이펙트 에셋 경로 오류 : ", path);
			return {};
		}

		ParticleEffectAssetId effectId = FindParticleEffect(effectPath.key);
		if (!effectId.IsValid())
		{
			std::unique_ptr<ParticleEffectAsset> effect = ParticleEffectAsset::Load(effectPath.utf8File);
			if (!effect)
			{
				return {};
			}
			effectId = effect->GetAssetId();
			m_ParticleEffectIdByPath.emplace(effectPath.key, effectId);
			m_ParticleEffectsById.emplace(effectId, AssetEntry<ParticleEffectAsset>{ std::move(effect), effectPath.assetPath, effectPath.key });
		}
		return CreateParticleEffectHandle(effectId, owner);
	}

	const ParticleEffectAsset* AssetManager::GetParticleEffect(const ParticleEffectAssetHandle& handle) const
	{
		return FindAsset(m_ParticleEffectsById, handle);
	}

	ParticleEffectAssetId AssetManager::FindParticleEffect(const std::string& pathKey) const
	{
		return FindAssetId(m_ParticleEffectIdByPath, pathKey);
	}

	ParticleEffectAssetHandle AssetManager::CreateParticleEffectHandle(ParticleEffectAssetId id, const AssetOwnerInfo& owner)
	{
		return CreateHandle(m_ParticleEffectsById, AssetType::ParticleEffect, id, owner);
	}

	void AssetManager::Release(ParticleEffectAssetId id, AssetRecordId recordId)
	{
		ReleaseReference(m_ParticleEffectsById, id, recordId);
	}

	size_t AssetManager::ReleaseUnusedParticleEffects()
	{
		const std::vector<ParticleEffectAssetId> unused = CollectUnused(m_ParticleEffectsById);
		EraseAssets(m_ParticleEffectIdByPath, m_ParticleEffectsById, unused);
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
		AppendAssetStatuses(m_AnimationClipsById, AssetType::AnimationClip, statuses);
		AppendAssetStatuses(m_ParticleEffectsById, AssetType::ParticleEffect, statuses);
		m_Tracker.PrintRecord(statuses);
	}
	size_t AssetManager::RemapAssetPaths(const std::string& fromPath, const std::string& toPath, bool directory)
	{
		ResolvedAssetPath from;
		ResolvedAssetPath to;
		if (!ResolveAssetPath(fromPath, from) || !ResolveAssetPath(toPath, to))
		{
			return 0;
		}

		const PathRemap remapKey = [&](const std::string& key) { return RemapPrefixed(key, from.key, to.key, directory); };
		const std::string fromAssetLower = ToLowerAscii(from.assetPath);
		const PathRemap remapPath = [&](const std::string& path) { return RemapPrefixed(path, fromAssetLower, to.assetPath, directory); };

		size_t remapped = 0;
		remapped += RemapCache(m_MeshIdByPath, m_MeshesById, remapKey, remapPath);
		remapped += RemapCache(m_TextureIdByPath, m_TexturesById, remapKey, remapPath);
		remapped += RemapCache(m_CubemapIdByPath, m_CubemapsById, remapKey, remapPath);
		remapped += RemapCache(m_AnimationClipIdByPath, m_AnimationClipsById, remapKey, remapPath);
		remapped += RemapCache(m_ParticleEffectIdByPath, m_ParticleEffectsById, remapKey, remapPath);
		remapped += RemapCache(m_MaterialIdByName, m_MaterialsById, remapPath, remapPath);

		size_t slots = 0;
		for (auto& [id, entry] : m_MeshesById)
		{
			const std::vector<MaterialData>& materialSlots = entry.asset->GetMaterialSlots();
			for (size_t slot = 0; slot < materialSlots.size(); ++slot)
			{
				if (std::optional<std::string> name = remapPath(materialSlots[slot].name))
				{
					entry.asset->SetMaterialSlotName(slot, std::move(*name));
					++slots;
				}
			}
		}
		m_Tracker.RemapPaths(remapPath);

		if (remapped > 0 || slots > 0)
		{
			CN_LOG_RUNTIME("Asset", "에셋 경로 갱신 : ", from.assetPath, " -> ", to.assetPath, ", 캐시 ", remapped, "개, 메쉬 슬롯 ", slots, "개");
		}
		return remapped;
	}

	size_t AssetManager::ForgetAssetPaths(const std::string& path, bool directory)
	{
		ResolvedAssetPath target;
		if (!ResolveAssetPath(path, target))
		{
			return 0;
		}

		const std::string assetLower = ToLowerAscii(target.assetPath);
		const auto matchesKey = [&](const std::string& key) { return RemapPrefixed(key, target.key, std::string(), directory).has_value(); };
		const auto matchesPath = [&](const std::string& value) { return RemapPrefixed(value, assetLower, std::string(), directory).has_value(); };
		size_t forgotten = 0;
		forgotten += ForgetCache(m_MeshIdByPath, m_MeshesById, matchesKey);
		forgotten += ForgetCache(m_TextureIdByPath, m_TexturesById, matchesKey);
		forgotten += ForgetCache(m_CubemapIdByPath, m_CubemapsById, matchesKey);
		forgotten += ForgetCache(m_AnimationClipIdByPath, m_AnimationClipsById, matchesKey);
		forgotten += ForgetCache(m_ParticleEffectIdByPath, m_ParticleEffectsById, matchesKey);
		forgotten += ForgetCache(m_MaterialIdByName, m_MaterialsById, matchesPath);

		size_t slots = 0;
		for (auto& [id, entry] : m_MeshesById)
		{
			const std::vector<MaterialData>& materialSlots = entry.asset->GetMaterialSlots();
			for (size_t slot = 0; slot < materialSlots.size(); ++slot)
			{
				if (matchesPath(materialSlots[slot].name))
				{
					entry.asset->SetMaterialSlotName(slot, std::string());
					++slots;
				}
			}
		}

		if (forgotten > 0 || slots > 0)
		{
			CN_LOG_RUNTIME("Asset", "삭제된 에셋 캐시 분리 : ", target.assetPath, ", 캐시 ", forgotten, "개, 메쉬 슬롯 ", slots, "개");
		}
		return forgotten;
	}

	size_t AssetManager::ReleaseUnusedAssets()
	{
		size_t released = ReleaseUnusedMeshes();
		released += ReleaseUnusedMaterials();
		released += ReleaseUnusedTextures();
		released += ReleaseUnusedCubemaps();
		released += ReleaseUnusedAnimationClips();
		released += ReleaseUnusedParticleEffects();
		return released;
	}
}
