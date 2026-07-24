#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"

#include <array>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::SEMANTICS {

    namespace {

        constexpr std::array<AssetArtifactSemantics,
            static_cast<size_t>(AssetArtifactKind::Count)> kArtifactSemantics{ {
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
        } };

    } // namespace

    const AssetArtifactSemantics& GetAssetArtifactSemantics(
        AssetArtifactKind kind) noexcept {

        const size_t index = static_cast<size_t>(kind);
        if (index < kArtifactSemantics.size()) {
            return kArtifactSemantics[index];
        }
        return kArtifactSemantics[0];
    }

    std::string_view ToString(CookedAssetFormat format) noexcept {
        switch (format) {
        case CookedAssetFormat::DDS: return "DDS";
        case CookedAssetFormat::HTEX: return "HTEX";
        case CookedAssetFormat::HMODEL: return "HMODEL";
        case CookedAssetFormat::HMESH: return "HMESH";
        case CookedAssetFormat::HCMESH: return "HCMESH";
        case CookedAssetFormat::HCOLLISION: return "HCOLLISION";
        case CookedAssetFormat::HMAT: return "HMAT";
        case CookedAssetFormat::HSKY: return "HSKY";
        case CookedAssetFormat::HIBL: return "HIBL";
        case CookedAssetFormat::HPAK: return "HPAK";
        case CookedAssetFormat::Unknown:
        default: return "Unknown";
        }
    }

    CookedAssetFormat ParseCookedAssetFormat(
        std::string_view text) noexcept {

        constexpr CookedAssetFormat formats[] = {
            CookedAssetFormat::DDS,
            CookedAssetFormat::HTEX,
            CookedAssetFormat::HMODEL,
            CookedAssetFormat::HMESH,
            CookedAssetFormat::HCMESH,
            CookedAssetFormat::HCOLLISION,
            CookedAssetFormat::HMAT,
            CookedAssetFormat::HSKY,
            CookedAssetFormat::HIBL,
            CookedAssetFormat::HPAK,
        };
        for (CookedAssetFormat format : formats) {
            if (text == ToString(format)) {
                return format;
            }
        }
        return CookedAssetFormat::Unknown;
    }

    std::string_view GetCookedAssetExtension(
        CookedAssetFormat format) noexcept {

        switch (format) {
        case CookedAssetFormat::DDS: return ".dds";
        case CookedAssetFormat::HTEX: return ".htex";
        case CookedAssetFormat::HMODEL: return ".hmodel";
        case CookedAssetFormat::HMESH: return ".hmesh";
        case CookedAssetFormat::HCMESH: return ".hcmesh";
        case CookedAssetFormat::HCOLLISION: return ".hcollision";
        case CookedAssetFormat::HMAT: return ".hmat";
        case CookedAssetFormat::HSKY: return ".hsky";
        case CookedAssetFormat::HIBL: return ".hibl";
        case CookedAssetFormat::HPAK: return ".hpak";
        case CookedAssetFormat::Unknown:
        default: return {};
        }
    }

    CookedAssetFormat ClassifyCookedAssetFormat(
        const std::filesystem::path& path) {

        const std::string extension =
            TEXT::ToLowerAsciiCopy(path.extension().string());
        constexpr CookedAssetFormat formats[] = {
            CookedAssetFormat::DDS,
            CookedAssetFormat::HTEX,
            CookedAssetFormat::HMODEL,
            CookedAssetFormat::HMESH,
            CookedAssetFormat::HCMESH,
            CookedAssetFormat::HCOLLISION,
            CookedAssetFormat::HMAT,
            CookedAssetFormat::HSKY,
            CookedAssetFormat::HIBL,
            CookedAssetFormat::HPAK,
        };
        for (CookedAssetFormat format : formats) {
            if (extension == GetCookedAssetExtension(format)) {
                return format;
            }
        }
        return CookedAssetFormat::Unknown;
    }

    AssetArtifactDesc MakeAssetArtifact(
        AssetArtifactKind kind,
        std::string path) {

        const AssetArtifactSemantics& semantics =
            GetAssetArtifactSemantics(kind);
        return AssetArtifactDesc{
            std::string(semantics.role),
            std::move(path),
            std::string(ToString(semantics.format)),
        };
    }

    bool MatchesAssetArtifact(
        const AssetArtifactDesc& artifact,
        AssetArtifactKind kind,
        bool acceptCompatible) noexcept {

        const AssetArtifactSemantics& semantics =
            GetAssetArtifactSemantics(kind);
        const std::string_view format = ToString(semantics.format);
        if (artifact.role == semantics.role &&
            artifact.format == format) {
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
        return semantics.format != CookedAssetFormat::DDS &&
            semantics.format != CookedAssetFormat::Unknown &&
            artifact.format == format;
    }

    const AssetArtifactDesc* FindAssetArtifact(
        const AssetRecord& record,
        AssetArtifactKind kind) noexcept {

        for (const AssetArtifactDesc& artifact :
            record.artifactManifest.artifacts) {
            if (MatchesAssetArtifact(artifact, kind) &&
                !artifact.path.empty()) {
                return &artifact;
            }
        }
        return nullptr;
    }

    const AssetArtifactDesc* FindCompatibleAssetArtifact(
        const AssetRecord& record,
        AssetArtifactKind kind) noexcept {

        for (const AssetArtifactDesc& artifact :
            record.artifactManifest.artifacts) {
            if (MatchesAssetArtifact(artifact, kind, true) &&
                !artifact.path.empty()) {
                return &artifact;
            }
        }
        return nullptr;
    }

    std::string FindAssetArtifactPath(
        const AssetRecord& record,
        AssetArtifactKind kind,
        bool acceptLegacyRoleOnly) {

        const AssetArtifactDesc* artifact = acceptLegacyRoleOnly
            ? FindCompatibleAssetArtifact(record, kind)
            : FindAssetArtifact(record, kind);
        return artifact != nullptr ? artifact->path : std::string{};
    }

    bool HasAssetArtifact(
        const AssetRecord& record,
        AssetArtifactKind kind) noexcept {

        return FindAssetArtifact(record, kind) != nullptr;
    }

} // namespace HIKARI::ASSETS::SEMANTICS
