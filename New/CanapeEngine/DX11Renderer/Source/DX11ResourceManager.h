#pragma once

#include "../DX11ResourceId.h"

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>

namespace Canape
{
	class Device;
	struct DX11SubMesh;
	struct DX11MaterialDesc;

	class DX11ResourceManager
	{
	public:
		explicit DX11ResourceManager(Device& device);
		~DX11ResourceManager();
		DX11ResourceManager(const DX11ResourceManager&) = delete;
		DX11ResourceManager& operator=(const DX11ResourceManager&) = delete;

		bool UpLoadMeshResouce();

		MeshResourceId CreateMeshResource(const void* vertexData, uint32_t vertexCount, uint32_t vertexStride,
			std::span<const uint32_t> indices, std::span<const DX11SubMesh> subMeshes);

		TextureResourceId CreateTextureResource(uint32_t width, uint32_t height, std::span<const uint8_t> rgba8Pixels);

		MaterialResourceId CreateMaterialResource(const DX11MaterialDesc& desc, TextureResourceId baseColorTexture);

		void DestroyMeshResource(MeshResourceId id);
		void DestroyTextureResource(TextureResourceId id);
		void DestroyMaterialResource(MaterialResourceId id);

		DX11MeshResource* GetMeshResource(MeshResourceId id);
		const DX11MeshResource* GetMeshResource(MeshResourceId id) const;

		DX11TextureResource* GetTextureResource(TextureResourceId id);
		const DX11TextureResource* GetTextureResource(TextureResourceId id) const;

		DX11MaterialResource* GetMaterialResource(MaterialResourceId id);
		const DX11MaterialResource* GetMaterialResource(MaterialResourceId id) const;

	private:
		Device& m_Device;

		std::unordered_map<MeshResourceId, std::unique_ptr<DX11MeshResource>,
			ResourceHash<DX11MeshResource>> m_MeshResources;
		uint64_t m_NextMeshResourceId = 0;

		std::unordered_map<TextureResourceId, std::unique_ptr<DX11TextureResource>,
			ResourceHash<DX11TextureResource>> m_TextureResources;
		uint64_t m_NextTextureResourceId = 0;

		std::unordered_map<MaterialResourceId, std::unique_ptr<DX11MaterialResource>,
			ResourceHash<DX11MaterialResource>> m_MaterialResources;
		uint64_t m_NextMaterialResourceId = 0;
	};
}
