#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"

#include <array>

namespace HIKARI::ASSETS::SEMANTICS {

namespace {

constexpr std::array<AssetArtifactSemantics, static_cast<size_t>(AssetArtifactKind::Count)> kArtifactSemantics{{
    {
        AssetArtifactKind::MainTexture,
        AssetType::Texture,
        CookedAssetFormat::HTEX,
        "MainTexture",
        true,
    },
    {
        AssetArtifactKind::DebugTextureDds,
        AssetType::Texture,
        CookedAssetFormat::DDS,
        "DebugDDS",
        false,
    },
    {
        AssetArtifactKind::MainModel,
        AssetType::Model,
        CookedAssetFormat::HMODEL,
        "MainModel",
        true,
    },
    {
        AssetArtifactKind::ClusteredGeometry,
        AssetType::Model,
        CookedAssetFormat::HCMESH,
        "ClusteredGeometry",
        false,
    },
    {
        AssetArtifactKind::CollisionGeometry,
        AssetType::Model,
        CookedAssetFormat::HCOLLISION,
        "CollisionGeometry",
        false,
    },
    {
        AssetArtifactKind::ModelMaterialTexture,
        AssetType::Model,
        CookedAssetFormat::DDS,
        "ModelMaterialTexture",
        false,
    },
    {
        AssetArtifactKind::Material,
        AssetType::Material,
        CookedAssetFormat::HMAT,
        "Material",
        true,
    },
    {
        AssetArtifactKind::SkyCubemap,
        AssetType::Sky,
        CookedAssetFormat::DDS,
        "SkyCubemap",
        true,
    },
    {
        AssetArtifactKind::IblIrradiance,
        AssetType::Sky,
        CookedAssetFormat::DDS,
        "IblIrradiance",
        false,
    },
    {
        AssetArtifactKind::IblPrefiltered,
        AssetType::Sky,
        CookedAssetFormat::DDS,
        "IblPrefiltered",
        false,
    },
    {
        AssetArtifactKind::BrdfLut,
        AssetType::Sky,
        CookedAssetFormat::DDS,
        "BrdfLut",
        false,
    },
    {
        AssetArtifactKind::VfxProgram,
        AssetType::VfxEffect,
        CookedAssetFormat::HVFX,
        "VfxProgram",
        true,
    },
    {
        AssetArtifactKind::ShaderProgram,
        AssetType::Shader,
        CookedAssetFormat::HSHADERC,
        "ShaderProgram",
        true,
    },
    {AssetArtifactKind::PrefabRuntime, AssetType::Prefab, CookedAssetFormat::HPREFABC, "PrefabRuntime", true},
    {AssetArtifactKind::SpriteRuntime, AssetType::Sprite, CookedAssetFormat::HSPRITEC, "SpriteRuntime", true},
    {AssetArtifactKind::SpritePageTexture, AssetType::Sprite, CookedAssetFormat::HTEX, "SpritePageTexture", false},
    {AssetArtifactKind::SpritePageNormalTexture, AssetType::Sprite, CookedAssetFormat::HTEX, "SpritePageNormalTexture",
     false},
    {AssetArtifactKind::SpriteMaterialRuntime, AssetType::SpriteMaterial, CookedAssetFormat::HSPRITEMATERIALC,
     "SpriteMaterialRuntime", true},
}};

} // namespace

const AssetArtifactSemantics& GetAssetArtifactSemantics(AssetArtifactKind kind) noexcept {
    const size_t index = static_cast<size_t>(kind);
    if (index < kArtifactSemantics.size()) {
        return kArtifactSemantics[index];
    }
    return kArtifactSemantics[0];
}

const AssetArtifactSemantics* FindPrimaryAssetArtifactSemantics(AssetType type) noexcept {
    for (const auto& entry : kArtifactSemantics) {
        if (entry.ownerType == type && entry.primary)
            return &entry;
    }
    return nullptr;
}

AssetArtifactDesc MakeAssetArtifact(AssetArtifactKind kind, std::string path) {
    const AssetArtifactSemantics& semantics = GetAssetArtifactSemantics(kind);
    return AssetArtifactDesc{
        std::string(semantics.role),
        std::move(path),
        std::string(ToString(semantics.format)),
    };
}

bool MatchesAssetArtifact(const AssetArtifactDesc& artifact, AssetArtifactKind kind, bool acceptCompatible) noexcept {
    const AssetArtifactSemantics& semantics = GetAssetArtifactSemantics(kind);
    const std::string_view format = ToString(semantics.format);
    if (artifact.role == semantics.role && artifact.format == format) {
        return true;
    }
    if (!acceptCompatible) {
        return false;
    }
    if (artifact.role == semantics.role) {
        return true;
    }

    // DDS is shared by several sky and texture artifact roles, so a
    // format-only legacy match cannot identify one of those roles safely.
    return semantics.format != CookedAssetFormat::DDS && semantics.format != CookedAssetFormat::Unknown &&
           artifact.format == format;
}

const AssetArtifactDesc* FindAssetArtifact(const AssetRecord& record, AssetArtifactKind kind) noexcept {
    for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
        if (MatchesAssetArtifact(artifact, kind) && !artifact.path.empty()) {
            return &artifact;
        }
    }
    return nullptr;
}

const AssetArtifactDesc* FindCompatibleAssetArtifact(const AssetRecord& record, AssetArtifactKind kind) noexcept {
    for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
        if (MatchesAssetArtifact(artifact, kind, true) && !artifact.path.empty()) {
            return &artifact;
        }
    }
    return nullptr;
}

std::string FindAssetArtifactPath(const AssetRecord& record, AssetArtifactKind kind, bool acceptLegacyRoleOnly) {
    const AssetArtifactDesc* artifact =
        acceptLegacyRoleOnly ? FindCompatibleAssetArtifact(record, kind) : FindAssetArtifact(record, kind);
    return artifact != nullptr ? artifact->path : std::string{};
}

bool HasAssetArtifact(const AssetRecord& record, AssetArtifactKind kind) noexcept {
    return FindAssetArtifact(record, kind) != nullptr;
}

} // namespace HIKARI::ASSETS::SEMANTICS
