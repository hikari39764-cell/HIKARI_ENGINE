#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include "Assets/HIKARI_AssetMeta.h"

namespace HIKARI::ASSETS::SEMANTICS {

    enum class AssetImporterKind : uint8_t {
        SkyCubemap,
        Texture,
        Model,
        Scene,
        Sequence,
        AnimationStateMachine,
        Material,
        VfxEffect,
        Count,
    };

    struct AssetImporterSemantics {
        AssetImporterKind kind = AssetImporterKind::Texture;
        AssetType assetType = AssetType::Unknown;
        std::string_view importerId{};
        uint32_t importerVersion = 1;
        std::string_view supportedSources{};
    };

    std::string_view ToString(AssetType type) noexcept;
    AssetType ParseAssetType(std::string_view text) noexcept;

    const AssetImporterSemantics& GetAssetImporterSemantics(
        AssetImporterKind kind) noexcept;
    const AssetImporterSemantics* FindAssetImporterSemantics(
        std::string_view importerId) noexcept;
    const AssetImporterSemantics* FindDefaultAssetImporterSemantics(
        AssetType assetType) noexcept;
    const AssetImporterSemantics* ResolveAssetSourceSemantics(
        const std::filesystem::path& sourcePath);

    AssetType ClassifyAssetTypeFromPath(
        const std::filesystem::path& path);
    bool IsAssetSourceForImporter(
        const std::filesystem::path& sourcePath,
        AssetImporterKind kind);

    AssetMeta MakeBaseAssetMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid,
        AssetImporterKind importerKind);

    bool IsAssetCompanionSource(const std::filesystem::path& path);
    bool IsSourceOnlyAssetDependencyRole(std::string_view role) noexcept;

} // namespace HIKARI::ASSETS::SEMANTICS
