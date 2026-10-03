#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <string>

#include "Assets/HIKARI_AssetMeta.h"

namespace HIKARI::ASSETS::SEMANTICS {

inline constexpr std::string_view kSourceMetaSuffix = ".hikari.asset.json";
std::filesystem::path MakeSourceMetaPath(const std::filesystem::path& sourceMetaRoot,
                                         const std::filesystem::path& relativeSource);
std::filesystem::path MakeArtifactManifestPath(const std::filesystem::path& libraryRoot, const std::string& guid);

std::string MakeAssetFileStem(std::string value, std::string_view fallback);

enum class AssetImporterKind : uint8_t {
    SkyCubemap,
    Texture,
    Sprite,
    SpriteMaterial,
    Model,
    Scene,
    Prefab,
    Sequence,
    AnimationStateMachine,
    Dialogue,
    Material,
    ModelMaterialBinding,
    VfxEffect,
    Shader,
    GameplayCamera,
    Font,
    TextStyle,
    UiAnimation,
    PostEffect,
    PostProfile,
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

const AssetImporterSemantics& GetAssetImporterSemantics(AssetImporterKind kind) noexcept;
const AssetImporterSemantics* FindDefaultAssetImporterSemantics(AssetType assetType) noexcept;
const AssetImporterSemantics* ResolveAssetSourceSemantics(const std::filesystem::path& sourcePath);

AssetType ClassifyAssetTypeFromPath(const std::filesystem::path& path);
bool IsAssetSourceForImporter(const std::filesystem::path& sourcePath, AssetImporterKind kind);

AssetMeta MakeBaseAssetMeta(const std::filesystem::path& sourcePath, const AssetGuid& guid,
                            AssetImporterKind importerKind);

bool IsAssetCompanionSource(const std::filesystem::path& path);
bool IsSourceOnlyAssetDependencyRole(std::string_view role) noexcept;

} // namespace HIKARI::ASSETS::SEMANTICS
