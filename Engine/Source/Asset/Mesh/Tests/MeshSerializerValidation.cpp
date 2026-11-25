#include "Asset/Mesh/MeshAsset.h"
#include "Asset/Mesh/MeshSerializer.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace Canape;

namespace
{
	int g_Failures = 0;

	void Require(bool condition, const char* message)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", message);
			++g_Failures;
		}
	}

	template <typename T>
	bool SameBytes(const std::vector<T>& a, const std::vector<T>& b)
	{
		return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(T)) == 0);
	}

	bool SameSlots(const MeshAsset& a, const MeshAsset& b)
	{
		if (a.GetMaterialSlots().size() != b.GetMaterialSlots().size())
			return false;
		for (size_t i = 0; i < a.GetMaterialSlots().size(); ++i)
		{
			if (a.GetMaterialSlots()[i].name != b.GetMaterialSlots()[i].name)
				return false;
		}
		return true;
	}

	std::vector<uint8_t> ReadAll(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		return std::vector<uint8_t>(std::istreambuf_iterator<char>(file), {});
	}

	void WriteAll(const std::filesystem::path& path, const std::vector<uint8_t>& data)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
	}

	std::string Utf8(const std::filesystem::path& path)
	{
		const std::u8string text = path.generic_u8string();
		return std::string(text.begin(), text.end());
	}

	MaterialData Slot(const std::string& name)
	{
		MaterialData slot;
		slot.name = name;
		slot.baseColorTexture = "ignored.png";
		return slot;
	}

	std::unique_ptr<MeshAsset> MakeStatic()
	{
		std::vector<StaticVertex> vertices(4);
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			const float f = static_cast<float>(i);
			vertices[i] = { { f, f + 0.5f, -f }, { 0.0f, 1.0f, 0.0f }, { f * 0.25f, 1.0f - f * 0.25f }, { 1.0f, 0.0f, 0.0f, -1.0f } };
		}
		std::vector<uint32_t> indices = { 0, 1, 2, 0, 2, 3 };
		std::vector<SubMesh> subMeshes = { { 0, 3, 0, 0 }, { 3, 3, 0, 1 } };
		std::vector<MaterialData> slots = { Slot("Content/Models/Tree/Bark.mat"), Slot("Content/Models/\xEB\x82\x98\xEB\xAC\xB4/\xEC\x9E\x8E.mat") };
		return std::make_unique<MeshAsset>(std::move(vertices), std::move(indices), std::move(subMeshes), std::move(slots));
	}

	std::unique_ptr<MeshAsset> MakeSkinned()
	{
		std::vector<SkinnedVertex> vertices(3);
		for (size_t i = 0; i < vertices.size(); ++i)
		{
			const float f = static_cast<float>(i);
			SkinnedVertex& v = vertices[i];
			v.Position = { f, 2.0f * f, 3.0f * f };
			v.Normal = { 0.0f, 0.0f, 1.0f };
			v.UV = { f, -f };
			v.Tangent = { 0.0f, 1.0f, 0.0f, 1.0f };
			v.Joints[0] = static_cast<uint16_t>(i % 2);
			v.Joints[1] = 1;
			v.Joints[2] = 0;
			v.Joints[3] = 0;
			v.Weights[0] = 0.75f;
			v.Weights[1] = 0.25f;
			v.Weights[2] = 0.0f;
			v.Weights[3] = 0.0f;
		}
		Matrix4 bind0 = Matrix4::Identity;
		Matrix4 bind1 = Matrix4::Identity;
		bind1.M[3][0] = 1.5f;
		bind1.M[3][1] = -2.0f;
		return std::make_unique<MeshAsset>(std::move(vertices), std::vector<uint32_t>{ 0, 1, 2 }, std::vector<SubMesh>{},
			std::vector<Matrix4>{ bind0, bind1 }, std::vector<std::string>{ "Armature/Hips", "Armature/Hips/Spine" },
			std::vector<MaterialData>{ Slot("Content/Models/Knight/Body.mat") });
	}

	void TestStaticRoundTrip(const std::filesystem::path& dir)
	{
		const auto source = MakeStatic();
		const std::string path = Utf8(dir / "static.cnmesh");
		Require(MeshSerializer::Save(*source, path), "static save");
		const auto loaded = MeshSerializer::Load(path);
		Require(loaded != nullptr, "static load");
		if (!loaded) return;
		Require(loaded->GetVertexFormat() == VertexFormat::Static, "static format");
		Require(SameBytes(source->GetVertices(), loaded->GetVertices()), "static vertices");
		Require(SameBytes(source->GetIndices(), loaded->GetIndices()), "static indices");
		Require(SameBytes(source->GetSubMeshes(), loaded->GetSubMeshes()), "static submeshes");
		Require(SameSlots(*source, *loaded), "static material slot names");
		Require(loaded->GetMaterialSlots()[0].baseColorTexture.empty(), "only slot names are stored");
		Require(loaded->GetBoneCount() == 0, "static has no bones");
		Require(loaded->GetAssetId().value != source->GetAssetId().value, "loaded mesh gets a fresh asset id");

		Require(MeshSerializer::Save(*loaded, path), "static resave");
		const auto reloaded = MeshSerializer::Load(path);
		Require(reloaded && SameBytes(source->GetVertices(), reloaded->GetVertices()), "static second round trip");
	}

	void TestSkinnedRoundTrip(const std::filesystem::path& dir)
	{
		const auto source = MakeSkinned();
		const std::string path = Utf8(dir / "skinned.cnskmesh");
		Require(MeshSerializer::Save(*source, path), "skinned save");
		const auto loaded = MeshSerializer::Load(path);
		Require(loaded != nullptr, "skinned load");
		if (!loaded) return;
		Require(loaded->GetVertexFormat() == VertexFormat::Skinned, "skinned format");
		Require(SameBytes(source->GetSkinnedVertices(), loaded->GetSkinnedVertices()), "skinned vertices");
		Require(SameBytes(source->GetIndices(), loaded->GetIndices()), "skinned indices");
		Require(SameBytes(source->GetSubMeshes(), loaded->GetSubMeshes()), "skinned default submesh");
		Require(SameBytes(source->GetInverseBindMatrices(), loaded->GetInverseBindMatrices()), "inverse bind matrices");
		Require(source->GetBonePaths() == loaded->GetBonePaths(), "bone paths");
		Require(SameSlots(*source, *loaded), "skinned material slot names");
	}

	void TestMismatchedBonesRejected(const std::filesystem::path& dir)
	{
		std::vector<SkinnedVertex> vertices(3);
		const MeshAsset mesh(std::move(vertices), std::vector<uint32_t>{ 0, 1, 2 }, std::vector<SubMesh>{},
			std::vector<Matrix4>{ Matrix4::Identity, Matrix4::Identity }, std::vector<std::string>{ "Only" });
		const std::filesystem::path path = dir / "mismatch.cnskmesh";
		Require(!MeshSerializer::Save(mesh, Utf8(path)), "mismatched bone data is not saved");
		Require(!std::filesystem::exists(path), "no file left after a failed save");
	}

	void TestCorruptFilesRejected(const std::filesystem::path& dir)
	{
		const std::filesystem::path validPath = dir / "skinned.cnskmesh";
		const std::vector<uint8_t> valid = ReadAll(validPath);
		Require(valid.size() > 36, "valid file present for corruption tests");
		const std::filesystem::path path = dir / "corrupt.cnskmesh";

		for (size_t size = 0; size < valid.size(); ++size)
		{
			WriteAll(path, std::vector<uint8_t>(valid.begin(), valid.begin() + static_cast<std::ptrdiff_t>(size)));
			if (MeshSerializer::Load(Utf8(path)))
			{
				std::printf("FAIL: truncated file of %zu bytes was accepted\n", size);
				++g_Failures;
				break;
			}
		}

		std::vector<uint8_t> data = valid;
		data.push_back(0);
		WriteAll(path, data);
		Require(!MeshSerializer::Load(Utf8(path)), "trailing byte rejected");

		data = valid;
		data[0] = 'X';
		WriteAll(path, data);
		Require(!MeshSerializer::Load(Utf8(path)), "bad magic rejected");

		data = valid;
		data[4] = static_cast<uint8_t>(MeshSerializer::Version + 1);
		WriteAll(path, data);
		Require(!MeshSerializer::Load(Utf8(path)), "unknown version rejected");

		data = valid;
		data[16] = 0xFF;
		data[17] = 0xFF;
		data[18] = 0xFF;
		data[19] = 0x7F;
		WriteAll(path, data);
		Require(!MeshSerializer::Load(Utf8(path)), "huge vertex count rejected without allocation");

		data = valid;
		const size_t indexOffset = 36 + 16 + 3 * sizeof(SkinnedVertex);
		data[indexOffset] = 9;
		WriteAll(path, data);
		Require(!MeshSerializer::Load(Utf8(path)), "out of range index rejected");

		Require(!MeshSerializer::Load(Utf8(dir / "missing.cnmesh")), "missing file rejected");
	}

	void TestExtensionRules(const std::filesystem::path& dir)
	{
		Require(MeshSerializer::IsStaticMeshPath("Content/A.cnmesh") && !MeshSerializer::IsSkinnedMeshPath("Content/A.cnmesh"), "static path detected");
		Require(MeshSerializer::IsSkinnedMeshPath("Content/A.CnSkMesh") && !MeshSerializer::IsStaticMeshPath("Content/A.CnSkMesh"), "skinned path detected case-insensitively");
		Require(!MeshSerializer::IsStaticMeshPath("Content/A.gltf#skin0") && !MeshSerializer::IsSkinnedMeshPath("Content/A.gltf#skin0"), "legacy path is neither");

		const auto skinned = MakeSkinned();
		const auto staticMesh = MakeStatic();
		Require(std::string(MeshSerializer::ExtensionFor(*skinned)) == ".cnskmesh", "skinned extension");
		Require(std::string(MeshSerializer::ExtensionFor(*staticMesh)) == ".cnmesh", "static extension");

		Require(!MeshSerializer::Save(*skinned, Utf8(dir / "wrong_skinned.cnmesh")), "skinned mesh not saved as .cnmesh");
		Require(!std::filesystem::exists(dir / "wrong_skinned.cnmesh"), "no file after wrong-extension save");
		Require(!MeshSerializer::Save(*staticMesh, Utf8(dir / "wrong_static.cnskmesh")), "static mesh not saved as .cnskmesh");

		std::filesystem::copy_file(dir / "skinned.cnskmesh", dir / "renamed_skinned.cnmesh", std::filesystem::copy_options::overwrite_existing);
		Require(!MeshSerializer::Load(Utf8(dir / "renamed_skinned.cnmesh")), "skinned file renamed to .cnmesh rejected");
		std::filesystem::copy_file(dir / "static.cnmesh", dir / "renamed_static.cnskmesh", std::filesystem::copy_options::overwrite_existing);
		Require(!MeshSerializer::Load(Utf8(dir / "renamed_static.cnskmesh")), "static file renamed to .cnskmesh rejected");

		std::filesystem::copy_file(dir / "skinned.cnskmesh", dir / "UPPER.CNSKMESH", std::filesystem::copy_options::overwrite_existing);
		Require(MeshSerializer::Load(Utf8(dir / "UPPER.CNSKMESH")) != nullptr, "upper-case extension loads");
	}
}

int main(int argc, char** argv)
{
	const std::filesystem::path dir = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path() / "MeshSerializerValidation";
	std::error_code error;
	std::filesystem::remove_all(dir, error);
	std::filesystem::create_directories(dir);

	TestStaticRoundTrip(dir);
	TestSkinnedRoundTrip(dir);
	TestMismatchedBonesRejected(dir);
	TestCorruptFilesRejected(dir);
	TestExtensionRules(dir);

	if (g_Failures == 0)
	{
		std::printf("MeshSerializer validation passed\n");
		return 0;
	}
	std::printf("MeshSerializer validation failed: %d\n", g_Failures);
	return 1;
}
