#include "Asset/ResourceManager.h"
#include "Asset/FbxImporter.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <filesystem>
#include <sstream>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace Canape
{
	namespace
	{
		struct ComScope
		{
			HRESULT Result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			~ComScope() { if (SUCCEEDED(Result)) CoUninitialize(); }
		};
	}
	std::shared_ptr<Mesh> ResourceManager::LoadMesh(const std::string& path)
	{
		const std::string key = std::filesystem::path(path).lexically_normal().generic_string();

		if (const auto found = m_Meshes.find(key); found != m_Meshes.end())
		{
			return found->second;
		}

		std::string error;
		std::shared_ptr<Mesh> mesh = FbxImporter::LoadMesh(path, &error);
		if (!mesh)
		{
			OutputDebugStringA(("[ResourceManager] Failed to load mesh: " + error + "\n").c_str());
			return nullptr;
		}

		m_Meshes.emplace(key, mesh);
		return mesh;
	}

	std::shared_ptr<TextureAsset> ResourceManager::LoadTexture(const std::string& path, std::string* outError)
	{
		using Microsoft::WRL::ComPtr;
		auto fail = [&](const char* operation, HRESULT result) -> std::shared_ptr<TextureAsset>
		{
			std::ostringstream stream;
			stream << path << ": " << operation << " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
			if (outError) *outError = stream.str();
			OutputDebugStringA(("[ResourceManager] " + stream.str() + "\n").c_str());
			return nullptr;
		};
		std::error_code pathError;
		std::filesystem::path filePath;
		try
		{
			filePath = std::filesystem::absolute(std::filesystem::path(std::u8string(path.begin(), path.end())), pathError).lexically_normal();
		}
		catch (const std::system_error&)
		{
			return fail("Decode texture path", E_INVALIDARG);
		}
		if (pathError) return fail("Resolve texture path", HRESULT_FROM_WIN32(pathError.value()));
		const auto utf8Key = filePath.generic_u8string();
		const std::string key(utf8Key.begin(), utf8Key.end());
		if (const auto found = m_Textures.find(key); found != m_Textures.end())
		{
			if (outError) outError->clear();
			return found->second;
		}

		ComScope com;
		// An existing STA is also usable; it belongs to the caller and must not be uninitialized here.
		if (FAILED(com.Result) && com.Result != RPC_E_CHANGED_MODE) return fail("CoInitializeEx", com.Result);
		ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if (FAILED(result)) return fail("Create WIC factory", result);
		ComPtr<IWICBitmapDecoder> decoder;
		result = factory->CreateDecoderFromFilename(filePath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
		if (FAILED(result)) return fail("Decode image", result);
		ComPtr<IWICBitmapFrameDecode> frame;
		result = decoder->GetFrame(0, &frame);
		if (FAILED(result)) return fail("Get image frame", result);
		UINT width = 0, height = 0;
		result = frame->GetSize(&width, &height);
		if (FAILED(result)) return fail("Get image size", result);
		// WIC CopyPixels uses 32-bit row pitch and total byte count.
		if (width == 0 || height == 0 || uint64_t(width) * height > UINT32_MAX / 4)
			return fail("Validate image size", E_INVALIDARG);
		ComPtr<IWICFormatConverter> converter;
		result = factory->CreateFormatConverter(&converter);
		if (FAILED(result)) return fail("Create RGBA converter", result);
		result = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
			nullptr, 0.0, WICBitmapPaletteTypeCustom);
		if (FAILED(result)) return fail("Convert image to RGBA8", result);
		std::vector<uint8_t> pixels(size_t(width) * height * 4);
		result = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
		if (FAILED(result)) return fail("Copy image pixels", result);
		auto texture = std::make_shared<TextureAsset>(width, height, std::move(pixels));
		m_Textures.emplace(key, texture); // Failed decoding never creates a cache entry.
		if (outError) outError->clear();
		return texture;
	}

	void ResourceManager::Clear()
	{
		m_Meshes.clear();
		m_Textures.clear();
	}
}
