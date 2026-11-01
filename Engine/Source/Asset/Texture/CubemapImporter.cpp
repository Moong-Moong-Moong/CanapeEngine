#include "Asset/Texture/CubemapImporter.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace Canape
{
	namespace
	{
		struct DdsPixelFormat
		{
			uint32_t size;
			uint32_t flags;
			uint32_t fourCC;
			uint32_t rgbBitCount;
			uint32_t rMask;
			uint32_t gMask;
			uint32_t bMask;
			uint32_t aMask;
		};

		struct DdsHeader
		{
			uint32_t size;
			uint32_t flags;
			uint32_t height;
			uint32_t width;
			uint32_t pitchOrLinearSize;
			uint32_t depth;
			uint32_t mipMapCount;
			uint32_t reserved1[11];
			DdsPixelFormat pixelFormat;
			uint32_t caps;
			uint32_t caps2;
			uint32_t caps3;
			uint32_t caps4;
			uint32_t reserved2;
		};

		struct DdsHeaderDx10
		{
			uint32_t dxgiFormat;
			uint32_t resourceDimension;
			uint32_t miscFlag;
			uint32_t arraySize;
			uint32_t miscFlags2;
		};

		static_assert(sizeof(DdsPixelFormat) == 32);
		static_assert(sizeof(DdsHeader) == 124);
		static_assert(sizeof(DdsHeaderDx10) == 20);

		constexpr uint32_t kDdsMagic = 0x20534444;
		constexpr uint32_t kFourCCDx10 = 0x30315844;
		constexpr uint32_t kDdsdMipMapCount = 0x20000;
		constexpr uint32_t kDdpfAlphaPixels = 0x1;
		constexpr uint32_t kDdpfFourCC = 0x4;
		constexpr uint32_t kDdpfRgb = 0x40;
		constexpr uint32_t kDdsCaps2Cubemap = 0x200;
		constexpr uint32_t kDdsCaps2AllFaces = 0xFC00;
		constexpr uint32_t kDx10DimensionTexture2D = 3;
		constexpr uint32_t kDx10MiscTextureCube = 0x4;
		constexpr uint32_t kDxgiR8G8B8A8Unorm = 28;
		constexpr uint32_t kDxgiR8G8B8A8UnormSrgb = 29;
		constexpr uint32_t kDxgiB8G8R8A8Unorm = 87;
		constexpr uint32_t kDxgiB8G8R8A8UnormSrgb = 91;
		constexpr uint32_t kMaxFaceSize = 16384;

		enum class ChannelOrder { Rgba, Bgra, Rgbx, Bgrx };
	}

	std::unique_ptr<CubemapAsset> CubemapImporter::LoadCubemap(const std::filesystem::path& path, std::string* outError)
	{
		auto fail = [&](const char* reason) -> std::unique_ptr<CubemapAsset>
		{
			if (outError)
			{
				const auto utf8Path = path.generic_u8string();
				*outError = std::string(utf8Path.begin(), utf8Path.end()) + ": " + reason;
			}
			return nullptr;
		};

		std::ifstream file(path, std::ios::binary);
		if (!file) return fail("Open DDS file");
		const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

		size_t offset = 0;
		auto read = [&](void* destination, size_t size)
		{
			if (bytes.size() - offset < size) return false;
			std::memcpy(destination, bytes.data() + offset, size);
			offset += size;
			return true;
		};

		uint32_t magic = 0;
		DdsHeader header{};
		if (!read(&magic, sizeof(magic)) || magic != kDdsMagic || !read(&header, sizeof(header)) || header.size != sizeof(header))
			return fail("Invalid DDS header");

		ChannelOrder order = ChannelOrder::Rgba;
		const DdsPixelFormat& pixelFormat = header.pixelFormat;
		if ((pixelFormat.flags & kDdpfFourCC) && pixelFormat.fourCC == kFourCCDx10)
		{
			DdsHeaderDx10 dx10{};
			if (!read(&dx10, sizeof(dx10))) return fail("Invalid DDS DX10 header");
			if (dx10.resourceDimension != kDx10DimensionTexture2D || !(dx10.miscFlag & kDx10MiscTextureCube) || dx10.arraySize != 1)
				return fail("DDS is not a single cubemap");
			if (dx10.dxgiFormat == kDxgiR8G8B8A8Unorm || dx10.dxgiFormat == kDxgiR8G8B8A8UnormSrgb)
				order = ChannelOrder::Rgba;
			else if (dx10.dxgiFormat == kDxgiB8G8R8A8Unorm || dx10.dxgiFormat == kDxgiB8G8R8A8UnormSrgb)
				order = ChannelOrder::Bgra;
			else
				return fail("Unsupported DDS pixel format");
		}
		else
		{
			if (!(header.caps2 & kDdsCaps2Cubemap) || (header.caps2 & kDdsCaps2AllFaces) != kDdsCaps2AllFaces)
				return fail("DDS is not a complete cubemap");
			if (!(pixelFormat.flags & kDdpfRgb) || pixelFormat.rgbBitCount != 32)
				return fail("Unsupported DDS pixel format");
			const bool hasAlpha = (pixelFormat.flags & kDdpfAlphaPixels) && pixelFormat.aMask == 0xFF000000;
			if (pixelFormat.rMask == 0x000000FF && pixelFormat.gMask == 0x0000FF00 && pixelFormat.bMask == 0x00FF0000)
				order = hasAlpha ? ChannelOrder::Rgba : ChannelOrder::Rgbx;
			else if (pixelFormat.rMask == 0x00FF0000 && pixelFormat.gMask == 0x0000FF00 && pixelFormat.bMask == 0x000000FF)
				order = hasAlpha ? ChannelOrder::Bgra : ChannelOrder::Bgrx;
			else
				return fail("Unsupported DDS pixel format");
		}

		if (header.width == 0 || header.width != header.height) return fail("DDS cubemap faces must be square");
		if (header.width > kMaxFaceSize) return fail("DDS cubemap face size is too large");
		const uint32_t mipLevels = (header.flags & kDdsdMipMapCount) && header.mipMapCount > 0 ? header.mipMapCount : 1;
		if (mipLevels > 32 || (header.width >> (mipLevels - 1)) == 0) return fail("Invalid DDS mip count");

		uint64_t byteCount = 0;
		for (uint32_t mip = 0; mip < mipLevels; ++mip)
		{
			const uint64_t size = header.width >> mip;
			byteCount += size * size * 4;
		}
		byteCount *= 6;
		if (bytes.size() - offset < byteCount) return fail("DDS pixel data is truncated");

		std::vector<uint8_t> pixels(bytes.begin() + offset, bytes.begin() + offset + static_cast<size_t>(byteCount));
		if (order != ChannelOrder::Rgba)
		{
			for (size_t i = 0; i < pixels.size(); i += 4)
			{
				if (order == ChannelOrder::Bgra || order == ChannelOrder::Bgrx) std::swap(pixels[i], pixels[i + 2]);
				if (order == ChannelOrder::Rgbx || order == ChannelOrder::Bgrx) pixels[i + 3] = 255;
			}
		}

		if (outError) outError->clear();
		return std::make_unique<CubemapAsset>(header.width, mipLevels, std::move(pixels));
	}
}
