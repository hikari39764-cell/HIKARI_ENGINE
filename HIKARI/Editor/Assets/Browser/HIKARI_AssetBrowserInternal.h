#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserPanel.h"
#include "Editor/HIKARI_EditorContext.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::ASSET_BROWSER {

const char *ToAssetTypeText(AssetType type);

std::filesystem::path
MakeUniqueFolderPath(const std::filesystem::path &parentDirectory);

void SelectRecord(const AssetRecord &record, EditorSelection &selection);

std::string FirstArtifactPath(const AssetRecord &record);

std::filesystem::path
MakeUniqueCompoundSuffixFilePath(const std::filesystem::path &absoluteDirectory,
                                 const std::string &baseName,
                                 const std::string &compoundSuffix);

std::string SanitizeFileToken(const std::string &raw,
                              const std::string &fallback);

std::string GetSceneAssetBaseName(const std::filesystem::path &path);

bool IsScenesDirectoryPath(const std::filesystem::path &path);

bool IsMaterialsDirectoryPath(const std::filesystem::path &path);

bool CreateDefaultMaterialAsset(AssetDatabase &assetDatabase,
                                const std::filesystem::path &currentDirectory,
                                std::filesystem::path &outRelativePath,
                                std::string &outError);

bool CreateEmptySceneAsset(AssetDatabase &assetDatabase,
                           const std::filesystem::path &currentDirectory,
                           std::filesystem::path &outRelativePath,
                           std::string &outError);

bool CreateFolderFromBrowser(AssetDatabase &assetDatabase,
                             std::filesystem::path &currentDirectory,
                             std::string &lastOperationMessage);

bool CreateSceneFromBrowser(AssetDatabase &assetDatabase,
                            std::filesystem::path &currentDirectory,
                            EditorSelection &selection,
                            std::string &lastOperationMessage);

bool CreateMaterialFromBrowser(AssetDatabase &assetDatabase,
                               std::filesystem::path &currentDirectory,
                               EditorSelection &selection,
                               std::string &lastOperationMessage);

bool EndsWithCaseInsensitive(const std::string &value, std::string_view suffix);

bool IsAssetsRootPath(const std::filesystem::path &path);

bool IsSupportedImportSource(const std::filesystem::path &path);

std::filesystem::path
SuggestedTargetDirectory(const std::filesystem::path &currentDirectory,
                         const std::filesystem::path &sourcePath);

bool IsPathInside(const std::filesystem::path &path,
                  const std::filesystem::path &directory);

std::filesystem::path
MakeUniqueFilePath(const std::filesystem::path &absolutePath);

bool CopySourceFileIntoProject(const AssetDatabase &assetDatabase,
                               const std::filesystem::path &sourceFile,
                               const std::filesystem::path &targetRelativePath,
                               bool preserveCompanionName,
                               std::filesystem::path &outRelativePath,
                               std::string &outError);

void CollectDroppedFiles(
    const std::filesystem::path &droppedPath,
    const std::filesystem::path &currentDirectory,
    const AssetDatabase &assetDatabase,
    std::vector<std::filesystem::path> &outProjectRelativeFiles,
    int &copiedCompanionCount, int &skippedCount, std::string &lastError);

void ProcessDroppedFiles(AssetDatabase &assetDatabase,
                         const std::filesystem::path &currentDirectory,
                         EditorSelection &selection,
                         std::string &lastOperationMessage);

AssetType TypeFromFilterIndex(int index);

bool MatchesTypeFilter(const AssetRecord &record, int typeFilter);

bool MatchesStateFilter(const AssetRecord &record, int stateFilter);

bool MatchesSearch(const AssetRecord &record, const char *searchText);

nlohmann::json ReadImportSettings(const AssetRecord &record);

bool MatchesScope(const AssetRecord &record,
                  const AssetUsageSummary *usageSummary,
                  AssetBrowserScope scope);

const char *ToScopeTitle(AssetBrowserScope scope);

const char *ToUsageBadge(const AssetRecord &record,
                         const AssetUsageSummary *usageSummary);

const char *ToCookedBadge(const AssetRecord &record);

const char *ToCompactTypeBadge(AssetType type);

const char *ToCompactStateBadge(AssetImportState state);

const char *ToCompactUsageBadge(const AssetRecord &record,
                                const AssetUsageSummary *usageSummary);

const char *ToCompactCookedBadge(const AssetRecord &record);

bool IsSameFilePath(const std::filesystem::path &lhs,
                    const std::filesystem::path &rhs);

void LogSceneAssetInfo(const std::string &message);

void LogSceneAssetWarn(const std::string &message);

bool MoveFileSafe(const std::filesystem::path &from,
                  const std::filesystem::path &to, std::string &outError);

void MoveFileBackBestEffort(const std::filesystem::path &from,
                            const std::filesystem::path &to);

bool UpdateSceneJsonSceneName(const std::filesystem::path &scenePath,
                              std::string_view sceneName,
                              std::string &outError);

bool UpdateSceneMetaAfterMove(AssetDatabase &assetDatabase,
                              const AssetRecord &oldRecord,
                              const std::filesystem::path &relativeSource,
                              const std::filesystem::path &metaPath,
                              std::string_view displayName,
                              std::string &outError);

bool DuplicateSceneAsset(AssetDatabase &assetDatabase,
                         const AssetRecord &record,
                         std::filesystem::path &outRelativePath,
                         std::string &outError);

bool RenameSceneAsset(AssetDatabase &assetDatabase, const AssetRecord &record,
                      std::string_view newName,
                      std::filesystem::path &outRelativePath,
                      std::string &outError);

std::filesystem::path
MakeSceneTrashDirectory(const AssetDatabase &assetDatabase);

bool DeleteSceneAssetToTrash(AssetDatabase &assetDatabase,
                             const AssetRecord &record,
                             bool &outClearedStartupScene,
                             std::string &outError);

#if defined(HIKARI_WITH_EDITOR)
ImVec4 StateColor(AssetImportState state);

std::string BuildGridCardLabel(std::string_view text, float maxWidth);

void DrawDirectoryBreadcrumbs(std::filesystem::path &currentDirectory);

void DrawRecordTooltip(const AssetRecord &record);

void DrawCompactBadge(const char *label, const ImVec4 &color);

ImVec4 TypeBadgeColor(AssetType type);

ImVec4 UsageBadgeColor(const char *label);

ImVec4 CookedBadgeColor(const char *label);

void HandleRecordActivated(const AssetRecord &record,
                           std::string &lastOperationMessage,
                           std::string &activatedSceneGuid,
                           std::string &activatedSequenceGuid,
                           std::string &activatedAnimationStateMachineGuid,
                           std::string &activatedModelCollisionGuid);

std::string SceneBadges(const AssetRecord &record,
                        const AssetBrowserContext *context);

std::string DisplayNameWithSceneBadges(const AssetRecord &record,
                                       const AssetBrowserContext *context);

std::string SceneDisplayNameByGuid(const AssetDatabase &assetDatabase,
                                   const AssetGuid &guid);

void QueueRenameSceneAsset(const AssetRecord &record,
                           std::string &renameSceneGuid,
                           std::array<char, 128> &renameSceneNameBuffer);

void QueueDeleteSceneAsset(const AssetRecord &record,
                           std::string &deleteSceneGuid);

void DrawAssetDragSource(const AssetRecord &record);

void DrawRecordContextMenu(
    AssetDatabase &assetDatabase, const AssetRecord &record,
    EditorSelection &selection, std::string &lastOperationMessage,
    const AssetBrowserContext *context, std::string &activatedSceneGuid,
    std::string &activatedSequenceGuid,
    std::string &activatedAnimationStateMachineGuid,
    std::string &activatedModelCollisionGuid, std::string &saveSceneAsGuid,
    std::string &refreshRuntimeAssetGuid,
    std::string &reimportAndRefreshRuntimeAssetGuid,
    std::string &pendingReimportAndRefreshRuntimeAssetGuid,
    std::string &renameSceneGuid, std::string &deleteSceneGuid,
    std::array<char, 128> &renameSceneNameBuffer);

void DrawRecordList(
    AssetDatabase &assetDatabase,
    const std::vector<const AssetRecord *> &records, EditorSelection &selection,
    const AssetUsageSummary *usageSummary, std::string &lastOperationMessage,
    const AssetBrowserContext *context, std::string &activatedSceneGuid,
    std::string &activatedSequenceGuid,
    std::string &activatedAnimationStateMachineGuid,
    std::string &activatedModelCollisionGuid, std::string &saveSceneAsGuid,
    std::string &refreshRuntimeAssetGuid,
    std::string &reimportAndRefreshRuntimeAssetGuid,
    std::string &pendingReimportAndRefreshRuntimeAssetGuid,
    std::string &renameSceneGuid, std::string &deleteSceneGuid,
    std::array<char, 128> &renameSceneNameBuffer);

void DrawRecordCompactRows(
    AssetDatabase &assetDatabase,
    const std::vector<const AssetRecord *> &records, EditorSelection &selection,
    const AssetUsageSummary *usageSummary, std::string &lastOperationMessage,
    const AssetBrowserContext *context, std::string &activatedSceneGuid,
    std::string &activatedSequenceGuid,
    std::string &activatedAnimationStateMachineGuid,
    std::string &activatedModelCollisionGuid, std::string &saveSceneAsGuid,
    std::string &refreshRuntimeAssetGuid,
    std::string &reimportAndRefreshRuntimeAssetGuid,
    std::string &pendingReimportAndRefreshRuntimeAssetGuid,
    std::string &renameSceneGuid, std::string &deleteSceneGuid,
    std::array<char, 128> &renameSceneNameBuffer);

void DrawRecordGrid(
    AssetDatabase &assetDatabase,
    const std::vector<const AssetRecord *> &records, EditorSelection &selection,
    const AssetUsageSummary *usageSummary, std::string &lastOperationMessage,
    const AssetBrowserContext *context, std::string &activatedSceneGuid,
    std::string &activatedSequenceGuid,
    std::string &activatedAnimationStateMachineGuid,
    std::string &activatedModelCollisionGuid, std::string &saveSceneAsGuid,
    std::string &refreshRuntimeAssetGuid,
    std::string &reimportAndRefreshRuntimeAssetGuid,
    std::string &pendingReimportAndRefreshRuntimeAssetGuid,
    std::string &renameSceneGuid, std::string &deleteSceneGuid,
    std::array<char, 128> &renameSceneNameBuffer);

void DrawSceneAssetModals(AssetDatabase &assetDatabase,
                          EditorSelection &selection,
                          std::string &lastOperationMessage,
                          const AssetBrowserContext *context,
                          std::string &renameSceneGuid,
                          std::string &deleteSceneGuid,
                          std::array<char, 128> &renameSceneNameBuffer);
#endif

} // namespace HIKARI::EDITOR::ASSET_BROWSER
