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

// 分区場景の格子ファイルを置く、場景ファイルの隣のフォルダ（Island.scene.json → Island.cells）。中身は場景という
// 一つの資産に属し、資産として走査しない。
inline constexpr std::string_view kSceneCellDirectorySuffix = ".cells";
std::filesystem::path MakeSceneCellDirectoryPath(const std::filesystem::path& scenePath);
bool IsSceneCellDirectoryName(std::string_view lowerName) noexcept;

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
    PostEffect,
    PostProfile,
    Environment,
    Weather,
    RenderStyle,
    ColorLut,
    LightProfile,
    Audio,
    ModuleAsset,
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

// モジュールの清単が宣言する資産の種類。JSON の原文のまま配り、参照は値の中の GUID から拾う。
struct ModuleAssetTypeDesc {
    std::string typeId{};
    // 小文字、先頭の点を含む（".hscatter"）。
    std::string extension{};
    std::string displayName{};
};

// 登記表はプロセスで一つ。資産庫の初期化が項目の清単から丸ごと作り直す（DLL を読まない工程も同じ表になる）。
// 引擎の資産と重なる拡張子や、種類どうしの衝突は登記せず、理由を outErrors に足す。
void ReplaceModuleAssetTypes(std::vector<ModuleAssetTypeDesc> types, std::vector<std::string>& outErrors);
const ModuleAssetTypeDesc* FindModuleAssetTypeForPath(const std::filesystem::path& sourcePath);
const ModuleAssetTypeDesc* FindModuleAssetType(std::string_view typeId);
// 資産（種類と元ファイル）が求める種類に合うか。moduleAssetTypeId が空でなければ、モジュールが登記したその種類に限る。
// 資産の選択・拖放・検証は同じ判定を使う。
bool MatchesAssetType(AssetType recordType, const std::filesystem::path& sourcePath, AssetType requiredType,
                      std::string_view moduleAssetTypeId = {});

bool IsAssetCompanionSource(const std::filesystem::path& path);
bool IsSourceOnlyAssetDependencyRole(std::string_view role) noexcept;

} // namespace HIKARI::ASSETS::SEMANTICS
