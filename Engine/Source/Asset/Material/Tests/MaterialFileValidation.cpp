#include "Asset/Material/MaterialFile.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace Canape;

static void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 2, "fixture directory argument");
        const std::filesystem::path directory = argv[1];
        std::filesystem::create_directories(directory);
        const auto file = directory / "Lightmap.mat";
        MaterialData source;
        source.baseColorTexture = "Content/Textures/BaseColor.png";
        source.normalTexture = "Content/Textures/Normal.png";
        source.occlusionTexture = "Content/Textures/AO.png";
        source.emissiveTexture = "Content/Textures/Emissive.png";
        source.lightmapTexture = "Content/Textures/Lightmap.png";
        source.desc.uvIndex = 1; // Retain legacy metadata without selecting surface-map UVs.
        Require(MaterialFile::Save(file, source), "save lightmap material");
        MaterialData loaded;
        Require(MaterialFile::Load(file, loaded), "load lightmap material");
        Require(loaded.lightmapTexture == source.lightmapTexture, "lightmap path round trip");
        Require(loaded.GetTexturePath(MaterialTextureSlot::Lightmap) == source.lightmapTexture,
            "generic material texture loader can resolve the lightmap slot");
        Require(loaded.baseColorTexture == source.baseColorTexture
            && loaded.normalTexture == source.normalTexture
            && loaded.occlusionTexture == source.occlusionTexture
            && loaded.emissiveTexture == source.emissiveTexture, "existing slots preserved");
        Require(loaded.desc.uvIndex == 1, "legacy UV metadata preserved");
        Require(GetMaterialTextureFormat(MaterialTextureSlot::Lightmap) == TextureFormat::RGBA8_UNORM,
            "lightmap uses the existing linear texture path");

        const auto legacy = directory / "Legacy.mat";
        { std::ofstream stream(legacy); stream << R"({"version":1,"baseColorTexture":"Content/Old.png","desc":{"uvIndex":1}})"; }
        Require(MaterialFile::Load(legacy, loaded), "old material without lightmap loads");
        Require(loaded.lightmapTexture.empty() && loaded.baseColorTexture == "Content/Old.png"
            && loaded.desc.uvIndex == 1, "legacy load clears a previously populated lightmap");
        Require(MaterialFile::Save(file, loaded) && MaterialFile::Load(file, loaded)
            && loaded.lightmapTexture.empty(), "empty lightmap round trip");
        std::cout << "PASS: lightmap slot round trip, old .mat compatibility, empty defaults and linear format\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
