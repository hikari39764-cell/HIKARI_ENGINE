#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"

#include <array>
#include <string>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::SEMANTICS {

    namespace {

        constexpr std::array<AssetImporterSemantics,
            static_cast<size_t>(AssetImporterKind::Count)> kImporterSemantics{ {
            {
                AssetImporterKind::SkyCubemap,
                AssetType::Sky,
                "SkyCubemapImporter",
                1u,
                ".dds under an Assets/Skies or Assets/Sky directory",
            },
            {
                AssetImporterKind::Texture,
                AssetType::Texture,
                "TextureImporter",
                5u,
                ".png, .jpg, .jpeg, .tga, .bmp, .dds, .hdr",
            },
            {
                AssetImporterKind::Model,
                AssetType::Model,
                "ModelImporter",
                34u,
                ".gltf, .obj, .fbx",
            },
            {
                AssetImporterKind::Scene,
                AssetType::Scene,
                "SceneAssetImporter",
                1u,
                ".hscene, .scene.json, or JSON under Assets/Scenes",
            },
            {
                AssetImporterKind::Sequence,
                AssetType::Sequence,
                "SequenceAssetImporter",
                1u,
                ".hsequence",
            },
            {
                AssetImporterKind::AnimationStateMachine,
                AssetType::AnimationStateMachine,
                "AnimationStateMachineAssetImporter",
                2u,
                ".hanimsm",
            },
            {
                AssetImporterKind::Material,
                AssetType::Material,
                "MaterialImporter",
                4u,
                ".material.json",
            },
            {
                AssetImporterKind::VfxEffect,
                AssetType::VfxEffect,
                "VfxAssetImporter",
                1u,
                ".efk, .efkefc",
            },
        } };

        bool EndsWith(std::string_view text, std::string_view suffix) noexcept {
            return text.size() >= suffix.size() &&
                text.substr(text.size() - suffix.size()) == suffix;
        }

        bool IsSkyDirectoryPath(const std::filesystem::path& path) {
            for (const std::filesystem::path& part : path) {
                const std::string name = TEXT::ToLowerAsciiCopy(part.string());
                if (name == "skies" || name == "sky") {
                    return true;
                }
            }
            return false;
        }

        bool IsTextureExtension(std::string_view extension) noexcept {
            return extension == ".png" ||
                extension == ".jpg" ||
                extension == ".jpeg" ||
                extension == ".tga" ||
                extension == ".bmp" ||
                extension == ".dds" ||
                extension == ".hdr";
        }

        bool IsModelExtension(std::string_view extension) noexcept {
            return extension == ".gltf" ||
                extension == ".obj" ||
                extension == ".fbx";
        }

        bool IsScenePath(
            const std::string& lowerPath,
            const std::string& lowerFilename,
            std::string_view extension) noexcept {

            return extension == ".hscene" ||
                EndsWith(lowerFilename, ".scene.json") ||
                (extension == ".json" &&
                    lowerPath.find("assets/scenes/") != std::string::npos);
        }

    } // namespace

    std::string_view ToString(AssetType type) noexcept {
        switch (type) {
        case AssetType::Model: return "Model";
        case AssetType::Scene: return "Scene";
        case AssetType::Sky: return "Sky";
        case AssetType::Texture: return "Texture";
        case AssetType::Material: return "Material";
        case AssetType::Animation: return "Animation";
        case AssetType::Particle: return "Particle";
        case AssetType::VfxEffect: return "VfxEffect";
        case AssetType::Sequence: return "Sequence";
        case AssetType::AnimationStateMachine: return "AnimationStateMachine";
        case AssetType::Unknown:
        default: return "Unknown";
        }
    }

    AssetType ParseAssetType(std::string_view text) noexcept {
        if (text == "Model") return AssetType::Model;
        if (text == "Scene") return AssetType::Scene;
        if (text == "Sky") return AssetType::Sky;
        if (text == "Texture") return AssetType::Texture;
        if (text == "Material") return AssetType::Material;
        if (text == "Animation") return AssetType::Animation;
        if (text == "Particle") return AssetType::Particle;
        if (text == "VfxEffect" || text == "Vfx") return AssetType::VfxEffect;
        if (text == "Sequence") return AssetType::Sequence;
        if (text == "AnimationStateMachine") {
            return AssetType::AnimationStateMachine;
        }
        return AssetType::Unknown;
    }

    const AssetImporterSemantics& GetAssetImporterSemantics(
        AssetImporterKind kind) noexcept {

        const size_t index = static_cast<size_t>(kind);
        if (index < kImporterSemantics.size()) {
            return kImporterSemantics[index];
        }
        return kImporterSemantics[static_cast<size_t>(
            AssetImporterKind::Texture)];
    }

    const AssetImporterSemantics* FindAssetImporterSemantics(
        std::string_view importerId) noexcept {

        for (const AssetImporterSemantics& semantics : kImporterSemantics) {
            if (semantics.importerId == importerId) {
                return &semantics;
            }
        }
        return nullptr;
    }

    const AssetImporterSemantics* FindDefaultAssetImporterSemantics(
        AssetType assetType) noexcept {

        for (const AssetImporterSemantics& semantics : kImporterSemantics) {
            if (semantics.assetType == assetType) {
                return &semantics;
            }
        }
        return nullptr;
    }

    const AssetImporterSemantics* ResolveAssetSourceSemantics(
        const std::filesystem::path& sourcePath) {

        const std::string extension =
            TEXT::ToLowerAsciiCopy(sourcePath.extension().string());
        const std::string filename =
            TEXT::ToLowerAsciiCopy(sourcePath.filename().string());
        const std::string genericPath =
            TEXT::ToLowerAsciiCopy(sourcePath.generic_string());

        AssetImporterKind kind = AssetImporterKind::Count;
        if (extension == ".dds" && IsSkyDirectoryPath(sourcePath)) {
            kind = AssetImporterKind::SkyCubemap;
        } else if (IsTextureExtension(extension)) {
            kind = AssetImporterKind::Texture;
        } else if (IsModelExtension(extension)) {
            kind = AssetImporterKind::Model;
        } else if (IsScenePath(genericPath, filename, extension)) {
            kind = AssetImporterKind::Scene;
        } else if (extension == ".hsequence") {
            kind = AssetImporterKind::Sequence;
        } else if (extension == ".hanimsm") {
            kind = AssetImporterKind::AnimationStateMachine;
        } else if (EndsWith(genericPath, ".material.json")) {
            kind = AssetImporterKind::Material;
        } else if (extension == ".efk" || extension == ".efkefc") {
            kind = AssetImporterKind::VfxEffect;
        }

        return kind == AssetImporterKind::Count
            ? nullptr
            : &GetAssetImporterSemantics(kind);
    }

    AssetType ClassifyAssetTypeFromPath(
        const std::filesystem::path& path) {

        if (const AssetImporterSemantics* semantics =
            ResolveAssetSourceSemantics(path)) {
            return semantics->assetType;
        }

        const std::string extension =
            TEXT::ToLowerAsciiCopy(path.extension().string());
        return extension == ".hmat"
            ? AssetType::Material
            : AssetType::Unknown;
    }

    bool IsAssetSourceForImporter(
        const std::filesystem::path& sourcePath,
        AssetImporterKind kind) {

        const AssetImporterSemantics* semantics =
            ResolveAssetSourceSemantics(sourcePath);
        return semantics != nullptr && semantics->kind == kind;
    }

    AssetMeta MakeBaseAssetMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid,
        AssetImporterKind importerKind) {

        const AssetImporterSemantics& semantics =
            GetAssetImporterSemantics(importerKind);

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = semantics.assetType;
        meta.importerId = semantics.importerId;
        meta.importerVersion = semantics.importerVersion;
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        return meta;
    }

    bool IsAssetCompanionSource(const std::filesystem::path& path) {
        const std::string extension =
            TEXT::ToLowerAsciiCopy(path.extension().string());
        return extension == ".bin" || extension == ".mtl";
    }

    bool IsSourceOnlyAssetDependencyRole(std::string_view role) noexcept {
        return role == "SourceBuffer" || role == "SourceCompanion";
    }

} // namespace HIKARI::ASSETS::SEMANTICS
