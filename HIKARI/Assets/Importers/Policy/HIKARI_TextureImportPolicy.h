#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Assets/Importers/HIKARI_TextureImportSettings.h"

namespace HIKARI::ASSETS::IMPORT_POLICY {

    std::string_view ToString(TextureUsage value) noexcept;
    std::string_view ToString(TextureAssetDimension value) noexcept;
    std::string_view ToString(TextureAssetColorSpace value) noexcept;
    std::string_view ToString(TextureCompression value) noexcept;
    std::string_view ToString(TextureMipPolicy value) noexcept;

    TextureUsage ParseTextureUsage(
        std::string_view value,
        TextureUsage fallback) noexcept;
    TextureAssetDimension ParseTextureDimension(
        std::string_view value,
        TextureAssetDimension fallback) noexcept;
    TextureAssetColorSpace ParseTextureColorSpace(
        std::string_view value,
        TextureAssetColorSpace fallback) noexcept;
    TextureCompression ParseTextureCompression(
        std::string_view value,
        TextureCompression fallback) noexcept;
    TextureMipPolicy ParseTextureMipPolicy(
        std::string_view value,
        TextureMipPolicy fallback) noexcept;

    TextureImportSettings ResolveTextureImportSettings(
        const std::filesystem::path& sourcePath,
        std::string_view importSettingsJson);
    std::string MakeTextureImportSettingsJson(
        const TextureImportSettings& settings);

} // namespace HIKARI::ASSETS::IMPORT_POLICY
