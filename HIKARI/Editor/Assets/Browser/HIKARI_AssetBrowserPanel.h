#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserCache.h"

namespace HIKARI {

class AssetDatabase;
struct AssetUsageSummary;
struct EditorSelection;
struct SceneSystemData;
enum class AssetType;

struct AssetWorkspaceActivation {
    AssetWorkspaceActivation(AssetType assetType, const AssetGuid& assetGuid) : type(assetType), guid(assetGuid) {}

    AssetType type;
    AssetGuid guid;
};

enum class AssetBrowserScope {
    Project,
    CurrentScene,
    UnusedInScene,
    Broken,
    Textures,
    Models,
    Scenes,
    Materials,
    Skies,
    Vfx,
    Sequences,
};

struct AssetBrowserContext {
    AssetGuid currentSceneGuid{};
    AssetGuid startupSceneGuid{};
    bool currentSceneDirty = false;
};

class AssetBrowserPanel {
public:
    void Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const;
    void DrawContents(AssetDatabase& assetDatabase, EditorSelection& selection) const;
    void DrawContents(AssetDatabase& assetDatabase, EditorSelection& selection, const AssetUsageSummary* usageSummary,
                      AssetBrowserScope scope, const AssetBrowserContext* context = nullptr,
                      uint64_t usageRevision = 0u) const;
    std::optional<AssetWorkspaceActivation> ConsumeActivatedAsset() const;
    std::string ConsumeSaveSceneAsGuid() const;
    std::string ConsumeRefreshRuntimeAssetGuid() const;
    std::string ConsumeReimportAndRefreshRuntimeAssetGuid() const;
    std::filesystem::path CurrentDirectory() const;
    bool IsRecursiveEnabled() const;

private:
    mutable EDITOR::ASSET_BROWSER::AssetBrowserCatalogCache catalogCache_{};
    mutable std::filesystem::path currentDirectory_{"Assets"};
    mutable std::array<char, 128> searchBuffer_{};
    mutable int typeFilter_ = 0;
    mutable int stateFilter_ = 0;
    mutable int viewMode_ = 2;
    mutable bool recursive_ = false;
    mutable bool filtersExpanded_ = false;
    mutable std::string lastOperationMessage_{};
    mutable std::string observedOperationMessage_{};
    mutable double operationMessageVisibleUntil_ = 0.0;
    mutable std::optional<AssetWorkspaceActivation> activatedAsset_{};
    mutable std::string saveSceneAsGuid_{};
    mutable std::string refreshRuntimeAssetGuid_{};
    mutable std::string reimportAndRefreshRuntimeAssetGuid_{};
    mutable std::string pendingReimportAndRefreshRuntimeAssetGuid_{};
    mutable std::string renameSceneGuid_{};
    mutable std::string deleteSceneGuid_{};
    mutable std::array<char, 128> renameSceneNameBuffer_{};
};

namespace EDITOR::ASSET_BROWSER {
// 新しい場面の資産（既定の床と systems）を作って資産の一覧に載せ、その GUID を返す。directory が Assets/Scenes の下で
// なければ Assets/Scenes に作る。baseName（英数字・空白・_・- 以外は落とす）がファイル名と場面名になり、同じ名前の
// ファイルがあれば番号を付ける。失敗した時は書いたファイルを消して無効な GUID を返し、理由を outError に書く。
AssetGuid CreateSceneAsset(AssetDatabase& assetDatabase, const std::filesystem::path& directory,
                           std::string_view baseName, const std::vector<SceneSystemData>& systems,
                           std::filesystem::path& outRelativePath, std::string& outError);
// 場面のファイル名から .scene.json を除いた名前。
std::string GetSceneAssetBaseName(const std::filesystem::path& path);
// プロジェクト相対の path が Assets/Scenes かその下か（大文字小文字は区別しない）。
bool IsScenesDirectoryPath(const std::filesystem::path& path);
} // namespace EDITOR::ASSET_BROWSER

} // namespace HIKARI
