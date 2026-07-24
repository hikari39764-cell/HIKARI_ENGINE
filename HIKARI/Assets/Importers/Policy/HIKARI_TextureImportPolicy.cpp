#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"

#include <json.hpp>

#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::IMPORT_POLICY {

    namespace {

        bool EndsWith(std::string_view text, std::string_view suffix) noexcept {
            return text.size() >= suffix.size() &&
                text.substr(text.size() - suffix.size()) == suffix;
        }

        TextureImportSettings GuessTextureImportSettings(
            const std::filesystem::path& sourcePath) {

            TextureImportSettings settings{};
            settings.dimension = TextureAssetDimension::Texture2D;
            settings.outputFormat = CookedAssetFormat::DDS;

            const std::string key = TEXT::ToLowerAsciiCopy(
                sourcePath.stem().string() + " " +
                sourcePath.generic_string());
            const std::string extension =
                TEXT::ToLowerAsciiCopy(sourcePath.extension().string());

            if (key.find("normal") != std::string::npos ||
                key.find("_nrm") != std::string::npos ||
                key.find("_n.") != std::string::npos) {
                settings.usage = TextureUsage::Normal;
                settings.colorSpace = TextureAssetColorSpace::Linear;
                settings.compression = TextureCompression::BC7;
                settings.mipPolicy = TextureMipPolicy::Generate;
            } else if (key.find("basecolor") != std::string::npos ||
                key.find("base_color") != std::string::npos ||
                key.find("albedo") != std::string::npos ||
                key.find("diffuse") != std::string::npos ||
                key.find("_color") != std::string::npos ||
                EndsWith(key, " color")) {
                settings.usage = TextureUsage::BaseColor;
                settings.colorSpace = TextureAssetColorSpace::Srgb;
                settings.compression = TextureCompression::BC7;
                settings.mipPolicy = TextureMipPolicy::Generate;
            } else if (
                key.find("metallicroughness") != std::string::npos ||
                key.find("metallic_roughness") != std::string::npos ||
                key.find("roughness") != std::string::npos ||
                key.find("metallic") != std::string::npos ||
                key.find("_orm") != std::string::npos ||
                key.find("_ao") != std::string::npos ||
                key.find("ambientocclusion") != std::string::npos ||
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

    std::string_view ToString(TextureUsage value) noexcept {
        switch (value) {
        case TextureUsage::BaseColor: return "BaseColor";
        case TextureUsage::Normal: return "Normal";
        case TextureUsage::MetallicRoughness: return "MetallicRoughness";
        case TextureUsage::Occlusion: return "Occlusion";
        case TextureUsage::Emissive: return "Emissive";
        case TextureUsage::Mask: return "Mask";
        case TextureUsage::UI: return "UI";
        case TextureUsage::SkyCubemap: return "SkyCubemap";
        case TextureUsage::IblIrradiance: return "IblIrradiance";
        case TextureUsage::IblPrefiltered: return "IblPrefiltered";
        case TextureUsage::BrdfLut: return "BrdfLut";
        case TextureUsage::Auto:
        default: return "Auto";
        }
    }

    std::string_view ToString(TextureAssetDimension value) noexcept {
        return value == TextureAssetDimension::TextureCube
            ? "TextureCube"
            : "Texture2D";
    }

    std::string_view ToString(TextureAssetColorSpace value) noexcept {
        switch (value) {
        case TextureAssetColorSpace::Linear: return "Linear";
        case TextureAssetColorSpace::Srgb: return "Srgb";
        case TextureAssetColorSpace::Auto:
        default: return "Auto";
        }
    }

    std::string_view ToString(TextureCompression value) noexcept {
        switch (value) {
        case TextureCompression::None: return "None";
        case TextureCompression::BC1: return "BC1";
        case TextureCompression::BC3: return "BC3";
        case TextureCompression::BC4: return "BC4";
        case TextureCompression::BC5: return "BC5";
        case TextureCompression::BC6H: return "BC6H";
        case TextureCompression::BC7: return "BC7";
        case TextureCompression::Auto:
        default: return "Auto";
        }
    }

    std::string_view ToString(TextureMipPolicy value) noexcept {
        switch (value) {
        case TextureMipPolicy::Generate: return "Generate";
        case TextureMipPolicy::Preserve: return "Preserve";
        case TextureMipPolicy::None: return "None";
        case TextureMipPolicy::Auto:
        default: return "Auto";
        }
    }

    TextureUsage ParseTextureUsage(
        std::string_view value,
        TextureUsage fallback) noexcept {

        if (value == "BaseColor") return TextureUsage::BaseColor;
        if (value == "Normal") return TextureUsage::Normal;
        if (value == "MetallicRoughness") {
            return TextureUsage::MetallicRoughness;
        }
        if (value == "Occlusion") return TextureUsage::Occlusion;
        if (value == "Emissive") return TextureUsage::Emissive;
        if (value == "Mask") return TextureUsage::Mask;
        if (value == "UI") return TextureUsage::UI;
        if (value == "SkyCubemap") return TextureUsage::SkyCubemap;
        if (value == "IblIrradiance") return TextureUsage::IblIrradiance;
        if (value == "IblPrefiltered") return TextureUsage::IblPrefiltered;
        if (value == "BrdfLut") return TextureUsage::BrdfLut;
        if (value == "Auto") return TextureUsage::Auto;
        return fallback;
    }

    TextureAssetDimension ParseTextureDimension(
        std::string_view value,
        TextureAssetDimension fallback) noexcept {

        if (value == "TextureCube") return TextureAssetDimension::TextureCube;
        if (value == "Texture2D") return TextureAssetDimension::Texture2D;
        return fallback;
    }

    TextureAssetColorSpace ParseTextureColorSpace(
        std::string_view value,
        TextureAssetColorSpace fallback) noexcept {

        if (value == "Linear") return TextureAssetColorSpace::Linear;
        if (value == "Srgb") return TextureAssetColorSpace::Srgb;
        if (value == "Auto") return TextureAssetColorSpace::Auto;
        return fallback;
    }

    TextureCompression ParseTextureCompression(
        std::string_view value,
        TextureCompression fallback) noexcept {

        if (value == "None") return TextureCompression::None;
        if (value == "BC1") return TextureCompression::BC1;
        if (value == "BC3") return TextureCompression::BC3;
        if (value == "BC4") return TextureCompression::BC4;
        if (value == "BC5") return TextureCompression::BC5;
        if (value == "BC6H") return TextureCompression::BC6H;
        if (value == "BC7") return TextureCompression::BC7;
        if (value == "Auto") return TextureCompression::Auto;
        return fallback;
    }

    TextureMipPolicy ParseTextureMipPolicy(
        std::string_view value,
        TextureMipPolicy fallback) noexcept {

        if (value == "Generate") return TextureMipPolicy::Generate;
        if (value == "Preserve") return TextureMipPolicy::Preserve;
        if (value == "None") return TextureMipPolicy::None;
        if (value == "Auto") return TextureMipPolicy::Auto;
        return fallback;
    }

    TextureImportSettings ResolveTextureImportSettings(
        const std::filesystem::path& sourcePath,
        std::string_view importSettingsJson) {

        TextureImportSettings settings =
            GuessTextureImportSettings(sourcePath);

        const nlohmann::json root = nlohmann::json::parse(
            importSettingsJson,
            nullptr,
            false);
        if (root.is_object()) {
            settings.usage = ParseTextureUsage(
                root.value("usage", std::string{}),
                settings.usage);
            settings.dimension = ParseTextureDimension(
                root.value("dimension", std::string{}),
                settings.dimension);
            settings.colorSpace = ParseTextureColorSpace(
                root.value("colorSpace", std::string{}),
                settings.colorSpace);
            settings.compression = ParseTextureCompression(
                root.value("compression", std::string{}),
                settings.compression);
            settings.mipPolicy = ParseTextureMipPolicy(
                root.value("mipPolicy", std::string{}),
                settings.mipPolicy);
            settings.forcePowerOfTwo =
                root.value("forcePowerOfTwo", settings.forcePowerOfTwo);
            settings.allowResize =
                root.value("allowResize", settings.allowResize);
            settings.maxSize = root.value("maxSize", settings.maxSize);
        }

        if (settings.usage == TextureUsage::Normal) {
            settings.colorSpace = TextureAssetColorSpace::Linear;
            if (settings.compression == TextureCompression::Auto) {
                settings.compression = TextureCompression::BC7;
            }
        }
        return settings;
    }

    std::string MakeTextureImportSettingsJson(
        const TextureImportSettings& settings) {

        using ASSETS::SEMANTICS::ToString;
        return nlohmann::json{
            { "usage", IMPORT_POLICY::ToString(settings.usage) },
            { "dimension", IMPORT_POLICY::ToString(settings.dimension) },
            { "colorSpace", IMPORT_POLICY::ToString(settings.colorSpace) },
            { "mipPolicy", IMPORT_POLICY::ToString(settings.mipPolicy) },
            { "compression", IMPORT_POLICY::ToString(settings.compression) },
            { "outputFormat", ToString(CookedAssetFormat::HTEX) },
            { "debugOutputFormat", ToString(CookedAssetFormat::DDS) },
            { "forcePowerOfTwo", settings.forcePowerOfTwo },
            { "allowResize", settings.allowResize },
            { "maxSize", settings.maxSize },
        }.dump(2);
    }

} // namespace HIKARI::ASSETS::IMPORT_POLICY
