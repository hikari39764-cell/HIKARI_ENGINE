#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorAssetIcons.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

#include "imgui.h"

#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"

namespace HIKARI::EDITOR::ASSET_BROWSER {

void HandleRecordActivated(const AssetRecord& record, std::string& lastOperationMessage,
                           std::optional<AssetWorkspaceActivation>& activatedAsset) {
    if (!record.guid.IsValid()) {
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.assetSelected") + record.displayName;
        return;
    }
    if (record.type == AssetType::TextStyle || record.type == AssetType::UiAnimation) {
        activatedAsset = AssetWorkspaceActivation{record.type, record.guid};
        return;
    }
    if (record.type == AssetType::Font) {
        lastOperationMessage = TEXT::Localize("textstyle.fontImported");
        return;
    }
    if (record.type == AssetType::Scene) {
        activatedAsset = AssetWorkspaceActivation{record.type, record.guid};
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.sceneOpenRequested") + record.displayName;
        LogSceneAssetInfo("open requested: " + record.sourcePath.generic_string());
        return;
    }
    if (record.type == AssetType::Sequence) {
        activatedAsset = AssetWorkspaceActivation{record.type, record.guid};
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.sequenceOpenRequested") + record.displayName;
        return;
    }
    if (record.type == AssetType::AnimationStateMachine) {
        activatedAsset = AssetWorkspaceActivation{record.type, record.guid};
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.stateMachineRequested") + record.displayName;
        return;
    }
    if (record.type == AssetType::Dialogue) {
        activatedAsset = AssetWorkspaceActivation{record.type, record.guid};
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.dialogueOpenRequested") + record.displayName;
        return;
    }
    if (record.type == AssetType::Model) {
        activatedAsset = AssetWorkspaceActivation{record.type, record.guid};
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.collisionRequested") + record.displayName;
        return;
    }
    if (record.type == AssetType::VfxEffect) {
        activatedAsset = AssetWorkspaceActivation{record.type, record.guid};
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.assetSelected") + record.displayName;
        return;
    }
    lastOperationMessage = TEXT::Localize("ui.assetBrowser.assetSelected") + record.displayName;
}

std::string SceneBadges(const AssetRecord& record, const AssetBrowserContext* context) {
    if (!context || record.type != AssetType::Scene || !record.guid.IsValid()) {
        return {};
    }

    std::string badges{};
    if (context->currentSceneGuid == record.guid) {
        badges += "  " + TEXT::Localize("ui.assetBrowser.badgeCurrent");
        if (context->currentSceneDirty) {
            badges += "*";
        }
        badges += "]";
    }
    if (context->startupSceneGuid == record.guid) {
        badges += "  " + TEXT::Localize("ui.assetBrowser.badgeStartup");
    }
    return badges;
}

std::string DisplayNameWithSceneBadges(const AssetRecord& record, const AssetBrowserContext* context) {
    const std::string displayName =
        record.displayName.empty() ? record.sourcePath.filename().string() : record.displayName;
    return displayName + SceneBadges(record, context);
}

std::string SceneDisplayNameByGuid(const AssetDatabase& assetDatabase, const AssetGuid& guid) {
    if (!guid.IsValid()) {
        return TEXT::Localize("ui.common.none");
    }
    const AssetRecord* record = assetDatabase.FindByGuid(guid);
    if (!record) {
        return TEXT::Localize("ui.assetBrowser.missing");
    }
    return record->displayName.empty() ? record->sourcePath.filename().string() : record->displayName;
}

void QueueRenameSceneAsset(const AssetRecord& record, std::string& renameSceneGuid,
                           std::array<char, 128>& renameSceneNameBuffer) {
    renameSceneGuid = record.guid.value;
    const std::string name = record.displayName.empty() ? GetSceneAssetBaseName(record.sourcePath) : record.displayName;
    renameSceneNameBuffer.fill('\0');
    std::snprintf(renameSceneNameBuffer.data(), renameSceneNameBuffer.size(), "%s", name.c_str());
    ImGui::OpenPopup((TEXT::Localize("ui.assetBrowser.renameTitle") + "###RenameSceneAsset").c_str());
}

void QueueDeleteSceneAsset(const AssetRecord& record, std::string& deleteSceneGuid) {
    deleteSceneGuid = record.guid.value;
    ImGui::OpenPopup((TEXT::Localize("ui.assetBrowser.deleteTitle") + "###DeleteSceneAsset").c_str());
}

void DrawAssetDragSource(const AssetRecord& record) { EDITOR::BeginAssetDragSource(record); }

void DrawRecordContextMenu(AssetDatabase& assetDatabase, const AssetRecord& record, EditorSelection& selection,
                           std::string& lastOperationMessage, const AssetBrowserContext* context,
                           std::optional<AssetWorkspaceActivation>& activatedAsset, std::string& saveSceneAsGuid,
                           std::string& refreshRuntimeAssetGuid, std::string& pendingReimportAndRefreshRuntimeAssetGuid,
                           std::string& renameSceneGuid, std::string& deleteSceneGuid,
                           std::array<char, 128>& renameSceneNameBuffer) {
    if (record.type == AssetType::Scene) {
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.openScene").c_str())) {
            SelectRecord(record, selection);
            HandleRecordActivated(record, lastOperationMessage, activatedAsset);
        }
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.saveCurrentSceneHere").c_str())) {
            saveSceneAsGuid = record.guid.value;
            lastOperationMessage = TEXT::Localize("ui.assetBrowser.sceneSaveRequested") + record.displayName;
            LogSceneAssetInfo("save requested: " + record.sourcePath.generic_string());
        }
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.duplicateScene").c_str())) {
            std::filesystem::path duplicatedPath{};
            std::string error{};
            if (DuplicateSceneAsset(assetDatabase, record, duplicatedPath, error)) {
                if (!assetDatabase.ScanAssets(true)) {
                    lastOperationMessage = TEXT::Localize("ui.assetBrowser.assetDatabaseRefreshFailed");
                } else {
                    if (const AssetRecord* duplicated = assetDatabase.FindByPath(duplicatedPath)) {
                        SelectRecord(*duplicated, selection);
                    }
                    lastOperationMessage = TEXT::Localize("ui.assetBrowser.sceneDuplicated");
                }
            } else {
                lastOperationMessage = error.empty() ? TEXT::Localize("ui.assetBrowser.sceneDuplicateFailed") : error;
            }
            return;
        }
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.renameScene").c_str())) {
            QueueRenameSceneAsset(record, renameSceneGuid, renameSceneNameBuffer);
        }
        const bool isCurrentScene = context && context->currentSceneGuid == record.guid;
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.deleteScene").c_str(), nullptr, false, !isCurrentScene)) {
            QueueDeleteSceneAsset(record, deleteSceneGuid);
        }
        if (isCurrentScene && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("%s", TEXT::Localize("ui.assetBrowser.deleteCurrentSceneTooltip").c_str());
        }
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.setStartupScene").c_str())) {
            ProjectSettingsService settings{};
            settings.Load(assetDatabase.GetProjectRoot());
            if (settings.SetStartupSceneGuid(record.guid) && settings.Save()) {
                lastOperationMessage = TEXT::Localize("ui.assetBrowser.startupSceneSet") + record.displayName;
                LogSceneAssetInfo("set startup scene: " + record.sourcePath.generic_string());
            } else {
                lastOperationMessage = TEXT::Localize("ui.assetBrowser.startupSceneUpdateFailed");
                LogSceneAssetWarn("set startup scene failed: " + record.sourcePath.generic_string());
            }
        }
        ImGui::Separator();
    }

    if (record.type == AssetType::Model) {
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.editCollision").c_str())) {
            SelectRecord(record, selection);
            HandleRecordActivated(record, lastOperationMessage, activatedAsset);
        }
        ImGui::Separator();
    }

    if (record.type == AssetType::Sequence) {
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.openInCinematics").c_str())) {
            SelectRecord(record, selection);
            HandleRecordActivated(record, lastOperationMessage, activatedAsset);
        }
        ImGui::Separator();
    }

    if (record.type == AssetType::AnimationStateMachine) {
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.openStateMachine").c_str())) {
            SelectRecord(record, selection);
            HandleRecordActivated(record, lastOperationMessage, activatedAsset);
        }
        ImGui::Separator();
    }

    if (record.type == AssetType::Dialogue) {
        if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.openDialogue").c_str())) {
            SelectRecord(record, selection);
            HandleRecordActivated(record, lastOperationMessage, activatedAsset);
        }
        ImGui::Separator();
    }

    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.reimport").c_str())) {
        SelectRecord(record, selection);
        const bool queued = assetDatabase.QueueImportAssets({record.guid}, "Reimport " + record.displayName);
        lastOperationMessage = queued ? TEXT::Localize("ui.assetBrowser.reimportQueued")
                                      : TEXT::Localize("ui.assetBrowser.importBatchRunning");
    }
    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.reimportRefreshRuntime").c_str())) {
        SelectRecord(record, selection);
        const bool queued = assetDatabase.QueueImportAssets({record.guid}, "Reimport " + record.displayName);
        if (queued) {
            pendingReimportAndRefreshRuntimeAssetGuid = record.guid.value;
        }
        lastOperationMessage = queued ? TEXT::Localize("ui.assetBrowser.reimportQueuedRefresh")
                                      : TEXT::Localize("ui.assetBrowser.importBatchRunning");
    }
    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.refreshRuntimeOnly").c_str())) {
        SelectRecord(record, selection);
        refreshRuntimeAssetGuid = record.guid.value;
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.runtimeRefreshQueued");
    }
    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.reimportDependencies").c_str())) {
        const bool queued = assetDatabase.QueueImportDependencies(record.guid, false);
        lastOperationMessage = queued ? TEXT::Localize("ui.assetBrowser.dependencyImportQueued")
                                      : TEXT::Localize("ui.assetBrowser.importBatchRunning");
    }
    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.showInExplorer").c_str())) {
        EDITOR::SHELL::ShowFileInExplorer(assetDatabase.ResolveSourcePath(record));
    }
    ImGui::Separator();
    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.copyGuid").c_str())) {
        ImGui::SetClipboardText(record.guid.value.c_str());
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.guidCopied");
    }
    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.copySourcePath").c_str())) {
        const std::string path = record.sourcePath.generic_string();
        ImGui::SetClipboardText(path.c_str());
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.sourcePathCopied");
    }
    const std::string artifactPath = FirstArtifactPath(record);
    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.copyArtifactPath").c_str(), nullptr, false,
                        !artifactPath.empty())) {
        ImGui::SetClipboardText(artifactPath.c_str());
        lastOperationMessage = TEXT::Localize("ui.assetBrowser.artifactPathCopied");
    }
    ImGui::Separator();
    if (ImGui::MenuItem(TEXT::Localize("ui.assetBrowser.regenerateMeta").c_str())) {
        SelectRecord(record, selection);
        const bool ok = assetDatabase.RegenerateMeta(record.sourcePath);
        lastOperationMessage = ok ? TEXT::Localize("ui.assetBrowser.metaRegenerated")
                                  : TEXT::Localize("ui.assetBrowser.metaRegenerationFailed");
    }
}

} // namespace HIKARI::EDITOR::ASSET_BROWSER
