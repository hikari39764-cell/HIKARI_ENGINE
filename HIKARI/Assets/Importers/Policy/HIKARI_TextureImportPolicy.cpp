#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Core/Text/HIKARI_EnumNames.h"

#include <json.hpp>

#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::IMPORT_POLICY {

namespace {

bool EndsWith(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

TextureImportSettings GuessTextureImportSettings(const std::filesystem::path& sourcePath) {
    TextureImportSettings settings{};
    settings.dimension = TextureAssetDimension::Texture2D;
    settings.outputFormat = CookedAssetFormat::DDS;

    const std::string key = TEXT::ToLowerAsciiCopy(sourcePath.stem().string());
    const std::string extension = TEXT::ToLowerAsciiCopy(sourcePath.extension().string());

    if (key.find("normal") != std::string::npos || key.find("_nrm") != std::string::npos || EndsWith(key, "_n")) {
        settings.usage = TextureUsage::Normal;
        settings.colorSpace = TextureAssetColorSpace::Linear;
        settings.compression = TextureCompression::BC7;
        settings.mipPolicy = TextureMipPolicy::Generate;
    } else if (key.find("basecolor") != std::string::npos || key.find("base_color") != std::string::npos ||
               key.find("albedo") != std::string::npos || key.find("diffuse") != std::string::npos ||
               key.find("_color") != std::string::npos || EndsWith(key, " color")) {
        settings.usage = TextureUsage::BaseColor;
        settings.colorSpace = TextureAssetColorSpace::Srgb;
        settings.compression = TextureCompression::BC7;
        settings.mipPolicy = TextureMipPolicy::Generate;
    } else if (key.find("metallicroughness") != std::string::npos ||
               key.find("metallic_roughness") != std::string::npos || key.find("metalrough") != std::string::npos ||
               key.find("metal_rough") != std::string::npos || key.find("roughness") != std::string::npos ||
               key.find("metallic") != std::string::npos || key.find("_orm") != std::string::npos ||
               key.find("_ao") != std::string::npos || key.find("ambientocclusion") != std::string::npos ||
               key.find("occlusion") != std::string::npos) {
        settings.usage = TextureUsage::MetallicRoughness;
        settings.colorSpace = TextureAssetColorSpace::Linear;
        settings.compression = TextureCompression::BC7;
        settings.mipPolicy = TextureMipPolicy::Generate;
    } else if (key.find("emissive") != std::string::npos) {
        settings.usage = TextureUsage::Emissive;
        settings.colorSpace = TextureAssetColorSpace::Srgb;
        settings.compression = TextureCompression::BC7;
        settings.mipPolicy = TextureMipPolicy::Generate;
    } else if (key.find("mask") != std::string::npos) {
        settings.usage = TextureUsage::Mask;
        settings.colorSpace = TextureAssetColorSpace::Linear;
        settings.compression = TextureCompression::BC7;
        settings.mipPolicy = TextureMipPolicy::Generate;
    } else {
        settings.usage = TextureUsage::Auto;
        settings.colorSpace = TextureAssetColorSpace::Srgb;
        settings.compression = TextureCompression::BC7;
        settings.mipPolicy = TextureMipPolicy::Generate;
    }

    if (extension == ".dds") {
        settings.colorSpace = TextureAssetColorSpace::Auto;
        settings.mipPolicy = TextureMipPolicy::Preserve;
        settings.compression = TextureCompression::None;
    } else if (extension == ".hdr") {
        settings.colorSpace = TextureAssetColorSpace::Linear;
        settings.compression = TextureCompression::None;
    }
    return settings;
}

} // namespace

namespace {

constexpr TEXT::EnumName<TextureUsage> kUsageNames[] = {
    {TextureUsage::Auto, "Auto"},
    {TextureUsage::BaseColor, "BaseColor"},
    {TextureUsage::Normal, "Normal"},
    {TextureUsage::MetallicRoughness, "MetallicRoughness"},
    {TextureUsage::Occlusion, "Occlusion"},
    {TextureUsage::Emissive, "Emissive"},
    {TextureUsage::Mask, "Mask"},
    {TextureUsage::UI, "UI"},
    {TextureUsage::SkyCubemap, "SkyCubemap"},
    {TextureUsage::IblIrradiance, "IblIrradiance"},
    {TextureUsage::IblPrefiltered, "IblPrefiltered"},
    {TextureUsage::BrdfLut, "BrdfLut"},
};
constexpr TEXT::EnumName<TextureAssetDimension> kDimensionNames[] = {
    {TextureAssetDimension::Texture2D, "Texture2D"},
    {TextureAssetDimension::TextureCube, "TextureCube"},
};
constexpr TEXT::EnumName<TextureAssetColorSpace> kColorSpaceNames[] = {
    {TextureAssetColorSpace::Auto, "Auto"},
    {TextureAssetColorSpace::Linear, "Linear"},
    {TextureAssetColorSpace::Srgb, "Srgb"},
};
constexpr TEXT::EnumName<TextureCompression> kCompressionNames[] = {
    {TextureCompression::Auto, "Auto"}, {TextureCompression::None, "None"}, {TextureCompression::BC1, "BC1"},
    {TextureCompression::BC3, "BC3"},   {TextureCompression::BC4, "BC4"},   {TextureCompression::BC5, "BC5"},
    {TextureCompression::BC6H, "BC6H"}, {TextureCompression::BC7, "BC7"},
};
constexpr TEXT::EnumName<TextureMipPolicy> kMipPolicyNames[] = {
    {TextureMipPolicy::Auto, "Auto"},
    {TextureMipPolicy::Generate, "Generate"},
    {TextureMipPolicy::Preserve, "Preserve"},
    {TextureMipPolicy::None, "None"},
};

} // namespace

std::string_view ToString(TextureUsage value) noexcept { return TEXT::EnumToName(kUsageNames, value, "Auto"); }
std::string_view ToString(TextureAssetDimension value) noexcept {
    return TEXT::EnumToName(kDimensionNames, value, "Texture2D");
}
std::string_view ToString(TextureAssetColorSpace value) noexcept {
    return TEXT::EnumToName(kColorSpaceNames, value, "Auto");
}
std::string_view ToString(TextureCompression value) noexcept {
    return TEXT::EnumToName(kCompressionNames, value, "Auto");
}
std::string_view ToString(TextureMipPolicy value) noexcept { return TEXT::EnumToName(kMipPolicyNames, value, "Auto"); }

TextureUsage ParseTextureUsage(std::string_view value, TextureUsage fallback) noexcept {
    return TEXT::ParseEnum(kUsageNames, value, fallback);
}
TextureAssetDimension ParseTextureDimension(std::string_view value, TextureAssetDimension fallback) noexcept {
    return TEXT::ParseEnum(kDimensionNames, value, fallback);
}
TextureAssetColorSpace ParseTextureColorSpace(std::string_view value, TextureAssetColorSpace fallback) noexcept {
    return TEXT::ParseEnum(kColorSpaceNames, value, fallback);
}
TextureCompression ParseTextureCompression(std::string_view value, TextureCompression fallback) noexcept {
    return TEXT::ParseEnum(kCompressionNames, value, fallback);
}
TextureMipPolicy ParseTextureMipPolicy(std::string_view value, TextureMipPolicy fallback) noexcept {
    return TEXT::ParseEnum(kMipPolicyNames, value, fallback);
}

TextureImportSettings ResolveTextureImportSettings(const std::filesystem::path& sourcePath,
                                                   std::string_view importSettingsJson) {
    TextureImportSettings settings = GuessTextureImportSettings(sourcePath);

    const nlohmann::json root = nlohmann::json::parse(importSettingsJson, nullptr, false);
    if (root.is_object()) {
        settings.usage = ParseTextureUsage(root.value("usage", std::string{}), settings.usage);
        settings.dimension = ParseTextureDimension(root.value("dimension", std::string{}), settings.dimension);
        settings.colorSpace = ParseTextureColorSpace(root.value("colorSpace", std::string{}), settings.colorSpace);
        settings.compression = ParseTextureCompression(root.value("compression", std::string{}), settings.compression);
        settings.mipPolicy = ParseTextureMipPolicy(root.value("mipPolicy", std::string{}), settings.mipPolicy);
        settings.forcePowerOfTwo = root.value("forcePowerOfTwo", settings.forcePowerOfTwo);
        settings.allowResize = root.value("allowResize", settings.allowResize);
        settings.maxSize = root.value("maxSize", settings.maxSize);
    }

    if (IsDataTextureUsage(settings.usage)) {
        settings.colorSpace = TextureAssetColorSpace::Linear;
        if (settings.compression == TextureCompression::Auto) {
            settings.compression = TextureCompression::BC7;
        }
    }
    return settings;
}

std::string MakeTextureImportSettingsJson(const TextureImportSettings& settings) {
    using ASSETS::SEMANTICS::ToString;
    return nlohmann::json{
        {"usage", IMPORT_POLICY::ToString(settings.usage)},
        {"dimension", IMPORT_POLICY::ToString(settings.dimension)},
        {"colorSpace", IMPORT_POLICY::ToString(settings.colorSpace)},
        {"mipPolicy", IMPORT_POLICY::ToString(settings.mipPolicy)},
        {"compression", IMPORT_POLICY::ToString(settings.compression)},
        {"outputFormat", ToString(CookedAssetFormat::HTEX)},
        {"debugOutputFormat", ToString(CookedAssetFormat::DDS)},
        {"forcePowerOfTwo", settings.forcePowerOfTwo},
        {"allowResize", settings.allowResize},
        {"maxSize", settings.maxSize},
    }
        .dump(2);
}

} // namespace HIKARI::ASSETS::IMPORT_POLICY
