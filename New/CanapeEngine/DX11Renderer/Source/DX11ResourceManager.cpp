#include "DX11ResourceManager.h"

#include "../DX11Device.h"
#include "../DX11MeshResource.h"
#include "../DX11TextureResource.h"
#include "../DX11MaterialResource.h"

namespace Canape
{
	DX11ResourceManager::DX11ResourceManager(Device& device)
		: m_Device(device)
	{
	}

	DX11ResourceManager::~DX11ResourceManager() = default;

	bool DX11ResourceManager::UpLoadMeshResouce()
	{
		return true;
	}

	MeshResourceId DX11ResourceManager::CreateMeshResource(const void* vertexData, uint32_t vertexCount, uint32_t vertexStride,
		std::span<const uint32_t> indices, std::span<const DX11SubMesh> subMeshes)
	{
		auto resource = std::make_unique<DX11MeshResource>();

		const UINT vertexBytes = vertexCount * vertexStride;
		if (!m_Device.CreateVertexBuffer(vertexData, vertexBytes, resource->VertexBuffer.GetAddressOf()))
		{
			return {};
		}

		const UINT indexBytes = static_cast<UINT>(indices.size_bytes());
		if (!m_Device.CreateIndexBuffer(indices.data(), indexBytes, resource->IndexBuffer.GetAddressOf()))
		{
			return {};
		}

		resource->Id = MeshResourceId{ ++m_NextMeshResourceId };
		resource->VertexStride = vertexStride;
		resource->IndexFormat = DXGI_FORMAT_R32_UINT;
		resource->SubMeshes.assign(subMeshes.begin(), subMeshes.end());

		const MeshResourceId id = resource->Id;
		m_MeshResources.emplace(id, std::move(resource));
		return id;
	}

	TextureResourceId DX11ResourceManager::CreateTextureResource(uint32_t width, uint32_t height, std::span<const uint8_t> rgba8Pixels)
	{
		if (width == 0 || height == 0 || rgba8Pixels.size() != static_cast<size_t>(width) * height * 4)
		{
			return {};
		}

		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = width;
		desc.Height = height;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA initData = {};
		initData.pSysMem = rgba8Pixels.data();
		initData.SysMemPitch = width * 4;

		auto resource = std::make_unique<DX11TextureResource>();
		resource->Texture = m_Device.CreateTexture2D(desc, &initData);
		if (!resource->Texture)
		{
			return {};
		}

		if (!m_Device.CreateShaderResourceView(resource->Texture.Get(), resource->ShaderResourceView.GetAddressOf()))
		{
			return {};
		}

		resource->Id = TextureResourceId{ ++m_NextTextureResourceId };
		resource->Width = width;
		resource->Height = height;

		const TextureResourceId id = resource->Id;
		m_TextureResources.emplace(id, std::move(resource));
		return id;
	}

	MaterialResourceId DX11ResourceManager::CreateMaterialResource(const DX11MaterialDesc& desc, TextureResourceId baseColorTexture)
	{
		if (baseColorTexture.IsValid() && !GetTextureResource(baseColorTexture))
		{
			return {};
		}

		auto resource = std::make_unique<DX11MaterialResource>();
		resource->Id = MaterialResourceId{ ++m_NextMaterialResourceId };
		resource->Desc = desc;
		resource->BaseColorTexture = baseColorTexture;

		const MaterialResourceId id = resource->Id;
		m_MaterialResources.emplace(id, std::move(resource));
		return id;
	}

	void DX11ResourceManager::DestroyMeshResource(MeshResourceId id)
	{
		m_MeshResources.erase(id);
	}

	void DX11ResourceManager::DestroyTextureResource(TextureResourceId id)
	{
		m_TextureResources.erase(id);
	}

	void DX11ResourceManager::DestroyMaterialResource(MaterialResourceId id)
	{
		m_MaterialResources.erase(id);
	}

	DX11TextureResource* DX11ResourceManager::GetTextureResource(TextureResourceId id)
	{
		auto it = m_TextureResources.find(id);
		return it != m_TextureResources.end() ? it->second.get() : nullptr;
	}

	const DX11TextureResource* DX11ResourceManager::GetTextureResource(TextureResourceId id) const
	{
		auto it = m_TextureResources.find(id);
		return it != m_TextureResources.end() ? it->second.get() : nullptr;
	}

	DX11MaterialResource* DX11ResourceManager::GetMaterialResource(MaterialResourceId id)
	{
		auto it = m_MaterialResources.find(id);
		return it != m_MaterialResources.end() ? it->second.get() : nullptr;
	}

	const DX11MaterialResource* DX11ResourceManager::GetMaterialResource(MaterialResourceId id) const
	{
		auto it = m_MaterialResources.find(id);
		return it != m_MaterialResources.end() ? it->second.get() : nullptr;
	}

	DX11MeshResource* DX11ResourceManager::GetMeshResource(MeshResourceId id)
	{
		auto it = m_MeshResources.find(id);
		return it != m_MeshResources.end() ? it->second.get() : nullptr;
	}

	const DX11MeshResource* DX11ResourceManager::GetMeshResource(MeshResourceId id) const
	{
		auto it = m_MeshResources.find(id);
		return it != m_MeshResources.end() ? it->second.get() : nullptr;
	}
}
