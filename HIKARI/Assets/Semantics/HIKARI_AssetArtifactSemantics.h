#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetRecord.h"

namespace HIKARI::ASSETS::SEMANTICS {

    enum class AssetArtifactKind : uint8_t {
        MainTexture,
        DebugTextureDds,
        MainModel,
        ClusteredGeometry,
        CollisionGeometry,
        Material,
        SkyCubemap,
        IblIrradiance,
        IblPrefiltered,
        BrdfLut,
        Count,
    };

    struct AssetArtifactSemantics {
        AssetArtifactKind kind = AssetArtifactKind::MainTexture;
        AssetType ownerType = AssetType::Unknown;
        CookedAssetFormat format = CookedAssetFormat::Unknown;
        std::string_view role{};
        bool primary = false;
    };

    const AssetArtifactSemantics& GetAssetArtifactSemantics(
        AssetArtifactKind kind) noexcept;
    std::string_view ToString(CookedAssetFormat format) noexcept;
    CookedAssetFormat ParseCookedAssetFormat(
        std::string_view text) noexcept;
    std::string_view GetCookedAssetExtension(
        CookedAssetFormat format) noexcept;
    CookedAssetFormat ClassifyCookedAssetFormat(
        const std::filesystem::path& path);

    AssetArtifactDesc MakeAssetArtifact(
        AssetArtifactKind kind,
        std::string path);

    bool MatchesAssetArtifact(
        const AssetArtifactDesc& artifact,
        AssetArtifactKind kind,
        bool acceptCompatible = false) noexcept;
    const AssetArtifactDesc* FindAssetArtifact(
        const AssetRecord& record,
        AssetArtifactKind kind) noexcept;
    const AssetArtifactDesc* FindCompatibleAssetArtifact(
        const AssetRecord& record,
        AssetArtifactKind kind) noexcept;
    std::string FindAssetArtifactPath(
        const AssetRecord& record,
        AssetArtifactKind kind,
        bool acceptLegacyRoleOnly = false);
    bool HasAssetArtifact(
        const AssetRecord& record,
        AssetArtifactKind kind) noexcept;

} // namespace HIKARI::ASSETS::SEMANTICS
