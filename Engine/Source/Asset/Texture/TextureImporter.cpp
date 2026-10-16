#include "Asset/Texture/TextureImporter.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <sstream>
#include <vector>

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

	std::unique_ptr<TextureAsset> TextureImporter::LoadTexture(const std::filesystem::path& path, std::string* outError)
	{
		using Microsoft::WRL::ComPtr;
		auto fail = [&](const char* operation, HRESULT result) -> std::unique_ptr<TextureAsset>
		{
			if (outError)
			{
				const auto utf8Path = path.generic_u8string();
				std::ostringstream stream;
				stream << std::string(utf8Path.begin(), utf8Path.end()) << ": " << operation
					<< " failed (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
				*outError = stream.str();
			}
			return nullptr;
		};

		ComScope com;
		// An existing STA is also usable; it belongs to the caller and must not be uninitialized here.
		if (FAILED(com.Result) && com.Result != RPC_E_CHANGED_MODE) return fail("CoInitializeEx", com.Result);
		ComPtr<IWICImagingFactory> factory;
		HRESULT result = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
		if (FAILED(result)) return fail("Create WIC factory", result);
		ComPtr<IWICBitmapDecoder> decoder;
		result = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
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
		if (outError) outError->clear();
		return std::make_unique<TextureAsset>(width, height, std::move(pixels));
	}
}
