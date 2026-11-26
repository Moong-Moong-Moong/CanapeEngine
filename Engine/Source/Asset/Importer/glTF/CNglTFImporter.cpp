#include "CNglTFImporter.h"
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <algorithm>
#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <vector>
#include "../../Mesh/MeshAsset.h"
#include "../../Texture/TextureAsset.h"
#include "Asset/AssetManager.h"
#include <rapidjson/filereadstream.h>
#include "Scene/Scene.h"
#include "Scene/GameObject/GameObject.h"
#include "Scene/Component/TransformComponent.h"
#include "Scene/Component/SkinnedMeshRendererComponent.h"
#include "Scene/Component/AnimatorComponent.h"
#include "Scene/Serialization/SceneSerializer.h"
#include "Asset/Mesh/MeshSerializer.h"
#include "CNDebug/DebugLog.h"

using namespace rapidjson;

namespace Canape
{
	namespace
	{
		// ===== [오류 처리] =====
		// glTF 데이터가 잘못되었을 때 rapidjson assert나 범위 밖 읽기로 프로그램이 꺼지지 않도록,
		// 값을 꺼내기 전에 모두 검사하고 문제가 있으면 GltfError를 던진다.
		// 공개 함수들은 GltfError(및 std::exception)를 잡아 CNDebug에 원인을 출력하고 실패 값을 돌려준다.
		struct GltfError : std::runtime_error
		{
			using std::runtime_error::runtime_error;
		};

		template <typename... Args>
		std::string Concat(const Args&... args)
		{
			std::ostringstream stream;
			(stream << ... << args);
			return stream.str();
		}

		template <typename... Args>
		[[noreturn]] void Fail(const Args&... args)
		{
			throw GltfError(Concat(args...));
		}

		// CN_LOG는 Debug 빌드에서만 출력된다.
		template <typename... Args>
		void Warn(const Args&... args)
		{
			(static_cast<void>(args), ...);
			CN_LOG_RUNTIME("Import", "glTF 경고 : ", Concat(args...));
		}

		void LogFailure(const char* stage, const std::string& file, const char* reason)
		{
			static_cast<void>(stage);
			static_cast<void>(file);
			static_cast<void>(reason);
			CN_LOG_RUNTIME("Import", "glTF ", stage, " 실패 (", file, ") : ", reason);
		}

		bool Has(const Value& object, const char* key)
		{
			return object.IsObject() && object.HasMember(key);
		}

		Value& Member(Value& object, const char* key, const std::string& where)
		{
			if (!object.IsObject())
				Fail(where, " 가 JSON 객체가 아님");
			if (!object.HasMember(key))
				Fail(where, " 에 '", key, "' 항목이 없음");
			return object[key];
		}

		Value& ArrayMember(Value& object, const char* key, const std::string& where)
		{
			Value& value = Member(object, key, where);
			if (!value.IsArray())
				Fail(where, ".", key, " 가 배열이 아님");
			return value;
		}

		Value& At(Value& array, uint32_t index, const std::string& where)
		{
			if (!array.IsArray())
				Fail(where, " 가 배열이 아님");
			if (index >= array.Size())
				Fail(where, "[", index, "] 인덱스 범위 초과 (크기 ", array.Size(), ")");
			return array[index];
		}

		uint32_t ToUint(const Value& value, const std::string& where)
		{
			if (!value.IsUint())
				Fail(where, " 가 0 이상의 정수가 아님");
			return value.GetUint();
		}

		float ToFloat(const Value& value, const std::string& where)
		{
			if (!value.IsNumber())
				Fail(where, " 가 숫자가 아님");
			return value.GetFloat();
		}

		uint32_t UintOf(Value& object, const char* key, const std::string& where)
		{
			return ToUint(Member(object, key, where), where + "." + key);
		}

		uint32_t UintOr(Value& object, const char* key, uint32_t fallback, const std::string& where)
		{
			return Has(object, key) ? ToUint(object[key], where + "." + key) : fallback;
		}

		// 선택 항목: 타입이 틀리면 경고 후 기본값.
		float FloatOr(Value& object, const char* key, float fallback, const std::string& where)
		{
			if (!Has(object, key))
				return fallback;
			if (!object[key].IsNumber())
			{
				Warn(where, ".", key, " 가 숫자가 아니라 기본값 ", fallback, " 사용");
				return fallback;
			}
			return object[key].GetFloat();
		}

		bool BoolOr(Value& object, const char* key, bool fallback, const std::string& where)
		{
			if (!Has(object, key))
				return fallback;
			if (!object[key].IsBool())
			{
				Warn(where, ".", key, " 가 bool이 아니라 기본값 사용");
				return fallback;
			}
			return object[key].GetBool();
		}

		std::string StringOr(Value& object, const char* key, const std::string& fallback, const std::string& where)
		{
			if (!Has(object, key))
				return fallback;
			if (!object[key].IsString())
			{
				Warn(where, ".", key, " 가 문자열이 아니라 기본값 사용");
				return fallback;
			}
			return object[key].GetString();
		}

		// doc[arrayKey][index]
		Value& Element(Value& doc, const char* arrayKey, uint32_t index, const std::string& where)
		{
			return At(ArrayMember(doc, arrayKey, "glTF"), index, Concat(arrayKey, " (", where, ")"));
		}

		// node[key] 가 길이 count 이상의 숫자 배열이면 out에 읽는다.
		void ReadFloats(Value& object, const char* key, uint32_t count, float* out, const std::string& where)
		{
			Value& array = ArrayMember(object, key, where);
			if (array.Size() < count)
				Fail(where, ".", key, " 원소 개수 부족 (필요 ", count, ", 실제 ", array.Size(), ")");
			for (uint32_t i = 0; i < count; ++i)
				out[i] = ToFloat(array[i], Concat(where, ".", key, "[", i, "]"));
		}

		// ===== [Accessor] 범위를 검사하는 데이터 읽기 =====
		constexpr uint32_t kByte = 5120, kUnsignedByte = 5121, kShort = 5122, kUnsignedShort = 5123, kUnsignedInt = 5125, kFloat = 5126;

		uint32_t ComponentSize(uint32_t componentType)
		{
			switch (componentType)
			{
			case kByte: case kUnsignedByte: return 1;
			case kShort: case kUnsignedShort: return 2;
			case kUnsignedInt: case kFloat: return 4;
			default: return 0;
			}
		}

		uint32_t ComponentCount(const std::string& type)
		{
			if (type == "SCALAR") return 1;
			if (type == "VEC2") return 2;
			if (type == "VEC3") return 3;
			if (type == "VEC4") return 4;
			if (type == "MAT2") return 4;
			if (type == "MAT3") return 9;
			if (type == "MAT4") return 16;
			return 0;
		}

		struct AccessorData
		{
			const uint8_t* data = nullptr; // 첫 원소
			uint32_t count = 0;
			size_t stride = 0;
			uint32_t componentType = 0;

			const uint8_t* Element(uint32_t i) const { return data + static_cast<size_t>(i) * stride; }
		};

		// accessor를 검사해서 bin 안의 안전한 읽기 범위를 돌려준다. 원소 [0, count)는 모두 bin 안에 있음이 보장된다.
		// vertexAttribute: 정점 속성만 bufferView.byteStride를 쓸 수 있다(인덱스, 애니메이션, IBM은 항상 촘촘함).
		AccessorData ReadAccessor(CNglTF& cNglTF, uint32_t accessorIndex, const std::string& what, const char* expectedType,
			std::initializer_list<uint32_t> allowedComponents, bool vertexAttribute)
		{
			Value& doc = cNglTF.m_doc;
			const std::string where = Concat(what, " (accessor ", accessorIndex, ")");
			Value& accessor = Element(doc, "accessors", accessorIndex, where);

			const uint32_t count = UintOf(accessor, "count", where);
			const uint32_t componentType = UintOf(accessor, "componentType", where);
			const std::string type = StringOr(accessor, "type", "", where);
			if (expectedType && type != expectedType)
				Fail(where, " 의 type이 ", type.empty() ? "(없음)" : type, " (", expectedType, " 필요)");
			if (std::find(allowedComponents.begin(), allowedComponents.end(), componentType) == allowedComponents.end())
				Fail(where, " 의 componentType ", componentType, " 은 지원하지 않음 (양자화/정규화 데이터 미지원)");
			if (Has(accessor, "sparse"))
				Fail(where, " 는 sparse accessor (지원하지 않음)");
			if (!Has(accessor, "bufferView"))
				Fail(where, " 에 bufferView가 없음 (sparse/압축(Draco 등) 데이터는 지원하지 않음)");

			const size_t elementSize = static_cast<size_t>(ComponentSize(componentType)) * ComponentCount(type);
			if (elementSize == 0)
				Fail(where, " 의 type/componentType 조합이 잘못됨 (", type, ", ", componentType, ")");

			const uint32_t viewIndex = UintOf(accessor, "bufferView", where);
			Value& view = Element(doc, "bufferViews", viewIndex, where);
			const std::string viewWhere = Concat("bufferViews[", viewIndex, "]");
			const uint32_t bufferIndex = UintOr(view, "buffer", 0u, viewWhere);
			if (bufferIndex != 0)
				Fail(viewWhere, " 가 buffers[", bufferIndex, "]를 가리킴 (buffers[0]만 지원)");
			const uint64_t viewOffset = UintOr(view, "byteOffset", 0u, viewWhere);
			const uint64_t viewLength = UintOf(view, "byteLength", viewWhere);
			if (viewOffset + viewLength > cNglTF.m_bin.size())
				Fail(viewWhere, " 범위(", viewOffset, " + ", viewLength, ")가 bin 크기 ", cNglTF.m_bin.size(), " 를 넘음");

			size_t stride = elementSize;
			if (Has(view, "byteStride"))
			{
				const uint32_t byteStride = UintOf(view, "byteStride", viewWhere);
				if (vertexAttribute)
				{
					if (byteStride < elementSize)
						Fail(viewWhere, ".byteStride ", byteStride, " 가 원소 크기 ", elementSize, " 보다 작음");
					stride = byteStride;
				}
				else if (byteStride != elementSize)
				{
					Warn(viewWhere, ".byteStride 는 정점 속성에만 쓸 수 있어 무시함 (", what, ")");
				}
			}

			const uint64_t accessorOffset = UintOr(accessor, "byteOffset", 0u, where);
			if (count > 0)
			{
				const uint64_t end = accessorOffset + static_cast<uint64_t>(count - 1) * stride + elementSize;
				if (end > viewLength)
					Fail(where, " 데이터(offset ", accessorOffset, ", count ", count, ", stride ", stride,
						")가 bufferView 길이 ", viewLength, " 를 넘음");
			}

			AccessorData data;
			data.data = cNglTF.m_bin.data() + viewOffset + accessorOffset;
			data.count = count;
			data.stride = stride;
			data.componentType = componentType;
			return data;
		}

		// 인덱스 accessor를 읽어 out 끝에 붙인다. primitive에 indices가 없으면 0..vertexCount-1을 만든다.
		void ReadIndices(CNglTF& cNglTF, Value& primitive, uint32_t vertexCount, std::vector<uint32_t>& out, const std::string& where)
		{
			const size_t start = out.size();
			if (!Has(primitive, "indices"))
			{
				out.resize(start + vertexCount);
				for (uint32_t i = 0; i < vertexCount; ++i)
					out[start + i] = i;
				return;
			}

			const AccessorData indices = ReadAccessor(cNglTF, UintOf(primitive, "indices", where), where + " indices", "SCALAR",
				{ kUnsignedByte, kUnsignedShort, kUnsignedInt }, false);
			out.resize(start + indices.count);
			for (uint32_t i = 0; i < indices.count; ++i)
			{
				uint32_t value = 0;
				if (indices.componentType == kUnsignedInt)
					std::memcpy(&value, indices.Element(i), sizeof(uint32_t));
				else if (indices.componentType == kUnsignedShort)
				{
					uint16_t shortValue;
					std::memcpy(&shortValue, indices.Element(i), sizeof(uint16_t));
					value = shortValue;
				}
				else
					value = *indices.Element(i);

				if (value >= vertexCount)
					Fail(where, " 인덱스[", i, "] 값 ", value, " 이 정점 수 ", vertexCount, " 를 넘음");
				out[start + i] = value;
			}
			if (indices.count % 3 != 0)
				Fail(where, " 인덱스 개수 ", indices.count, " 가 3의 배수가 아님");
		}

		// 삼각형 목록(mode 4)만 지원한다. 그 외(점/선/strip/fan)는 경고 후 건너뛴다.
		bool IsTrianglePrimitive(Value& primitive, const std::string& where)
		{
			const uint32_t mode = UintOr(primitive, "mode", 4u, where);
			if (mode != 4)
			{
				Warn(where, " 의 mode ", mode, " 는 지원하지 않아 건너뜀 (삼각형 목록 4만 지원)");
				return false;
			}
			return true;
		}

		// primitive.material 이 머터리얼 개수 안이면 그 값, 아니면 경고 후 0
		uint32_t MaterialSlotOf(Value& primitive, size_t materialCount, const std::string& where)
		{
			if (!Has(primitive, "material"))
				return 0;
			const uint32_t slot = UintOf(primitive, "material", where);
			if (slot >= materialCount)
			{
				Warn(where, " 의 material ", slot, " 가 머터리얼 개수 ", materialCount, " 를 넘어 0번 슬롯 사용");
				return 0;
			}
			return slot;
		}

		// ===== 경로 / 이미지 헬퍼 =====
		std::filesystem::path ToPath(const std::string& utf8)
		{
			return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
		}

		std::string ToUtf8(const std::u8string& s) { return std::string(s.begin(), s.end()); }

		// glTF 디렉토리 + fileName → "Content/Models/xxx" 형태의 에셋 경로
		std::string MakeAssetPath(const std::string& gltfFile, const std::string& fileName)
		{
			const std::filesystem::path full = (ToPath(gltfFile).parent_path() / ToPath(fileName)).lexically_normal();
			return AssetManager::ToAssetPath(ToUtf8(full.generic_u8string()));
		}

		int HexValue(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}

		std::string PercentDecode(const std::string& text)
		{
			std::string result;
			result.reserve(text.size());
			for (size_t i = 0; i < text.size(); ++i)
			{
				if (text[i] == '%' && i + 2 < text.size() && HexValue(text[i + 1]) >= 0 && HexValue(text[i + 2]) >= 0)
				{
					result += static_cast<char>(HexValue(text[i + 1]) * 16 + HexValue(text[i + 2]));
					i += 2;
				}
				else
				{
					result += text[i];
				}
			}
			return result;
		}

		std::string SafeFileName(const std::string& name)
		{
			std::string result;
			result.reserve(name.size());
			for (const char c : name)
			{
				const bool invalid = static_cast<unsigned char>(c) < 0x20 || std::strchr("<>:\"/\\|?*", c) != nullptr;
				result += invalid ? '_' : c;
			}
			while (!result.empty() && (result.back() == ' ' || result.back() == '.'))
				result.pop_back();
			return result.empty() ? std::string("Unnamed") : result;
		}

		bool DecodeBase64(std::string_view text, std::vector<uint8_t>& bytes)
		{
			bytes.clear();
			uint32_t accumulator = 0;
			int bits = 0;
			for (const char c : text)
			{
				int value = -1;
				if (c >= 'A' && c <= 'Z') value = c - 'A';
				else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
				else if (c >= '0' && c <= '9') value = c - '0' + 52;
				else if (c == '+' || c == '-') value = 62;
				else if (c == '/' || c == '_') value = 63;
				else if (c == '=' || c == '\r' || c == '\n' || c == ' ') continue;
				else return false;
				accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
				bits += 6;
				if (bits >= 8)
				{
					bits -= 8;
					bytes.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xFF));
				}
			}
			return !bytes.empty();
		}

		bool IsLocalRelativeUri(const std::string& decoded)
		{
			if (decoded.empty() || decoded.starts_with("data:"))
				return false;
			const std::filesystem::path path = ToPath(decoded);
			if (path.has_root_name() || path.has_root_directory())
				return false;
			for (const std::filesystem::path& part : path.lexically_normal())
			{
				if (part == "..")
					return false;
			}
			return true;
		}

		std::string ImageExtension(Value& image)
		{
			std::string mime;
			if (Has(image, "mimeType") && image["mimeType"].IsString())
				mime = image["mimeType"].GetString();
			else if (Has(image, "uri") && image["uri"].IsString())
			{
				const std::string uri = image["uri"].GetString();
				if (uri.starts_with("data:"))
					mime = uri.substr(5, uri.find(';') == std::string::npos ? 0 : uri.find(';') - 5);
			}
			if (mime == "image/jpeg") return ".jpg";
			if (mime == "image/webp") return ".webp";
			if (mime == "image/ktx2") return ".ktx2";
			return ".png";
		}

		// imageIndex 범위는 호출하는 쪽에서 검사한다.
		std::string ImageRelativePath(Value& doc, uint32_t imageIndex, const std::string& gltfStem)
		{
			Value& image = doc["images"][imageIndex];
			if (Has(image, "uri") && image["uri"].IsString())
			{
				const std::string decoded = PercentDecode(image["uri"].GetString());
				if (IsLocalRelativeUri(decoded))
					return ToUtf8(ToPath(decoded).lexically_normal().generic_u8string());
				if (!decoded.starts_with("data:"))
					return "textures/" + SafeFileName(ToUtf8(ToPath(decoded).filename().u8string()));
			}

			const std::string extension = ImageExtension(image);
			std::string name = Has(image, "name") && image["name"].IsString() && image["name"].GetStringLength() > 0
				? std::string(image["name"].GetString())
				: gltfStem + "_image" + std::to_string(imageIndex);
			name = SafeFileName(name);
			if (!ToUtf8(ToPath(name).extension().u8string()).empty())
				name = ToUtf8(ToPath(name).stem().u8string());
			return "textures/" + name + extension;
		}

		// 텍스처를 찾을 수 없으면 경고 후 빈 문자열(텍스처 없음)
		std::string TextureAssetPath(CNglTF& cNglTF, Value& textureInfo, const std::string& where)
		{
			Value& doc = cNglTF.m_doc;
			if (!Has(textureInfo, "index") || !textureInfo["index"].IsUint())
			{
				Warn(where, ".index 가 없거나 정수가 아니라 텍스처를 건너뜀");
				return {};
			}
			if (!Has(doc, "textures") || !doc["textures"].IsArray() || !Has(doc, "images") || !doc["images"].IsArray())
			{
				Warn(where, " 가 텍스처를 참조하지만 textures/images 배열이 없음");
				return {};
			}
			const uint32_t textureIndex = textureInfo["index"].GetUint();
			if (textureIndex >= doc["textures"].Size())
			{
				Warn(where, ".index ", textureIndex, " 가 textures 범위를 넘음");
				return {};
			}
			Value& texture = doc["textures"][textureIndex];
			if (!Has(texture, "source") || !texture["source"].IsUint())
			{
				Warn("textures[", textureIndex, "] 에 source가 없음 (확장 전용 텍스처는 지원하지 않음)");
				return {};
			}
			const uint32_t imageIndex = texture["source"].GetUint();
			if (imageIndex >= doc["images"].Size())
			{
				Warn("textures[", textureIndex, "].source ", imageIndex, " 가 images 범위를 넘음");
				return {};
			}
			const std::string stem = ToUtf8(ToPath(cNglTF.m_filePath).stem().u8string());
			return MakeAssetPath(cNglTF.m_filePath, ImageRelativePath(doc, imageIndex, stem));
		}

		bool ReadEmbeddedImage(CNglTF& cNglTF, uint32_t imageIndex, std::vector<uint8_t>& bytes)
		{
			Value& doc = cNglTF.m_doc;
			Value& image = doc["images"][imageIndex];
			if (Has(image, "uri") && image["uri"].IsString())
			{
				const std::string uri = image["uri"].GetString();
				const size_t comma = uri.find(',');
				return uri.starts_with("data:") && comma != std::string::npos
					&& DecodeBase64(std::string_view(uri).substr(comma + 1), bytes);
			}
			if (!Has(image, "bufferView") || !image["bufferView"].IsUint() || !Has(doc, "bufferViews") || !doc["bufferViews"].IsArray())
				return false;
			const uint32_t viewIndex = image["bufferView"].GetUint();
			if (viewIndex >= doc["bufferViews"].Size())
				return false;
			Value& view = doc["bufferViews"][viewIndex];
			if (Has(view, "buffer") && (!view["buffer"].IsUint() || view["buffer"].GetUint() != 0))
				return false;
			if (!Has(view, "byteLength") || !view["byteLength"].IsUint() || (Has(view, "byteOffset") && !view["byteOffset"].IsUint()))
				return false;
			const size_t offset = Has(view, "byteOffset") ? view["byteOffset"].GetUint() : 0u;
			const size_t length = view["byteLength"].GetUint();
			if (offset > cNglTF.m_bin.size() || length > cNglTF.m_bin.size() - offset)
				return false;
			bytes.assign(cNglTF.m_bin.begin() + offset, cNglTF.m_bin.begin() + offset + length);
			return !bytes.empty();
		}

		// 기본 씬 인덱스: scene 항목, 없으면 0 (scenes가 없으면 오류)
		Value& DefaultScene(Value& doc)
		{
			const uint32_t sceneIndex = UintOr(doc, "scene", 0u, "glTF");
			return Element(doc, "scenes", sceneIndex, "기본 scene");
		}

		// ===== [Tangent] 헬퍼 시작 =====
		// UV가 없거나 퇴화해서 탄젠트를 구할 수 없을 때 쓰는 N에 수직인 임의 벡터.
		Vector3 AnyPerpendicular(const Vector3& normal)
		{
			const Vector3 axis = std::abs(normal.Y) < 0.99f ? Vector3::Up : Vector3::Right;
			return Vector3::Cross(axis, normal).Normalized();
		}

		// primitive 하나의 정점 탄젠트를 계산해서 vertices[vertexStart ~ vertexStart + vertexCount)에 저장한다.
		// glTF는 인덱스 버퍼로 삼각형을 구성하고, 같은 정점은 이미 공유되어 있으므로
		// 정점 하나 = 탄젠트 누적 단위다. (FBX처럼 논리 정점 그룹을 따로 만들 필요 없음)
		// indices[indexStart ~ indexStart + indexCount)는 이 primitive 기준 로컬 정점 번호다.
		// 셰이더에서는 B = cross(N, T.xyz) * T.w 를 사용한다.
		template <typename VertexType>
		void ComputeTangents(std::vector<VertexType>& vertices, uint32_t vertexStart, uint32_t vertexCount,
			const std::vector<uint32_t>& indices, uint32_t indexStart, uint32_t indexCount)
		{
			std::vector<Vector3> tangents(vertexCount);   // 로컬 정점 번호별 누적 탄젠트
			std::vector<Vector3> bitangents(vertexCount); // 로컬 정점 번호별 누적 바이탄젠트

			//*** 1. 삼각형마다 탄젠트/바이탄젠트를 구해서 세 정점에 누적
			for (uint32_t i = 0; i + 2 < indexCount; i += 3)
			{
				const uint32_t i0 = indices[indexStart + i];
				const uint32_t i1 = indices[indexStart + i + 1];
				const uint32_t i2 = indices[indexStart + i + 2];
				if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
					continue; // 잘못된 인덱스 방어

				const VertexType& v0 = vertices[vertexStart + i0];
				const VertexType& v1 = vertices[vertexStart + i1];
				const VertexType& v2 = vertices[vertexStart + i2];

				// 위치 차이(e1, e2)와 UV 차이(du, dv)로 UV의 U/V 방향이 3D에서 어디를 향하는지 구한다.
				const Vector3 e1 = v1.Position - v0.Position;
				const Vector3 e2 = v2.Position - v0.Position;
				const float du1 = v1.UV.X - v0.UV.X, dv1 = v1.UV.Y - v0.UV.Y;
				const float du2 = v2.UV.X - v0.UV.X, dv2 = v2.UV.Y - v0.UV.Y;
				const float det = du1 * dv2 - du2 * dv1;
				if (std::abs(det) < 1e-12f)
					continue; // UV가 퇴화한 삼각형은 기여하지 않음

				const float r = 1.0f / det;
				const Vector3 t = (e1 * dv2 - e2 * dv1) * r; // U 방향
				const Vector3 b = (e2 * du1 - e1 * du2) * r; // V 방향
				for (uint32_t local : { i0, i1, i2 })
				{
					tangents[local] += t;
					bitangents[local] += b;
				}
			}

			//*** 2. 정점마다 노말에 수직이 되도록 보정(Gram-Schmidt)하고 방향 부호(w) 저장
			for (uint32_t local = 0; local < vertexCount; ++local)
			{
				VertexType& vertex = vertices[vertexStart + local];
				const Vector3& n = vertex.Normal;
				const Vector3& accumulated = tangents[local];
				Vector3 t = (accumulated - n * Vector3::Dot(n, accumulated)).Normalized();
				if (t.IsNearlyZero())
					t = AnyPerpendicular(n); // 노말이 0이면 탄젠트도 0으로 남음
				// UV가 미러링된 부분은 바이탄젠트 방향이 반대 → w = -1
				const float w = Vector3::Dot(Vector3::Cross(n, t), bitangents[local]) < 0.0f ? -1.0f : 1.0f;
				vertex.Tangent = { t.X, t.Y, t.Z, w };
			}
		}
		// ===== [Tangent] 헬퍼 끝 =====

		// 선택 속성(NORMAL, TEXCOORD_0, TANGENT, JOINTS_0, WEIGHTS_0)은 잘못되었으면 경고만 하고 건너뛴다.
		// read가 GltfError를 던지면 false.
		template <typename Fn>
		bool TryOptional(const std::string& what, Fn&& read)
		{
			try
			{
				read();
				return true;
			}
			catch (const GltfError& error)
			{
				Warn(error.what(), " → ", what, " 속성을 건너뜀");
				return false;
			}
		}

		void WarnShortAttribute(const std::string& what, uint32_t count, uint32_t vertexCount)
		{
			if (count < vertexCount)
				Warn(what, " 개수 ", count, " 가 POSITION 개수 ", vertexCount, " 보다 적음 (나머지는 0)");
		}
	}

	bool CNglTFImporter::ReadglTF(std::string filePath, CNglTF& cNglTF)
	try
	{
		//***** glTF -> Json -> rapidjson::doc 변환
		FILE* file = nullptr;
		if (_wfopen_s(&file, ToPath(filePath).c_str(), L"rb") != 0 || !file)
			Fail("glTF 파일을 열 수 없음");

		char buff[2048] = "";
		FileReadStream frs(file, buff, sizeof(buff));
		cNglTF.m_doc.ParseStream(frs);
		fclose(file);

		if (cNglTF.m_doc.HasParseError())
			Fail("JSON 파싱 오류 (offset ", cNglTF.m_doc.GetErrorOffset(), ", code ", static_cast<int>(cNglTF.m_doc.GetParseError()),
				") .glb 바이너리 glTF는 지원하지 않음");
		if (!cNglTF.m_doc.IsObject())
			Fail("glTF 최상위가 JSON 객체가 아님");
		cNglTF.m_filePath = filePath;

		//***** bin 경로 결정: glTF와 같은 디렉토리 + buffers[0].uri (파일 이름)
		Value& doc = cNglTF.m_doc;
		Value& buffer = At(ArrayMember(doc, "buffers", "glTF"), 0, "buffers");
		const std::string uri = StringOr(buffer, "uri", "", "buffers[0]");
		if (uri.empty())
			Fail("buffers[0].uri 가 없음 (.glb는 지원하지 않음)");
		if (uri.starts_with("data:"))
			Fail("buffers[0] 가 data URI (내장 base64 버퍼는 지원하지 않음)");
		if (ArrayMember(doc, "buffers", "glTF").Size() > 1)
			Warn("buffers 가 ", doc["buffers"].Size(), " 개 (buffers[0]만 읽음, 다른 버퍼를 쓰는 데이터는 오류 처리됨)");

		const std::filesystem::path binPath = ToPath(filePath).parent_path() / ToPath(PercentDecode(uri));
		const std::u8string binPathU8 = binPath.generic_u8string();
		cNglTF.m_binFIlePath = std::string(binPathU8.begin(), binPathU8.end());

		//***** bin -> vector<uint8_t> 변환
		FILE* binFile = nullptr;
		if (_wfopen_s(&binFile, binPath.c_str(), L"rb") != 0 || !binFile)
			Fail("bin 파일을 열 수 없음 : ", cNglTF.m_binFIlePath);

		//*** 파일 전체 크기 확인
		std::fseek(binFile, 0, SEEK_END);
		const long long fileSize = _ftelli64(binFile);
		if (fileSize <= 0) // 파일 사이즈 확인을 실패하거나 파일 사이즈가 0 바이트 이면 실패
		{
			std::fclose(binFile);
			Fail("bin 파일이 비어 있거나 크기를 읽을 수 없음 : ", cNglTF.m_binFIlePath);
		}

		//*** 파일 크기만큼 vector 할당 후 데이터 복사
		cNglTF.m_bin.resize(static_cast<size_t>(fileSize));
		std::fseek(binFile, 0, SEEK_SET);
		const size_t readSize = std::fread(cNglTF.m_bin.data(), 1, cNglTF.m_bin.size(), binFile);
		std::fclose(binFile);
		if (readSize != cNglTF.m_bin.size())
			Fail("bin 파일을 끝까지 읽지 못함 (", readSize, " / ", cNglTF.m_bin.size(), " 바이트)");

		const uint32_t declaredLength = UintOr(buffer, "byteLength", 0u, "buffers[0]");
		if (declaredLength > cNglTF.m_bin.size())
			Warn("buffers[0].byteLength ", declaredLength, " 가 실제 bin 크기 ", cNglTF.m_bin.size(), " 보다 큼");

		//***** 기본 데이터 추출 및 저장 (없는 항목은 0)
		const auto arraySize = [&doc](const char* key) -> uint32_t
		{
			return Has(doc, key) && doc[key].IsArray() ? doc[key].Size() : 0u;
		};
		cNglTF.m_meshCnt = arraySize("meshes");
		cNglTF.m_materialCnt = arraySize("materials");
		cNglTF.m_nodeCnt = arraySize("nodes");
		cNglTF.m_idxVtxCnt = 0;
		cNglTF.m_faceCnt = 0;

		//*** 인덱스, 폴리곤 개수 (통계용이라 잘못된 항목은 경고 없이 건너뜀)
		uint32_t polygonIdxCount = 0;
		if (cNglTF.m_meshCnt > 0)
		{
			for (Value& mesh : doc["meshes"].GetArray())
			{
				if (!Has(mesh, "primitives") || !mesh["primitives"].IsArray())
					continue;
				for (Value& primitive : mesh["primitives"].GetArray())
				{
					uint32_t accessorIndex = 0;
					if (Has(primitive, "indices") && primitive["indices"].IsUint())
						accessorIndex = primitive["indices"].GetUint();
					else if (Has(primitive, "attributes") && Has(primitive["attributes"], "POSITION") && primitive["attributes"]["POSITION"].IsUint())
						accessorIndex = primitive["attributes"]["POSITION"].GetUint();
					else
						continue;
					if (accessorIndex >= arraySize("accessors") || !Has(doc["accessors"][accessorIndex], "count")
						|| !doc["accessors"][accessorIndex]["count"].IsUint())
						continue;
					const uint32_t count = doc["accessors"][accessorIndex]["count"].GetUint();
					cNglTF.m_idxVtxCnt += count;
					if (!Has(primitive, "mode") || (primitive["mode"].IsUint() && primitive["mode"].GetUint() == 4))
						polygonIdxCount += count;
				}
			}
		}

		//*** 폴리곤 개수 저장
		cNglTF.m_faceCnt = polygonIdxCount / 3;
		return true;
	}
	catch (const std::exception& error)
	{
		LogFailure("읽기", filePath, error.what());
		return false;
	}

	std::unique_ptr<MeshAsset> CNglTFImporter::LoadMeshAsset(CNglTF& cNglTF)
	try
	{
		//***** 머터리얼 슬롯 추출
		std::vector<MaterialData> materialSlots;
		LoadMaterials(cNglTF, materialSlots);

		//***** 노드 데이터 추출
		Value& doc = cNglTF.m_doc;
		if (!Has(doc, "scene")) // scene이 없는 glTF는 지원하지 않음
		{
			Warn("scene 항목이 없어 메쉬를 만들지 않음 : ", cNglTF.m_filePath);
			return std::make_unique<MeshAsset>();
		}
		Value& scene = DefaultScene(doc);
		if (!Has(scene, "nodes")) // 노드가 없으면 모델이 로드되지 않음
		{
			Warn("기본 scene 에 nodes 가 없어 메쉬를 만들지 않음 : ", cNglTF.m_filePath);
			return std::make_unique<MeshAsset>();
		}

		//***** 정점 데이터 추출을 위한 데이터
		std::vector<StaticVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<SubMesh> subMeshes;

		Value& rootNodes = ArrayMember(scene, "nodes", "기본 scene");
		for (Value& nodeIndex : rootNodes.GetArray())
		{
			//*** 노드에 메쉬가 있다면 정점 데이터 추출
			Matrix4 TRS = Matrix4::Identity; // 루트 노드의 부모 TRS는 단위행렬
			const uint32_t index = ToUint(nodeIndex, "scene.nodes 원소");
			LoadNodeMesh(cNglTF, Element(doc, "nodes", index, "scene 루트 노드"), TRS, vertices, indices, subMeshes,
				true, true, materialSlots.size(), 0);
		}

		if (vertices.empty() || subMeshes.empty())
			Fail("삼각형 메쉬 데이터가 하나도 없음");

		return std::make_unique<MeshAsset>(std::move(vertices), std::move(indices), std::move(subMeshes), std::move(materialSlots));
	}
	catch (const std::exception& error)
	{
		LogFailure("스태틱 메쉬 로드", cNglTF.m_filePath, error.what());
		return nullptr;
	}

	std::unique_ptr<TextureAsset> CNglTFImporter::LoadTextureAsset(CNglTF& cNglTF)
	{
		(void)cNglTF;
		return std::unique_ptr<TextureAsset>();
	}

	std::vector<std::unique_ptr<AnimationClipAsset>> CNglTFImporter::LoadAnimationClipAsset(CNglTF& cNglTF)
	try
	{
		Value& doc = cNglTF.m_doc;
		if (!Has(doc, "animations"))
		{
			return {};
		}

		//***** 본의 루트 기준 상대 경로를 구하기 위해 전체 노드 경로를 일괄 개산해놓는다.
		Value& nodes = ArrayMember(doc, "nodes", "glTF");
		std::vector<std::string> nodePaths(nodes.Size());
		for (Value& rootNode : ArrayMember(At(ArrayMember(doc, "scenes", "glTF"), 0u, "scenes"), "nodes", "scenes[0]").GetArray())
		{
			BuildPaths(cNglTF, ToUint(rootNode, "scenes[0].nodes 원소"), "", nodePaths, 0);
		}

		Value& animations = ArrayMember(doc, "animations", "glTF");
		std::vector<std::unique_ptr<AnimationClipAsset>> clips;

		for (SizeType animIndex = 0; animIndex < animations.Size(); animIndex++)
		{
			const std::string animWhere = Concat("animations[", animIndex, "]");
			try
			{
				Value& animation = animations[animIndex];
				Value& channels = ArrayMember(animation, "channels", animWhere);
				Value& samplers = ArrayMember(animation, "samplers", animWhere);

				std::vector<AnimationTrack> tracks;
				float duration = 0.0f;

				for (SizeType channelIndex = 0; channelIndex < channels.Size(); channelIndex++)
				{
					//*** 채널 하나가 잘못되었으면 그 트랙만 건너뛴다.
					const std::string where = Concat(animWhere, ".channels[", channelIndex, "]");
					try
					{
						Value& channel = channels[channelIndex];
						AnimationTrack track;

						Value& target = Member(channel, "target", where);
						if (!Has(target, "node"))
						{
							Warn(where, " 에 target.node 가 없어 건너뜀");
							continue;
						}
						const uint32_t samplerIndex = UintOf(channel, "sampler", where);
						Value& sampler = At(samplers, samplerIndex, animWhere + ".samplers");
						const std::string samplerWhere = Concat(animWhere, ".samplers[", samplerIndex, "]");

						//*** 본 노드 경로
						const uint32_t nodeIndex = UintOf(target, "node", where + ".target");
						if (nodeIndex >= nodePaths.size())
							Fail(where, ".target.node ", nodeIndex, " 가 nodes 범위를 넘음");
						track.targetPath = nodePaths[nodeIndex];

						//*** 트랙 프레임 정보 유형
						const std::string property = StringOr(target, "path", "", where + ".target");
						if (property == "translation") track.property = AnimationProperty::Translation;
						else if (property == "rotation") track.property = AnimationProperty::Rotation;
						else if (property == "scale") track.property = AnimationProperty::Scale;
						else
						{
							Warn(where, " 의 target.path '", property, "' 는 지원하지 않아 건너뜀");
							continue;
						}

						//*** 트랙 보간 방법 (생략 시 LINEAR)
						const std::string interpolation = StringOr(sampler, "interpolation", "LINEAR", samplerWhere);
						if (interpolation == "STEP") track.interpolation = AnimationInterpolation::Step;
						else if (interpolation == "LINEAR") track.interpolation = AnimationInterpolation::Linear;
						else
						{
							Warn(samplerWhere, " 의 보간 '", interpolation, "' 는 지원하지 않아 트랙을 건너뜀");
							continue;
						}

						//*** 키값 (시간)
						const AccessorData input = ReadAccessor(cNglTF, UintOf(sampler, "input", samplerWhere),
							samplerWhere + " input", "SCALAR", { kFloat }, false);
						const bool rotation = track.property == AnimationProperty::Rotation;
						const AccessorData output = ReadAccessor(cNglTF, UintOf(sampler, "output", samplerWhere),
							samplerWhere + " output", rotation ? "VEC4" : "VEC3", { kFloat }, false);
						if (output.count < input.count)
							Fail(samplerWhere, " output 개수 ", output.count, " 가 input 개수 ", input.count, " 보다 적음");

						//*** 키 시간 읽기
						track.times.resize(input.count);
						for (uint32_t i = 0; i < input.count; i++)
						{
							std::memcpy(&track.times[i], input.Element(i), sizeof(float));
						}

						//*** 키 값 읽기 (메쉬와 같은 엔진 좌표계로 X축 반전)
						track.values.resize(input.count);
						for (uint32_t i = 0; i < input.count; i++)
						{
							if (rotation)
							{
								//* 회전 (x, y, z, w) → (x, -y, -z, w)
								Vector4 value;
								std::memcpy(&value, output.Element(i), sizeof(Vector4));
								track.values[i] = { value.X, -value.Y, -value.Z, value.W };
							}
							else
							{
								//* 위치 (x, y, z) → (-x, y, z), 스케일은 그대로
								Vector3 value;
								std::memcpy(&value, output.Element(i), sizeof(Vector3));
								if (track.property == AnimationProperty::Translation)
								{
									value.X = -value.X;
								}
								track.values[i] = { value.X, value.Y, value.Z, 0.0f };
							}
						}

						//*** 클립 길이 = 모든 트랙의 마지막 키 시간 중 최댓값 (시간은 오름차순)
						if (!track.times.empty() && track.times.back() > duration)
						{
							duration = track.times.back();
						}

						tracks.push_back(std::move(track));
					}
					catch (const GltfError& error)
					{
						Warn(error.what(), " → 트랙을 건너뜀");
					}
				}

				std::string fileStem = ToUtf8(ToPath(cNglTF.m_filePath).stem().u8string());
				std::string name = StringOr(animation, "name", fileStem + "_Anim_" + std::to_string(animIndex), animWhere);
				clips.push_back(std::make_unique<AnimationClipAsset>(name, duration, std::move(tracks)));
			}
			catch (const GltfError& error)
			{
				Warn(error.what(), " → ", animWhere, " 클립을 건너뜀");
			}
		}

		return clips;
	}
	catch (const std::exception& error)
	{
		LogFailure("애니메이션 로드", cNglTF.m_filePath, error.what());
		return {};
	}

	void BuildPaths(CNglTF& cNglTF, uint32_t currentNodeIndex, const std::string& parentPath, std::vector<std::string>& out, uint32_t depth)
	{
		Value& nodes = ArrayMember(cNglTF.m_doc, "nodes", "glTF");
		if (depth > nodes.Size())
			Fail("노드 계층에 순환이 있음 (nodes[", currentNodeIndex, "])");
		Value& node = At(nodes, currentNodeIndex, "nodes");
		if (currentNodeIndex >= out.size())
			Fail("노드 경로 버퍼 크기 부족 (nodes[", currentNodeIndex, "])");

		const std::string where = Concat("nodes[", currentNodeIndex, "]");
		const std::string name = StringOr(node, "name", "Node_" + std::to_string(currentNodeIndex), where);
		out[currentNodeIndex] = parentPath.empty() ? name : parentPath + "/" + name;

		if (!Has(node, "children"))
			return;

		for (Value& child : ArrayMember(node, "children", where).GetArray())
		{
			BuildPaths(cNglTF, ToUint(child, where + ".children 원소"), out[currentNodeIndex], out, depth + 1);
		}
	}

	void CNglTFImporter::LoadMaterials(CNglTF& cNglTF, std::vector<MaterialData>& materialSlots)
	{
		Value& doc = cNglTF.m_doc;

		//**** 머터리얼 추출
		if (!Has(doc, "materials"))
			return;

		Value& materials = ArrayMember(doc, "materials", "glTF");
		materialSlots.resize(materials.Size()); // 매터리얼 개수만큼 메쉬의 머터리얼 슬롯 공간 확보

		//*** 이 모델에서 사용하는 모든 머터리얼 추출 (잘못된 값은 경고 후 기본값)
		for (uint32_t i = 0; i < materials.Size(); i++)
		{
			Value& material = materials[i];
			const std::string where = Concat("materials[", i, "]");
			if (!material.IsObject())
			{
				Warn(where, " 가 JSON 객체가 아니라 기본 머터리얼 사용");
				continue;
			}

			//*** 머터리얼 이름으로 머터리얼 에셋 경로 저장
			//* 머터리얼 이름이 있다면 사용, 없다면 glTF 파일 이름 + Material_i 사용
			const std::string name = StringOr(material, "name", "", where);
			if (!name.empty())
			{
				materialSlots[i].name = MakeAssetPath(cNglTF.m_filePath, SafeFileName(name) + ".mat");
			}
			else
			{
				std::string fileStem = ToUtf8(ToPath(cNglTF.m_filePath).stem().u8string());
				materialSlots[i].name = MakeAssetPath(cNglTF.m_filePath, fileStem + "_Material_" + std::to_string(i) + ".mat");
			}

			//*** doubleSided 양면 렌더
			materialSlots[i].doubleSided = BoolOr(material, "doubleSided", materialSlots[i].doubleSided, where);

			//*** pprMetallicRoughness 옵션
			if (Has(material, "pbrMetallicRoughness"))
			{
				Value& pbr = material["pbrMetallicRoughness"];
				const std::string pbrWhere = where + ".pbrMetallicRoughness";

				materialSlots[i].desc.metallic = FloatOr(pbr, "metallicFactor", 1.0f, pbrWhere);
				materialSlots[i].desc.roughness = FloatOr(pbr, "roughnessFactor", 1.0f, pbrWhere);

				//* baseColorFactor
				if (Has(pbr, "baseColorFactor"))
				{
					float color[4];
					try
					{
						ReadFloats(pbr, "baseColorFactor", 4, color, pbrWhere);
						materialSlots[i].desc.baseColor = { color[0], color[1], color[2], color[3] };
					}
					catch (const GltfError& error)
					{
						Warn(error.what(), " → 기본 색 사용");
					}
				}

				if (Has(pbr, "baseColorTexture"))
					materialSlots[i].baseColorTexture = TextureAssetPath(cNglTF, pbr["baseColorTexture"], pbrWhere + ".baseColorTexture");
				if (Has(pbr, "metallicRoughnessTexture"))
					materialSlots[i].metallicRoughnessTexture = TextureAssetPath(cNglTF, pbr["metallicRoughnessTexture"], pbrWhere + ".metallicRoughnessTexture");
			}

			if (Has(material, "normalTexture"))
				materialSlots[i].normalTexture = TextureAssetPath(cNglTF, material["normalTexture"], where + ".normalTexture");
			if (Has(material, "occlusionTexture"))
				materialSlots[i].occlusionTexture = TextureAssetPath(cNglTF, material["occlusionTexture"], where + ".occlusionTexture");
			if (Has(material, "emissiveTexture"))
				materialSlots[i].emissiveTexture = TextureAssetPath(cNglTF, material["emissiveTexture"], where + ".emissiveTexture");
		}
	}

	Matrix4 CNglTFImporter::GetNodeTRS(Value& node)
	{
		//*** matrix가 있으면 TRS 대신 사용
		// glTF는 열벡터 + column-major 배열, 엔진은 행벡터 + row-major → 배열 순서 그대로 복사하면 된다.
		if (Has(node, "matrix"))
		{
			float values[16];
			ReadFloats(node, "matrix", 16, values, "node");
			Matrix4 m;
			for (uint32_t i = 0; i < 16; i++)
			{
				m.M[i / 4][i % 4] = values[i];
			}
			return m;
		}

		Matrix4 s;
		Matrix4 r;
		Matrix4 t;
		if (Has(node, "rotation"))
		{
			float rotation[4];
			ReadFloats(node, "rotation", 4, rotation, "node");
			r = Matrix4::Rotation({ rotation[0], rotation[1], rotation[2], rotation[3] });
		}

		if (Has(node, "scale"))
		{
			float scale[3];
			ReadFloats(node, "scale", 3, scale, "node");
			s = Matrix4::Scale({ scale[0], scale[1], scale[2] });
		}

		if (Has(node, "translation"))
		{
			float translation[3];
			ReadFloats(node, "translation", 3, translation, "node");
			t = Matrix4::Translation({ translation[0], translation[1], translation[2] });
		}

		return s * r * t;
	}

	void CNglTFImporter::LoadNodeMesh(
		CNglTF& cNglTF,
		Value& node,
		Matrix4& parentTM,
		std::vector<StaticVertex>& vertices,
		std::vector<uint32_t>& indices,
		std::vector<SubMesh>& subMeshes,
		bool includeNodeTransform,
		bool recurse,
		size_t materialCount,
		uint32_t depth
	)
	{
		Value& doc = cNglTF.m_doc;
		if (depth > cNglTF.m_nodeCnt)
			Fail("노드 계층에 순환이 있음");

		//*** 노드 로컬 tm 행렬
		Matrix4 TRS = includeNodeTransform ? GetNodeTRS(node) * parentTM : parentTM; // 우리 엔진은 행벡터 규약을 사용

		//*** 행렬식: 음수면 노드에 미러링(음수 스케일)이 있음 → 와인딩 판단용
		float det = DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(TRS.ToXM()));

		//*** 노드에 메쉬가 있다면 메쉬 정보 추출
		if (Has(node, "mesh"))
		{
			const uint32_t meshIndex = UintOf(node, "mesh", "node");
			Value& mesh = Element(doc, "meshes", meshIndex, "node.mesh");
			Value& primitives = ArrayMember(mesh, "primitives", Concat("meshes[", meshIndex, "]"));

			for (SizeType primitiveIndex = 0; primitiveIndex < primitives.Size(); primitiveIndex++)
			{
				Value& primitive = primitives[primitiveIndex];
				const std::string where = Concat("meshes[", meshIndex, "].primitives[", primitiveIndex, "]");
				if (!IsTrianglePrimitive(primitive, where))
					continue;

				//***** 서브메쉬 정보 넣기 - 1
				SubMesh sm;
				sm.startIndex = static_cast<uint32_t>(indices.size()); // 이 primitve의 인덱스 넣기전 시작점
				sm.baseVertex = static_cast<int32_t>(vertices.size()); //     ``        정점 넣기전 시작점
				sm.materialSlot = MaterialSlotOf(primitive, materialCount, where);

				//***** Vertex 정보 추출 : position, normal, uv, tangent
				Value& attributes = Member(primitive, "attributes", where);

				//*** Position (필수)
				const AccessorData positions = ReadAccessor(cNglTF, UintOf(attributes, "POSITION", where + ".attributes"),
					where + " POSITION", "VEC3", { kFloat }, true);
				const uint32_t posCnt = positions.count;

				//***** 바이너리에서 정점 정보 복사
				//*** 이 primitive를 위한 정점 데이터 메모리 할당
				const uint32_t svStart = static_cast<uint32_t>(vertices.size());
				const uint32_t iStart = static_cast<uint32_t>(indices.size());

				//*** 인덱스를 먼저 검사한다(잘못되면 이 노드 이하 전체가 실패하므로 정점을 넣기 전에).
				std::vector<uint32_t> primitiveIndices;
				ReadIndices(cNglTF, primitive, posCnt, primitiveIndices, where);
				if (primitiveIndices.empty())
				{
					Warn(where, " 에 삼각형이 없어 건너뜀");
					continue;
				}

				vertices.resize(svStart + posCnt);
				for (uint32_t i = 0; i < posCnt; i++)
				{
					Vector3 pos;
					std::memcpy(&pos, positions.Element(i), sizeof(Vector3));
					pos = TRS.TransformPoint(pos);
					// 좌표계 변환
					pos.X = -pos.X;
					vertices[svStart + i].Position = pos;
				}

				//*** Normal
				if (Has(attributes, "NORMAL"))
				{
					TryOptional(where + " NORMAL", [&]
					{
						const AccessorData normals = ReadAccessor(cNglTF, UintOf(attributes, "NORMAL", where + ".attributes"),
							where + " NORMAL", "VEC3", { kFloat }, true);
						const uint32_t norCnt = (std::min)(normals.count, posCnt);
						WarnShortAttribute(where + " NORMAL", normals.count, posCnt);

						//* Normal에는 스케일이 포함되면 방향이 바뀌기 때문에 역전체 행렬을 사용해야함
						DirectX::XMMATRIX normalMat = DirectX::XMMatrixTranspose(TRS.Inverse().ToXM());
						for (uint32_t i = 0; i < norCnt; i++)
						{
							Vector3 normal;
							std::memcpy(&normal, normals.Element(i), sizeof(Vector3));
							DirectX::XMVECTOR n = DirectX::XMVector3TransformNormal(normal.ToXM(), normalMat);
							Vector3 nrm = Vector3::FromXM(DirectX::XMVector3Normalize(n));
							nrm.X = -nrm.X; // 좌표계 변환
							vertices[svStart + i].Normal = nrm;
						}
					});
				}

				//*** uv
				if (Has(attributes, "TEXCOORD_0"))
				{
					TryOptional(where + " TEXCOORD_0", [&]
					{
						const AccessorData uvs = ReadAccessor(cNglTF, UintOf(attributes, "TEXCOORD_0", where + ".attributes"),
							where + " TEXCOORD_0", "VEC2", { kFloat }, true);
						const uint32_t uvCnt = (std::min)(uvs.count, posCnt);
						WarnShortAttribute(where + " TEXCOORD_0", uvs.count, posCnt);
						for (uint32_t i = 0; i < uvCnt; i++)
						{
							Vector2 uv;
							std::memcpy(&uv, uvs.Element(i), sizeof(Vector2));
							vertices[svStart + i].UV = uv;
						}
					});
				}

				//*** 탄젠트
				bool hasTangent = false;
				if (Has(attributes, "TANGENT"))
				{
					hasTangent = TryOptional(where + " TANGENT", [&]
					{
						const AccessorData tangents = ReadAccessor(cNglTF, UintOf(attributes, "TANGENT", where + ".attributes"),
							where + " TANGENT", "VEC4", { kFloat }, true);
						const uint32_t tanCnt = (std::min)(tangents.count, posCnt);
						WarnShortAttribute(where + " TANGENT", tangents.count, posCnt);
						const DirectX::XMMATRIX M = TRS.ToXM();

						for (uint32_t i = 0; i < tanCnt; i++)
						{
							Vector4 t; // VEC4 float
							std::memcpy(&t, tangents.Element(i), sizeof(Vector4));

							// 탄젠트는 표면 방향 → 역전치 X, TRS 그대로 w=0
							Vector3 tv = Vector3::FromXM(DirectX::XMVector3Normalize(
								DirectX::XMVector3TransformNormal(DirectX::XMVectorSet(t.X, t.Y, t.Z, 0.0f), M)));
							tv.X = -tv.X;                              // 좌표계 미러링
							const float w = (det > 0.0f) ? -t.W : t.W; // 손 방향이 뒤집히면 부호 반전
							vertices[svStart + i].Tangent = { tv.X, tv.Y, tv.Z, w };
						}
					});
				}

				//***** 인덱스 복사
				const uint32_t indexCnt = static_cast<uint32_t>(primitiveIndices.size());
				indices.insert(indices.end(), primitiveIndices.begin(), primitiveIndices.end());

				//***** 와인딩 반전: X 미러링으로 앞뒷면이 바뀜 (노드에도 미러링이 있으면 원래대로)
				if (det > 0.0f)
				{
					for (uint32_t i = 0; i + 2 < indexCnt; i += 3)
						std::swap(indices[iStart + i + 1], indices[iStart + i + 2]);
				}

				//***** 탄젠트 계산 (정점, 인덱스가 모두 채워진 뒤에 해야 함)
				if (!hasTangent)
				{
					ComputeTangents(vertices, svStart, posCnt, indices, iStart, indexCnt);
				}

				//***** 서브메쉬 정보 넣기 - 2
				sm.indexCount = indexCnt; // 인덱스 값 몇개 넣었는지 저장
				subMeshes.push_back(sm);
			}
		}

		//***** 자식노드로 재귀
		if (recurse && Has(node, "children"))
		{
			for (Value& child : ArrayMember(node, "children", "node").GetArray())
			{
				const uint32_t childNodeIndex = ToUint(child, "node.children 원소");
				Value& childNode = Element(doc, "nodes", childNodeIndex, "node.children");

				LoadNodeMesh(cNglTF, childNode, TRS, vertices, indices, subMeshes, true, true, materialCount, depth + 1);
			}
		}
	}

	std::unique_ptr<MeshAsset> CNglTFImporter::LoadNodeMeshAsset(CNglTF& cNglTF, uint32_t nodeIndex)
	try
	{
		Value& doc = cNglTF.m_doc;
		if (!Has(doc, "nodes") || !doc["nodes"].IsArray() || nodeIndex >= doc["nodes"].Size() || !Has(doc["nodes"][nodeIndex], "mesh"))
		{
			Warn("nodes[", nodeIndex, "] 가 없거나 메쉬가 없음 : ", cNglTF.m_filePath);
			return nullptr;
		}

		std::vector<MaterialData> materialSlots;
		LoadMaterials(cNglTF, materialSlots);

		std::vector<StaticVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<SubMesh> subMeshes;
		Matrix4 identity = Matrix4::Identity; // 노드 오브젝트의 Transform이 노드 TRS를 맡는다
		LoadNodeMesh(cNglTF, doc["nodes"][nodeIndex], identity, vertices, indices, subMeshes, false, false, materialSlots.size(), 0);
		if (vertices.empty() || subMeshes.empty())
			return nullptr;

		return std::make_unique<MeshAsset>(std::move(vertices), std::move(indices), std::move(subMeshes), std::move(materialSlots));
	}
	catch (const std::exception& error)
	{
		LogFailure(Concat("노드 메쉬 로드 (nodes[", nodeIndex, "])").c_str(), cNglTF.m_filePath, error.what());
		return nullptr;
	}

	std::unique_ptr<MeshAsset> CNglTFImporter::LoadSkinnedMeshAsset(CNglTF& cNglTF, uint32_t skinIndex)
	try
	{
		Value& doc = cNglTF.m_doc;
		if (!Has(doc, "skins") || !doc["skins"].IsArray() || skinIndex >= doc["skins"].Size())
		{
			Warn("skins[", skinIndex, "] 가 없음 : ", cNglTF.m_filePath);
			return nullptr;
		}

		Value& nodes = ArrayMember(doc, "nodes", "glTF");

		std::vector<MaterialData> materialSlots;
		LoadMaterials(cNglTF, materialSlots);

		std::vector<SkinnedVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<SubMesh> subMeshes;

		for (SizeType nodeIndex = 0; nodeIndex < nodes.Size(); nodeIndex++)
		{
			Value& meshNode = nodes[nodeIndex];
			const std::string nodeWhere = Concat("nodes[", nodeIndex, "]");
			if (!Has(meshNode, "skin") || !Has(meshNode, "mesh") || UintOf(meshNode, "skin", nodeWhere) != skinIndex)
				continue;

			const uint32_t meshIndex = UintOf(meshNode, "mesh", nodeWhere);
			Value& mesh = Element(doc, "meshes", meshIndex, nodeWhere + ".mesh");
			Value& primitives = ArrayMember(mesh, "primitives", Concat("meshes[", meshIndex, "]"));
			for (SizeType primitiveIndex = 0; primitiveIndex < primitives.Size(); primitiveIndex++)
			{
				Value& primitive = primitives[primitiveIndex];
				const std::string where = Concat("meshes[", meshIndex, "].primitives[", primitiveIndex, "]");
				if (!IsTrianglePrimitive(primitive, where))
					continue;
				Value& attributes = Member(primitive, "attributes", where);
				const std::string attrWhere = where + ".attributes";

				SubMesh sm;
				sm.startIndex = static_cast<uint32_t>(indices.size());
				sm.baseVertex = static_cast<int32_t>(vertices.size());
				sm.materialSlot = MaterialSlotOf(primitive, materialSlots.size(), where);

				const AccessorData positions = ReadAccessor(cNglTF, UintOf(attributes, "POSITION", attrWhere),
					where + " POSITION", "VEC3", { kFloat }, true);
				const uint32_t vertexCount = positions.count;

				std::vector<uint32_t> primitiveIndices;
				ReadIndices(cNglTF, primitive, vertexCount, primitiveIndices, where);
				if (primitiveIndices.empty())
				{
					Warn(where, " 에 삼각형이 없어 건너뜀");
					continue;
				}

				const uint32_t vStart = static_cast<uint32_t>(vertices.size());
				vertices.resize(vStart + vertexCount);
				for (uint32_t i = 0; i < vertexCount; i++)
				{
					Vector3 pos;
					std::memcpy(&pos, positions.Element(i), sizeof(Vector3));
					pos.X = -pos.X;
					vertices[vStart + i].Position = pos;
				}

				if (Has(attributes, "NORMAL"))
				{
					TryOptional(where + " NORMAL", [&]
					{
						const AccessorData normals = ReadAccessor(cNglTF, UintOf(attributes, "NORMAL", attrWhere),
							where + " NORMAL", "VEC3", { kFloat }, true);
						WarnShortAttribute(where + " NORMAL", normals.count, vertexCount);
						const uint32_t count = (std::min)(normals.count, vertexCount);
						for (uint32_t i = 0; i < count; i++)
						{
							Vector3 normal;
							std::memcpy(&normal, normals.Element(i), sizeof(Vector3));
							normal.X = -normal.X;
							vertices[vStart + i].Normal = normal;
						}
					});
				}

				if (Has(attributes, "TEXCOORD_0"))
				{
					TryOptional(where + " TEXCOORD_0", [&]
					{
						const AccessorData uvs = ReadAccessor(cNglTF, UintOf(attributes, "TEXCOORD_0", attrWhere),
							where + " TEXCOORD_0", "VEC2", { kFloat }, true);
						WarnShortAttribute(where + " TEXCOORD_0", uvs.count, vertexCount);
						const uint32_t count = (std::min)(uvs.count, vertexCount);
						for (uint32_t i = 0; i < count; i++)
						{
							std::memcpy(&vertices[vStart + i].UV, uvs.Element(i), sizeof(Vector2));
						}
					});
				}

				if (Has(attributes, "JOINTS_0"))
				{
					TryOptional(where + " JOINTS_0", [&]
					{
						const AccessorData joints = ReadAccessor(cNglTF, UintOf(attributes, "JOINTS_0", attrWhere),
							where + " JOINTS_0", "VEC4", { kUnsignedByte, kUnsignedShort }, true);
						WarnShortAttribute(where + " JOINTS_0", joints.count, vertexCount);
						const uint32_t count = (std::min)(joints.count, vertexCount);
						const bool isByte = joints.componentType == kUnsignedByte;
						for (uint32_t i = 0; i < count; i++)
						{
							const uint8_t* element = joints.Element(i);
							for (uint32_t k = 0; k < 4; k++)
							{
								if (isByte)
								{
									vertices[vStart + i].Joints[k] = element[k];
								}
								else
								{
									uint16_t joint;
									std::memcpy(&joint, element + k * sizeof(uint16_t), sizeof(uint16_t));
									vertices[vStart + i].Joints[k] = joint;
								}
							}
						}
					});
				}

				if (Has(attributes, "WEIGHTS_0"))
				{
					TryOptional(where + " WEIGHTS_0", [&]
					{
						const AccessorData weights = ReadAccessor(cNglTF, UintOf(attributes, "WEIGHTS_0", attrWhere),
							where + " WEIGHTS_0", "VEC4", { kFloat }, true);
						WarnShortAttribute(where + " WEIGHTS_0", weights.count, vertexCount);
						const uint32_t count = (std::min)(weights.count, vertexCount);
						for (uint32_t i = 0; i < count; i++)
						{
							std::memcpy(vertices[vStart + i].Weights, weights.Element(i), sizeof(float) * 4);
						}
					});
				}

				bool hasTangent = false;
				if (Has(attributes, "TANGENT"))
				{
					hasTangent = TryOptional(where + " TANGENT", [&]
					{
						const AccessorData tangents = ReadAccessor(cNglTF, UintOf(attributes, "TANGENT", attrWhere),
							where + " TANGENT", "VEC4", { kFloat }, true);
						WarnShortAttribute(where + " TANGENT", tangents.count, vertexCount);
						const uint32_t count = (std::min)(tangents.count, vertexCount);
						for (uint32_t i = 0; i < count; i++)
						{
							Vector4 t;
							std::memcpy(&t, tangents.Element(i), sizeof(Vector4));
							const Vector3 tv = Vector3(-t.X, t.Y, t.Z).Normalized();
							vertices[vStart + i].Tangent = { tv.X, tv.Y, tv.Z, -t.W };
						}
					});
				}

				const uint32_t iStart = static_cast<uint32_t>(indices.size());
				const uint32_t indexCount = static_cast<uint32_t>(primitiveIndices.size());
				indices.insert(indices.end(), primitiveIndices.begin(), primitiveIndices.end());
				for (uint32_t i = 0; i + 2 < indexCount; i += 3)
				{
					std::swap(indices[iStart + i + 1], indices[iStart + i + 2]);
				}

				if (!hasTangent)
				{
					ComputeTangents(vertices, vStart, vertexCount, indices, iStart, indexCount);
				}

				sm.indexCount = indexCount;
				subMeshes.push_back(sm);
			}
		}
		if (subMeshes.empty())
		{
			Warn("skins[", skinIndex, "] 를 쓰는 삼각형 메쉬가 없음 : ", cNglTF.m_filePath);
			return nullptr;
		}

		const std::string skinWhere = Concat("skins[", skinIndex, "]");
		Value& skin = doc["skins"][skinIndex];
		Value& joints = ArrayMember(skin, "joints", skinWhere);

		std::vector<std::string> nodePaths(nodes.Size());
		for (Value& rootNode : ArrayMember(DefaultScene(doc), "nodes", "기본 scene").GetArray())
		{
			BuildPaths(cNglTF, ToUint(rootNode, "scene.nodes 원소"), "", nodePaths, 0);
		}

		std::vector<int32_t> parents(nodes.Size(), -1);
		for (uint32_t i = 0; i < nodes.Size(); i++)
		{
			if (!Has(nodes[i], "children"))
				continue;
			const std::string where = Concat("nodes[", i, "]");
			for (Value& child : ArrayMember(nodes[i], "children", where).GetArray())
			{
				const uint32_t childIndex = ToUint(child, where + ".children 원소");
				if (childIndex >= nodes.Size())
					Fail(where, ".children 의 ", childIndex, " 가 nodes 범위를 넘음");
				parents[childIndex] = static_cast<int32_t>(i);
			}
		}

		std::vector<uint32_t> jointNodes;
		std::vector<bool> isJoint(nodes.Size(), false);
		for (Value& joint : joints.GetArray())
		{
			const uint32_t jointNode = ToUint(joint, skinWhere + ".joints 원소");
			if (jointNode >= nodes.Size())
				Fail(skinWhere, ".joints 의 ", jointNode, " 가 nodes 범위를 넘음");
			isJoint[jointNode] = true;
			jointNodes.push_back(jointNode);
		}

		int32_t rootJoint = jointNodes.empty() ? -1 : static_cast<int32_t>(jointNodes[0]);
		for (size_t guard = 0; rootJoint >= 0 && parents[rootJoint] >= 0 && isJoint[parents[rootJoint]]; ++guard)
		{
			if (guard > nodes.Size())
				Fail("노드 계층에 순환이 있음 (", skinWhere, " 루트 조인트 탐색)");
			rootJoint = parents[rootJoint];
		}
		const int32_t skeletonNode = rootJoint >= 0 ? parents[rootJoint] : -1;
		const std::string skeletonPrefix = skeletonNode >= 0 ? nodePaths[skeletonNode] + "/" : "";

		std::vector<std::string> bonePaths;
		for (const uint32_t jointNode : jointNodes)
		{
			const std::string& path = nodePaths[jointNode];
			if (path.empty())
				Warn(skinWhere, " 의 조인트 nodes[", jointNode, "] 가 기본 scene 에 없음 (본 경로 비어 있음)");
			bonePaths.push_back(path.starts_with(skeletonPrefix) ? path.substr(skeletonPrefix.size()) : path);
		}

		std::vector<Matrix4> inverseBindMatrices(jointNodes.size(), Matrix4::Identity);
		if (Has(skin, "inverseBindMatrices"))
		{
			const AccessorData ibms = ReadAccessor(cNglTF, UintOf(skin, "inverseBindMatrices", skinWhere),
				skinWhere + " inverseBindMatrices", "MAT4", { kFloat }, false);
			if (ibms.count < jointNodes.size())
				Warn(skinWhere, " inverseBindMatrices 개수 ", ibms.count, " 가 조인트 수 ", jointNodes.size(), " 보다 적음 (나머지는 단위행렬)");
			const uint32_t count = (std::min)(ibms.count, static_cast<uint32_t>(jointNodes.size()));
			for (uint32_t j = 0; j < count; j++)
			{
				Matrix4& ibm = inverseBindMatrices[j];
				std::memcpy(ibm.M, ibms.Element(j), sizeof(float) * 16);
				for (uint32_t row = 0; row < 4; row++)
				{
					for (uint32_t col = 0; col < 4; col++)
					{
						if ((row == 0) != (col == 0))
						{
							ibm.M[row][col] = -ibm.M[row][col];
						}
					}
				}
			}
		}

		return std::make_unique<MeshAsset>(std::move(vertices), std::move(indices), std::move(subMeshes),
			std::move(inverseBindMatrices), std::move(bonePaths), std::move(materialSlots));
	}
	catch (const std::exception& error)
	{
		LogFailure(Concat("스킨 메쉬 로드 (skins[", skinIndex, "])").c_str(), cNglTF.m_filePath, error.what());
		return nullptr;
	}

	GameObject* CNglTFImporter::LoadSkeleton(CNglTF& cNglTF, Scene& scene, const std::string& name, std::vector<GameObject*>& nodeObjects)
	try
	{
		Value& doc = cNglTF.m_doc;
		Value& nodes = ArrayMember(doc, "nodes", "glTF");
		Value& rootNodes = ArrayMember(DefaultScene(doc), "nodes", "기본 scene");
		GameObject* top = scene.GetGameObject(scene.AddGameObject(name));

		nodeObjects.assign(nodes.Size(), nullptr);
		for (uint32_t i = 0; i < nodes.Size(); i++)
		{
			Value& node = nodes[i];
			const std::string where = Concat("nodes[", i, "]");
			if (Has(node, "skin") && Has(node, "mesh"))
				continue;

			const std::string nodeName = StringOr(node, "name", "Node_" + std::to_string(i), where);
			nodeObjects[i] = scene.GetGameObject(scene.AddGameObject(nodeName));

			//*** 트랜스폼이 잘못된 노드는 경고 후 기본 트랜스폼으로 둔다.
			try
			{
				TransformComponent* transform = nodeObjects[i]->GetTransform();
				if (Has(node, "matrix"))
				{
					// matrix 노드(Sketchfab 루트의 축 변환·스케일 등): TRS로 분해해서 같은 좌표 변환(X 반전)을 적용한다.
					DirectX::XMVECTOR scale, rotation, translation;
					if (DirectX::XMMatrixDecompose(&scale, &rotation, &translation, GetNodeTRS(node).ToXM()))
					{
						DirectX::XMFLOAT3 t, s;
						DirectX::XMFLOAT4 r;
						DirectX::XMStoreFloat3(&t, translation);
						DirectX::XMStoreFloat3(&s, scale);
						DirectX::XMStoreFloat4(&r, rotation);
						transform->SetPosition({ -t.x, t.y, t.z });
						transform->SetRotation({ r.x, -r.y, -r.z, r.w });
						transform->SetScale({ s.x, s.y, s.z });
					}
					else
					{
						Warn(where, ".matrix 를 TRS로 분해할 수 없어 기본 트랜스폼 사용");
					}
				}
				if (Has(node, "translation"))
				{
					float t[3];
					ReadFloats(node, "translation", 3, t, where);
					transform->SetPosition({ -t[0], t[1], t[2] });
				}
				if (Has(node, "rotation"))
				{
					float r[4];
					ReadFloats(node, "rotation", 4, r, where);
					transform->SetRotation({ r[0], -r[1], -r[2], r[3] });
				}
				if (Has(node, "scale"))
				{
					float s[3];
					ReadFloats(node, "scale", 3, s, where);
					transform->SetScale({ s[0], s[1], s[2] });
				}
			}
			catch (const GltfError& error)
			{
				Warn(error.what(), " → ", where, " 트랜스폼 일부를 기본값으로 둠");
			}
		}

		for (Value& rootNode : rootNodes.GetArray())
		{
			const uint32_t index = ToUint(rootNode, "scene.nodes 원소");
			if (index >= nodeObjects.size())
				Fail("scene.nodes 의 ", index, " 가 nodes 범위를 넘음");
			if (nodeObjects[index])
				nodeObjects[index]->GetTransform()->SetParent(top->GetTransform());
		}
		for (uint32_t i = 0; i < nodes.Size(); i++)
		{
			if (!nodeObjects[i] || !Has(nodes[i], "children"))
				continue;
			const std::string where = Concat("nodes[", i, "]");
			for (Value& child : ArrayMember(nodes[i], "children", where).GetArray())
			{
				const uint32_t childIndex = ToUint(child, where + ".children 원소");
				if (childIndex >= nodeObjects.size())
					Fail(where, ".children 의 ", childIndex, " 가 nodes 범위를 넘음");
				if (nodeObjects[childIndex])
					nodeObjects[childIndex]->GetTransform()->SetParent(nodeObjects[i]->GetTransform());
			}
		}

		top->AddComponent<AnimatorComponent>();
		return top;
	}
	catch (const std::exception& error)
	{
		LogFailure("스켈레톤 로드", cNglTF.m_filePath, error.what());
		return nullptr;
	}

	bool CNglTFImporter::ImportSkeleton(const std::string& gltfPath, const std::string& targetDir, const std::string& clipPath)
	try
	{
		CNglTF cNglTF;
		if (!ReadglTF(gltfPath, cNglTF))
			return false;

		const std::string modelName = ToUtf8(ToPath(gltfPath).stem().u8string());
		Scene scene(modelName);
		std::vector<GameObject*> nodeObjects;
		GameObject* top = LoadSkeleton(cNglTF, scene, modelName, nodeObjects);
		if (!top)
			return false;
		if (!clipPath.empty())
		{
			top->GetComponent<AnimatorComponent>()->SetClip(clipPath);
		}

		const std::string prefabPath = ToUtf8((ToPath(targetDir) / ToPath(modelName + "_Skeleton.prefab")).generic_u8string());
		if (!SceneSerializer::SavePrefab(*top, prefabPath))
			Fail("프리팹 저장 실패 : ", prefabPath);
		return true;
	}
	catch (const std::exception& error)
	{
		LogFailure("스켈레톤 임포트", gltfPath, error.what());
		return false;
	}

	namespace
	{
		// Sketchfab 모델은 파일 이름이 모두 scene.gltf라서, 그럴 때는 폴더 이름을 모델 이름으로 쓴다.
		std::string ModelNameOf(const std::filesystem::path& gltf)
		{
			const std::string stem = ToUtf8(gltf.stem().u8string());
			if (stem == "scene" && gltf.has_parent_path() && !gltf.parent_path().filename().empty())
				return ToUtf8(gltf.parent_path().filename().u8string());
			return stem;
		}

		std::string SanitizeFileName(const std::string& name)
		{
			std::string result;
			result.reserve(name.size());
			for (const char c : name)
			{
				const bool invalid = static_cast<unsigned char>(c) < 0x20 || std::strchr("<>:\"/\\|?* ", c) != nullptr;
				result += invalid ? '_' : c;
			}
			return result.empty() ? std::string("Mesh") : result;
		}

		std::string UniqueStem(const std::string& stem, std::vector<std::string>& used)
		{
			const auto lower = [](std::string text)
			{
				std::transform(text.begin(), text.end(), text.begin(),
					[](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
				return text;
			};
			std::string candidate = stem;
			for (int suffix = 1; std::find(used.begin(), used.end(), lower(candidate)) != used.end(); ++suffix)
				candidate = stem + "_" + std::to_string(suffix);
			used.push_back(lower(candidate));
			return candidate;
		}

		std::string SaveMeshFile(const MeshAsset& mesh, const std::filesystem::path& targetDir, const std::string& stem)
		{
			AssetManager::WriteDefaultMaterialFiles(mesh);
			const std::string file = ToUtf8((targetDir / ToPath(stem + MeshSerializer::ExtensionFor(mesh))).generic_u8string());
			if (!MeshSerializer::Save(mesh, file))
			{
				CN_LOG_RUNTIME("Import", "메쉬 파일 저장 실패 : ", file);
				return {};
			}
			return AssetManager::ToAssetPath(file);
		}
	}

	std::string CNglTFImporter::CopyGltfTextures(const std::string& gltfPath, const std::string& targetDir, CNglTF& sourceGltf)
	{
		const std::filesystem::path source = ToPath(gltfPath);
		const std::filesystem::path target = ToPath(targetDir);

		if (!ReadglTF(gltfPath, sourceGltf))
			return {};

		std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies;
		Value& doc = sourceGltf.m_doc;

		std::error_code error;
		std::filesystem::create_directories(target, error);
		if (error)
			CN_LOG_RUNTIME("Import", "대상 폴더를 만들 수 없음 : ", targetDir, " (", error.message(), ")");
		const std::string stem = ToUtf8(source.stem().u8string());
		const uint32_t imageCount = Has(doc, "images") && doc["images"].IsArray() ? doc["images"].Size() : 0u;
		for (uint32_t i = 0; i < imageCount; ++i)
		{
			const std::filesystem::path to = (target / ToPath(ImageRelativePath(doc, i, stem))).lexically_normal();
			Value& image = doc["images"][i];
			const std::string uri = Has(image, "uri") && image["uri"].IsString() ? image["uri"].GetString() : std::string();
			if (!uri.empty() && !uri.starts_with("data:"))
			{
				const std::filesystem::path decoded = ToPath(PercentDecode(uri));
				const std::filesystem::path from = (decoded.is_absolute() ? decoded : source.parent_path() / decoded).lexically_normal();
				if (std::filesystem::exists(from, error))
					copies.emplace_back(from, to);
				else
					CN_LOG_RUNTIME("Import", "glTF 텍스처 파일 없음 : ", ToUtf8(from.generic_u8string()));
				continue;
			}

			std::vector<uint8_t> bytes;
			if (!ReadEmbeddedImage(sourceGltf, i, bytes))
			{
				CN_LOG_RUNTIME("Import", "glTF 내장 텍스처를 읽을 수 없음 : image ", i);
				continue;
			}
			std::filesystem::create_directories(to.parent_path(), error);
			FILE* file = nullptr;
			if (_wfopen_s(&file, to.c_str(), L"wb") == 0 && file)
			{
				const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), file);
				std::fclose(file);
				if (written != bytes.size())
					CN_LOG_RUNTIME("Import", "내장 텍스처 쓰기 실패 : ", ToUtf8(to.generic_u8string()));
			}
			else
			{
				CN_LOG_RUNTIME("Import", "내장 텍스처 파일을 만들 수 없음 : ", ToUtf8(to.generic_u8string()));
			}
		}

		for (const auto& [from, to] : copies)
		{
			if (std::filesystem::equivalent(from, to, error))
				continue;
			std::filesystem::create_directories(to.parent_path(), error);
			std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, error);
			if (error)
				CN_LOG_RUNTIME("Import", "파일 복사 실패 : ", ToUtf8(from.generic_u8string()), " -> ", ToUtf8(to.generic_u8string()),
					" (", error.message(), ")");
		}
		return ToUtf8((target / source.filename()).generic_u8string());
	}

	bool CNglTFImporter::ImportSkeletonModel(const std::string& gltfPath, const std::string& targetDir, const std::string& clipPath)
	try
	{
		const std::filesystem::path target = ToPath(targetDir);
		CNglTF cNglTF;
		const std::string mappedPath = CopyGltfTextures(gltfPath, targetDir, cNglTF);
		if (mappedPath.empty())
			return false;
		cNglTF.m_filePath = mappedPath;

		Value& doc = cNglTF.m_doc;
		const std::string modelName = ModelNameOf(ToPath(gltfPath));
		std::vector<std::string> usedStems;

		Scene scene(modelName);
		std::vector<GameObject*> nodeObjects;
		GameObject* top = LoadSkeleton(cNglTF, scene, modelName, nodeObjects);
		if (!top)
			return false;
		if (!clipPath.empty())
		{
			top->GetComponent<AnimatorComponent>()->SetClip(clipPath);
		}

		Value& nodes = ArrayMember(doc, "nodes", "glTF");
		const uint32_t skinCount = Has(doc, "skins") ? ArrayMember(doc, "skins", "glTF").Size() : 0u;
		for (uint32_t i = 0; i < skinCount; i++)
		{
			// 이 스킨을 쓰는 메쉬가 없으면(조인트만 있고 메쉬는 노드에 붙은 강체인 경우) 스킨 렌더러를 만들지 않는다.
			bool used = false;
			for (Value& node : nodes.GetArray())
			{
				if (Has(node, "mesh") && Has(node, "skin") && node["skin"].IsUint() && node["skin"].GetUint() == i)
					used = true;
			}
			if (!used)
				continue;

			const std::string skinWhere = Concat("skins[", i, "]");
			Value& skin = doc["skins"][i];
			const std::string skinName = StringOr(skin, "name", "Skin_" + std::to_string(i), skinWhere);

			std::vector<TransformComponent*> jointTransforms;
			for (Value& joint : ArrayMember(skin, "joints", skinWhere).GetArray())
			{
				const uint32_t jointNode = ToUint(joint, skinWhere + ".joints 원소");
				if (jointNode >= nodeObjects.size())
					Fail(skinWhere, ".joints 의 ", jointNode, " 가 nodes 범위를 넘음");
				if (nodeObjects[jointNode])
					jointTransforms.push_back(nodeObjects[jointNode]->GetTransform());
			}
			TransformComponent* skeletonRoot = top->GetTransform();
			if (!jointTransforms.empty())
			{
				TransformComponent* rootJoint = jointTransforms[0];
				while (rootJoint->GetParent() && std::find(jointTransforms.begin(), jointTransforms.end(), rootJoint->GetParent()) != jointTransforms.end())
				{
					rootJoint = rootJoint->GetParent();
				}
				if (rootJoint->GetParent())
					skeletonRoot = rootJoint->GetParent();
			}
			GameObject* meshObject = scene.GetGameObject(scene.AddGameObject(skinName));
			meshObject->GetTransform()->SetParent(top->GetTransform());

			const std::unique_ptr<MeshAsset> skinMesh = LoadSkinnedMeshAsset(cNglTF, i);
			if (!skinMesh || skinMesh->GetVertexFormat() != VertexFormat::Skinned)
				Fail("스킨 메쉬 변환 실패 : ", skinName);
			const std::string skinMeshPath = SaveMeshFile(*skinMesh, target,
				UniqueStem(SanitizeFileName(modelName + "_" + skinName), usedStems));
			if (skinMeshPath.empty())
				Fail("스킨 메쉬 저장 실패 : ", skinName);

			SkinnedMeshRendererComponent* renderer = meshObject->AddComponent<SkinnedMeshRendererComponent>();
			renderer->SetMesh(skinMeshPath);
			renderer->SetSkeleton(skeletonRoot);
		}

		// 노드에 붙은 일반 메쉬: 그 노드 오브젝트에서 노드 로컬 메쉬로 그린다. 노드가 애니메이션되면 메쉬도 같이 움직인다.
		for (uint32_t i = 0; i < nodes.Size(); i++)
		{
			if (!nodeObjects[i] || !Has(nodes[i], "mesh") || Has(nodes[i], "skin"))
				continue;
			const std::unique_ptr<MeshAsset> nodeMesh = LoadNodeMeshAsset(cNglTF, i);
			if (!nodeMesh)
			{
				CN_LOG_RUNTIME("Import", "노드 메쉬가 비어 있거나 잘못되어 건너뜀 : ", nodeObjects[i]->GetName());
				continue;
			}
			const std::string nodeName = StringOr(nodes[i], "name", "Node_" + std::to_string(i), Concat("nodes[", i, "]"));
			const std::string nodeMeshPath = SaveMeshFile(*nodeMesh, target,
				UniqueStem(SanitizeFileName(modelName + "_" + nodeName), usedStems));
			if (nodeMeshPath.empty())
				Fail("노드 메쉬 저장 실패 : ", nodeName);
			MeshRendererComponent* renderer = nodeObjects[i]->AddComponent<MeshRendererComponent>();
			renderer->SetMesh(nodeMeshPath);
		}

		const std::string prefabPath = ToUtf8((target / ToPath(modelName + "_SkeletonModel.prefab")).generic_u8string());
		if (!SceneSerializer::SavePrefab(*top, prefabPath))
			Fail("프리팹 저장 실패 : ", prefabPath);
		return true;
	}
	catch (const std::exception& error)
	{
		LogFailure("스켈레톤 모델 임포트", gltfPath, error.what());
		return false;
	}

	bool CNglTFImporter::ImportStaticModel(const std::string& gltfPath, const std::string& targetDir)
	try
	{
		const std::filesystem::path target = ToPath(targetDir);
		CNglTF cNglTF;
		const std::string mappedPath = CopyGltfTextures(gltfPath, targetDir, cNglTF);
		if (mappedPath.empty())
			return false;
		cNglTF.m_filePath = mappedPath;
		const std::unique_ptr<MeshAsset> mesh = LoadMeshAsset(cNglTF);
		if (!mesh || mesh->GetVertexCount() == 0)
			Fail("정적 모델에 메쉬가 없음 (원인은 위 로그 참고)");

		const std::string modelName = ModelNameOf(ToPath(gltfPath));
		const std::string meshPath = SaveMeshFile(*mesh, target, SanitizeFileName(modelName));
		if (meshPath.empty())
			Fail("메쉬 파일 저장 실패");

		Scene scene(modelName);
		GameObject* top = scene.GetGameObject(scene.AddGameObject(modelName));
		MeshRendererComponent* renderer = top->AddComponent<MeshRendererComponent>();
		renderer->SetMesh(meshPath);

		const std::string prefabPath = ToUtf8((target / ToPath(modelName + "_StaticModel.prefab")).generic_u8string());
		if (!SceneSerializer::SavePrefab(*top, prefabPath))
			Fail("프리팹 저장 실패 : ", prefabPath);
		return true;
	}
	catch (const std::exception& error)
	{
		LogFailure("스태틱 모델 임포트", gltfPath, error.what());
		return false;
	}
}
