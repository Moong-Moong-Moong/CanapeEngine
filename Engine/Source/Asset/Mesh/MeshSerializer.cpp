#include "Asset/Mesh/MeshSerializer.h"
#include "Asset/Mesh/MeshAsset.h"
#include "CNDebug/DebugLog.h"

#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace Canape
{
	namespace
	{
		constexpr char kMagic[4] = { 'C', 'N', 'M', 'S' };
		constexpr uint32_t kFormatStatic = 0;
		constexpr uint32_t kFormatSkinned = 1;

		struct FileHeader
		{
			char magic[4];
			uint32_t version;
			uint32_t vertexFormat;
			uint32_t vertexStride;
			uint32_t vertexCount;
			uint32_t indexCount;
			uint32_t subMeshCount;
			uint32_t boneCount;
			uint32_t materialSlotCount;
		};

		static_assert(sizeof(FileHeader) == 36);
		static_assert(std::is_trivially_copyable_v<SubMesh> && sizeof(SubMesh) == 16);
		static_assert(std::is_trivially_copyable_v<StaticVertex> && sizeof(StaticVertex) == 48);
		static_assert(std::is_trivially_copyable_v<SkinnedVertex> && sizeof(SkinnedVertex) == 72);
		static_assert(std::is_trivially_copyable_v<Matrix4> && sizeof(Matrix4) == 64);

		std::filesystem::path ToPath(const std::string& utf8)
		{
			return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
		}

		bool HasExtension(const std::string& path, std::string_view extension)
		{
			if (path.size() < extension.size())
				return false;
			for (size_t i = 0; i < extension.size(); ++i)
			{
				char c = path[path.size() - extension.size() + i];
				if (c >= 'A' && c <= 'Z')
					c = static_cast<char>(c - 'A' + 'a');
				if (c != extension[i])
					return false;
			}
			return true;
		}

		class Writer
		{
		public:
			template <typename T>
			void Value(const T& value)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				Bytes(&value, sizeof(T));
			}

			template <typename T>
			void Array(std::span<const T> values)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				Bytes(values.data(), values.size_bytes());
			}

			void String(const std::string& value)
			{
				Value(static_cast<uint32_t>(value.size()));
				Bytes(value.data(), value.size());
			}

			const std::vector<uint8_t>& Data() const { return m_Data; }

		private:
			void Bytes(const void* data, size_t size)
			{
				const uint8_t* bytes = static_cast<const uint8_t*>(data);
				m_Data.insert(m_Data.end(), bytes, bytes + size);
			}

			std::vector<uint8_t> m_Data;
		};

		class Reader
		{
		public:
			explicit Reader(std::span<const uint8_t> data) : m_Data(data) {}

			template <typename T>
			bool Value(T& value)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				return Bytes(&value, sizeof(T));
			}

			template <typename T>
			bool Array(std::vector<T>& values, uint32_t count)
			{
				static_assert(std::is_trivially_copyable_v<T>);
				if (count > Remaining() / sizeof(T))
					return false;
				values.resize(count);
				return Bytes(values.data(), size_t(count) * sizeof(T));
			}

			bool String(std::string& value)
			{
				uint32_t length = 0;
				if (!Value(length) || length > Remaining())
					return false;
				value.assign(reinterpret_cast<const char*>(m_Data.data() + m_Offset), length);
				m_Offset += length;
				return true;
			}

			size_t Remaining() const { return m_Data.size() - m_Offset; }

		private:
			bool Bytes(void* destination, size_t size)
			{
				if (size > Remaining())
					return false;
				if (size > 0)
					std::memcpy(destination, m_Data.data() + m_Offset, size);
				m_Offset += size;
				return true;
			}

			std::span<const uint8_t> m_Data;
			size_t m_Offset = 0;
		};

		bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& data)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
				return false;
			const std::streamoff size = file.tellg();
			if (size < 0)
				return false;
			data.resize(static_cast<size_t>(size));
			file.seekg(0);
			return data.empty() || file.read(reinterpret_cast<char*>(data.data()), size).good();
		}
	}

	bool MeshSerializer::IsStaticMeshPath(const std::string& path)
	{
		return HasExtension(path, StaticExtension);
	}

	bool MeshSerializer::IsSkinnedMeshPath(const std::string& path)
	{
		return HasExtension(path, SkinnedExtension);
	}

	const char* MeshSerializer::ExtensionFor(const MeshAsset& mesh)
	{
		return mesh.GetVertexFormat() == VertexFormat::Skinned ? SkinnedExtension : StaticExtension;
	}

	bool MeshSerializer::Save(const MeshAsset& mesh, const std::string& path)
	{
		const bool skinned = mesh.GetVertexFormat() == VertexFormat::Skinned;
		if (!HasExtension(path, ExtensionFor(mesh)))
		{
			CN_LOG_RUNTIME("Asset", "메쉬 저장 실패 : ", skinned ? "스킨드" : "정적", " 메쉬는 ", ExtensionFor(mesh),
				" 확장자로 저장해야 함, ", path);
			return false;
		}
		if (skinned && mesh.GetInverseBindMatrices().size() != mesh.GetBonePaths().size())
		{
			CN_LOG_RUNTIME("Asset", "메쉬 저장 실패 : 본 경로와 inverse bind 개수 불일치, ", path);
			return false;
		}

		FileHeader header{};
		std::memcpy(header.magic, kMagic, sizeof(kMagic));
		header.version = Version;
		header.vertexFormat = skinned ? kFormatSkinned : kFormatStatic;
		header.vertexStride = skinned ? sizeof(SkinnedVertex) : sizeof(StaticVertex);
		header.vertexCount = mesh.GetVertexCount();
		header.indexCount = mesh.GetIndexCount();
		header.subMeshCount = static_cast<uint32_t>(mesh.GetSubMeshes().size());
		header.boneCount = skinned ? mesh.GetBoneCount() : 0;
		header.materialSlotCount = static_cast<uint32_t>(mesh.GetMaterialSlots().size());

		Writer writer;
		writer.Value(header);
		writer.Array(std::span<const SubMesh>(mesh.GetSubMeshes()));
		if (skinned)
			writer.Array(std::span<const SkinnedVertex>(mesh.GetSkinnedVertices()));
		else
			writer.Array(std::span<const StaticVertex>(mesh.GetVertices()));
		writer.Array(std::span<const uint32_t>(mesh.GetIndices()));
		if (skinned)
		{
			writer.Array(std::span<const Matrix4>(mesh.GetInverseBindMatrices()));
			for (const std::string& bonePath : mesh.GetBonePaths())
				writer.String(bonePath);
		}
		for (const MaterialData& slot : mesh.GetMaterialSlots())
			writer.String(slot.name);

		const std::filesystem::path filePath = ToPath(path);
		std::error_code error;
		if (filePath.has_parent_path())
			std::filesystem::create_directories(filePath.parent_path(), error);

		std::filesystem::path tempPath = filePath;
		tempPath += ".tmp";
		{
			std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
			const std::vector<uint8_t>& data = writer.Data();
			if (!file || !file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size())))
			{
				CN_LOG_RUNTIME("Asset", "메쉬 저장 실패 : 파일 쓰기 오류, ", path);
				std::filesystem::remove(tempPath, error);
				return false;
			}
		}

		std::filesystem::rename(tempPath, filePath, error);
		if (error)
		{
			CN_LOG_RUNTIME("Asset", "메쉬 저장 실패 : 파일 교체 오류, ", path);
			std::filesystem::remove(tempPath, error);
			return false;
		}
		return true;
	}

	std::unique_ptr<MeshAsset> MeshSerializer::Load(const std::string& path)
	{
		std::vector<uint8_t> data;
		if (!ReadFile(ToPath(path), data))
		{
			CN_LOG_RUNTIME("Asset", "메쉬 로드 실패 : 파일을 열 수 없음, ", path);
			return nullptr;
		}

		const auto fail = [&path](const char* reason) -> std::unique_ptr<MeshAsset>
		{
			CN_LOG_RUNTIME("Asset", "메쉬 로드 실패 : ", reason, ", ", path);
			return nullptr;
		};

		Reader reader(data);
		FileHeader header{};
		if (!reader.Value(header) || std::memcmp(header.magic, kMagic, sizeof(kMagic)) != 0)
			return fail("cnmesh 형식이 아님");
		if (header.version != Version)
			return fail("지원하지 않는 cnmesh 버전");

		const bool skinned = header.vertexFormat == kFormatSkinned;
		if (!skinned && header.vertexFormat != kFormatStatic)
			return fail("알 수 없는 정점 형식");
		if (header.vertexStride != (skinned ? sizeof(SkinnedVertex) : sizeof(StaticVertex)))
			return fail("정점 크기 불일치");
		if (!skinned && header.boneCount != 0)
			return fail("정적 메쉬에 본 데이터가 있음");
		if (!HasExtension(path, skinned ? SkinnedExtension : StaticExtension))
			return fail(skinned ? "스킨드 메쉬인데 확장자가 .cnskmesh가 아님" : "정적 메쉬인데 확장자가 .cnmesh가 아님");

		std::vector<SubMesh> subMeshes;
		std::vector<StaticVertex> staticVertices;
		std::vector<SkinnedVertex> skinnedVertices;
		std::vector<uint32_t> indices;
		std::vector<Matrix4> inverseBindMatrices;
		std::vector<std::string> bonePaths(header.boneCount);
		std::vector<MaterialData> materialSlots(header.materialSlotCount);

		if (!reader.Array(subMeshes, header.subMeshCount))
			return fail("서브메쉬 데이터 손상");
		const bool verticesRead = skinned
			? reader.Array(skinnedVertices, header.vertexCount)
			: reader.Array(staticVertices, header.vertexCount);
		if (!verticesRead)
			return fail("정점 데이터 손상");
		if (!reader.Array(indices, header.indexCount))
			return fail("인덱스 데이터 손상");
		if (skinned)
		{
			if (!reader.Array(inverseBindMatrices, header.boneCount))
				return fail("inverse bind 데이터 손상");
			for (std::string& bonePath : bonePaths)
			{
				if (!reader.String(bonePath))
					return fail("본 경로 데이터 손상");
			}
		}
		for (MaterialData& slot : materialSlots)
		{
			if (!reader.String(slot.name))
				return fail("머티리얼 슬롯 데이터 손상");
		}
		if (reader.Remaining() != 0)
			return fail("파일 끝에 알 수 없는 데이터");

		try
		{
			if (skinned)
			{
				return std::make_unique<MeshAsset>(std::move(skinnedVertices), std::move(indices), std::move(subMeshes),
					std::move(inverseBindMatrices), std::move(bonePaths), std::move(materialSlots));
			}
			return std::make_unique<MeshAsset>(std::move(staticVertices), std::move(indices), std::move(subMeshes),
				std::move(materialSlots));
		}
		catch (const std::exception& exception)
		{
			return fail(exception.what());
		}
	}
}
