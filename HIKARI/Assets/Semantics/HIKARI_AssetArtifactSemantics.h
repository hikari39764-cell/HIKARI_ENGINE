#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetRecord.h"
#include "RenderContracts/Format/HIKARI_CookedAssetFormat.h"

namespace HIKARI::ASSETS::SEMANTICS {

enum class AssetArtifactKind : uint8_t {
    MainTexture,
    DebugTextureDds,
    MainModel,
    ClusteredGeometry,
    CollisionGeometry,
    ModelMaterialTexture,
    Material,
    SkyCubemap,
    IblIrradiance,
    IblPrefiltered,
    BrdfLut,
    VfxProgram,
    ShaderProgram,
    PrefabRuntime,
    SpriteRuntime,
    SpritePageTexture,
    SpritePageNormalTexture,
    SpriteMaterialRuntime,
    Count,
};

struct AssetArtifactSemantics {
    AssetArtifactKind kind = AssetArtifactKind::MainTexture;
    AssetType ownerType = AssetType::Unknown;
    CookedAssetFormat format = CookedAssetFormat::Unknown;
    std::string_view role{};
    bool primary = false;
};

const AssetArtifactSemantics& GetAssetArtifactSemantics(AssetArtifactKind kind) noexcept;
const AssetArtifactSemantics* FindPrimaryAssetArtifactSemantics(AssetType type) noexcept;

AssetArtifactDesc MakeAssetArtifact(AssetArtifactKind kind, std::string path);

bool MatchesAssetArtifact(const AssetArtifactDesc& artifact, AssetArtifactKind kind,
                          bool acceptCompatible = false) noexcept;
const AssetArtifactDesc* FindAssetArtifact(const AssetRecord& record, AssetArtifactKind kind) noexcept;
const AssetArtifactDesc* FindCompatibleAssetArtifact(const AssetRecord& record, AssetArtifactKind kind) noexcept;
std::string FindAssetArtifactPath(const AssetRecord& record, AssetArtifactKind kind, bool acceptLegacyRoleOnly = false);
bool HasAssetArtifact(const AssetRecord& record, AssetArtifactKind kind) noexcept;

} // namespace HIKARI::ASSETS::SEMANTICS
