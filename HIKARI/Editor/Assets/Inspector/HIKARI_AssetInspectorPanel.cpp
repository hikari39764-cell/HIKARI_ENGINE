#include "HIKARI_AssetInspectorPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetImportInputs.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Editor/Assets/HIKARI_AssetImportSettingsEditor.h"
#include "Editor/Assets/Inspector/HIKARI_AssetInspectorInternal.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"
#include "Editor/Widgets/HIKARI_EditorTexturePreview.h"

#include "imgui.h"

namespace HIKARI {

using namespace EDITOR::ASSET_INSPECTOR;

namespace {

const std::array TextureUsageItems = {
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::Auto).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::BaseColor).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::Normal).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::MetallicRoughness).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::Occlusion).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::Emissive).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::Mask).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::UI).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::SkyCubemap).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::IblIrradiance).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::IblPrefiltered).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureUsage::BrdfLut).data(),
};

const std::array TextureColorSpaceItems = {
    ASSETS::IMPORT_POLICY::ToString(TextureAssetColorSpace::Auto).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureAssetColorSpace::Linear).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureAssetColorSpace::Srgb).data(),
};

const std::array TextureCompressionItems = {
    ASSETS::IMPORT_POLICY::ToString(TextureCompression::Auto).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureCompression::None).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureCompression::BC1).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureCompression::BC3).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureCompression::BC4).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureCompression::BC5).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureCompression::BC6H).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureCompression::BC7).data(),
};

const std::array TextureMipPolicyItems = {
    ASSETS::IMPORT_POLICY::ToString(TextureMipPolicy::Auto).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureMipPolicy::Generate).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureMipPolicy::Preserve).data(),
    ASSETS::IMPORT_POLICY::ToString(TextureMipPolicy::None).data(),
};

int FindItemIndex(const char* const* items, int count, const std::string& value) {
    for (int i = 0; i < count; ++i) {
        if (value == items[i]) {
            return i;
        }
    }
    return 0;
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return {};
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool DrawComboSetting(const char* label, nlohmann::json& settings, const char* key, const char* const* items,
                      int itemCount) {
    int current = FindItemIndex(items, itemCount, settings.value(key, ""));
    if (ImGui::Combo(label, &current, items, itemCount)) {
        settings[key] = items[current];
        return true;
    }
    return false;
}

bool DrawBoolSetting(const char* label, nlohmann::json& settings, const char* key, bool fallback) {
    bool value = settings.value(key, fallback);
    if (ImGui::Checkbox(label, &value)) {
        settings[key] = value;
        return true;
    }
    return false;
}

bool DrawIntSetting(const char* label, nlohmann::json& settings, const char* key, int fallback, int minimum = 0) {
    int value = settings.value(key, fallback);
    if (ImGui::InputInt(label, &value)) {
        settings[key] = (std::max)(minimum, value);
        return true;
    }
    return false;
}

void DrawPathRow(const char* label, const std::filesystem::path& path) {
    ImGui::Text("%s: %s", label, path.generic_string().c_str());
}

} // namespace

void AssetInspectorPanel::RefreshFilePreview(const AssetDatabase& database, const AssetRecord& record) const {
    const auto now = std::chrono::steady_clock::now();
    const std::string key = database.GetProjectRoot().generic_string() + "|" + record.guid.value;
    if (key == filePreviewKey_ && filePreviewRevision_ == database.GetContentRevision() && now < nextFilePreview_)
        return;
    filePreviewKey_ = key;
    filePreviewRevision_ = database.GetContentRevision();
    nextFilePreview_ = now + std::chrono::seconds(1);
    rawMeta_ = ReadTextFile(record.metaPath);
    rawReport_ = ReadTextFile(record.importedDirectory / "import_report.json");
    modelReport_ = nlohmann::json::parse(rawReport_, nullptr, false);
    if (record.type == AssetType::Model)
        modelInfo_ = database.GetClusteredGeometryArtifactInfo(record);
    else
        modelInfo_ = {};
}

void AssetInspectorPanel::Draw(AssetDatabase& assetDatabase, AssetRegistry& assetRegistry,
                               EditorSelection& selection) const {
    if (!pendingRefreshRuntimeMaterialGuid_.empty()) {
        const AssetImportBatchStatus status = assetDatabase.GetQueuedImportStatus();
        if (!status.active && status.completed) {
            if (!status.canceled && status.failed == 0) {
                EDITOR::ClearEditorTexturePreviewCache();
                refreshRuntimeMaterialGuid_ = pendingRefreshRuntimeMaterialGuid_;
            }
            pendingRefreshRuntimeMaterialGuid_.clear();
        }
    }
    if (selection.selectedAssetGuid.empty()) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.noAssetSelected").c_str());
        return;
    }

    AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{selection.selectedAssetGuid});
    if (!record) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.assetUnavailable").c_str());
        return;
    }

    const std::string draftKey = assetDatabase.GetProjectRoot().generic_string() + "/" + record->guid.value;
    auto& displayNameBuffer = displayNameBuffer_;
    const bool selectionChanged = loadedGuid_ != record->guid.value || loadedProject_ != assetDatabase.GetProjectRoot();
    if (selectionChanged) {
        const auto previous = importDrafts_.find(loadedProject_.generic_string() + "/" + loadedGuid_);
        if (previous != importDrafts_.end() && !previous->second.IsDirty())
            importDrafts_.erase(previous);
        loadedGuid_ = record->guid.value;
        loadedProject_ = assetDatabase.GetProjectRoot();
        filePreviewKey_.clear();
        importSettingsStatus_.clear();
        const auto* importer = assetDatabase.GetImporterRegistry().FindById(record->meta.importerId);
        importDefaults_ = importer ? importer->CreateDefaultMeta(record->sourcePath, record->guid) : record->meta;
    }
    auto [draftIt, inserted] = importDrafts_.try_emplace(draftKey);
    auto& draft = draftIt->second;
    if (inserted || (!draft.IsDirty() && (draft.baseline.importSettingsJson != record->meta.importSettingsJson ||
                                          draft.baseline.displayName != record->meta.displayName)))
        draft.Reset(*record);
    if (selectionChanged || !draft.IsDirty())
        CopyToBuffer(draft.edited.displayName, displayNameBuffer.data(), displayNameBuffer.size());
    auto settings = nlohmann::json::parse(draft.edited.importSettingsJson, nullptr, false);
    if (!settings.is_object())
        settings = nlohmann::json::object();
    std::string settingsError;
    const bool settingsValid = ValidateAssetImportSettings(draft.edited, importDefaults_, settingsError);
    bool dirty = false;
    const AssetImportState importState = GetImportState(*record);
    const bool importing = assetDatabase.GetQueuedImportStatus().active;
    ImGui::Text("%s: %s", TEXT::Localize("ui.assetInspector.importStatus").c_str(), ToString(importState));
    if (record->importOutdated && !record->importOutdatedReason.empty())
        ImGui::TextWrapped("%s", record->importOutdatedReason.c_str());
    if (!record->lastImportSucceeded && !record->lastImportMessage.empty())
        ImGui::TextWrapped("%s", record->lastImportMessage.c_str());
    if (draft.IsDirty())
        ImGui::TextWrapped("%s", TEXT::Localize("ui.assetInspector.unappliedDraft").c_str());

    const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetTextLineHeightWithSpacing() * 4.0f;
    ImGui::BeginChild("AssetInspectorContent",
                      ImVec2(0, (std::max)(64.0f, ImGui::GetContentRegionAvail().y - footerHeight)));
    if (ImGui::BeginTabBar("AssetInspectorTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
        if (ImGui::BeginTabItem(TEXT::Localize("ui.assetInspector.tabSummary").c_str())) {
            if (ImGui::InputText(TEXT::Localize("ui.assetInspector.displayName").c_str(), displayNameBuffer.data(),
                                 displayNameBuffer.size())) {
                dirty = true;
            }

            ImGui::Text(TEXT::Localize("ui.assetInspector.guidFormat").c_str(), record->guid.value.c_str());
            ImGui::Text(TEXT::Localize("ui.assetInspector.typeFormat").c_str(),
                        ASSETS::SEMANTICS::ToString(record->type).data());
            DrawPathRow(TEXT::Localize("ui.assetInspector.sourcePath").c_str(), record->sourcePath);
            DrawPathRow(TEXT::Localize("ui.assetInspector.metaPath").c_str(), record->metaPath);
            ImGui::Text(TEXT::Localize("ui.assetInspector.importerFormat").c_str(),
                        record->meta.importerId.empty() ? TEXT::Localize("ui.common.none").c_str()
                                                        : record->meta.importerId.c_str());
            ImGui::Text(TEXT::Localize("ui.assetInspector.importerVersionFormat").c_str(),
                        record->meta.importerVersion);
            DrawPathRow(TEXT::Localize("ui.assetInspector.importedDirectory").c_str(), record->importedDirectory);
            ImGui::Text(TEXT::Localize("ui.assetInspector.stateFormat").c_str(), ToString(importState));
            if (!record->lastImportMessage.empty()) {
                ImGui::TextWrapped(TEXT::Localize("ui.assetInspector.lastImportMessageFormat").c_str(),
                                   record->lastImportMessage.c_str());
            }

            if (ImGui::Button(TEXT::Localize("ui.assetInspector.copyGuid").c_str())) {
                ImGui::SetClipboardText(record->guid.value.c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button(TEXT::Localize("ui.assetInspector.showSource").c_str())) {
                EDITOR::SHELL::ShowFileInExplorer(assetDatabase.ResolveSourcePath(*record));
            }
            ImGui::SameLine();
            if (ImGui::Button(TEXT::Localize("ui.assetInspector.showImportedDirectory").c_str()) &&
                !record->importedDirectory.empty()) {
                EDITOR::SHELL::OpenFolderInExplorer(record->importedDirectory);
            }

            ImGui::BeginDisabled(importing);
            if (ImGui::Button(TEXT::Localize("ui.assetInspector.importDependencies").c_str())) {
                if (!assetDatabase.QueueImportDependencies(record->guid, false))
                    importSettingsStatus_ = TEXT::Localize("ui.assetInspector.queueFailed");
            }
            ImGui::EndDisabled();

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TEXT::Localize("ui.assetInspector.tabImportSettings").c_str())) {
            if (!settingsValid) {
                ImGui::TextWrapped("%s", settingsError.c_str());
            } else if (record->type == AssetType::Texture) {
                dirty = DrawComboSetting(TEXT::Localize("ui.assetInspector.settingUsage").c_str(), settings, "usage",
                                         TextureUsageItems.data(), static_cast<int>(TextureUsageItems.size())) ||
                        dirty;
                dirty = DrawComboSetting(TEXT::Localize("ui.assetInspector.settingColorSpace").c_str(), settings,
                                         "colorSpace", TextureColorSpaceItems.data(),
                                         static_cast<int>(TextureColorSpaceItems.size())) ||
                        dirty;
                dirty = DrawComboSetting(TEXT::Localize("ui.assetInspector.settingCompression").c_str(), settings,
                                         "compression", TextureCompressionItems.data(),
                                         static_cast<int>(TextureCompressionItems.size())) ||
                        dirty;
                dirty = DrawComboSetting(TEXT::Localize("ui.assetInspector.settingMipPolicy").c_str(), settings,
                                         "mipPolicy", TextureMipPolicyItems.data(),
                                         static_cast<int>(TextureMipPolicyItems.size())) ||
                        dirty;
                dirty = DrawIntSetting(TEXT::Localize("ui.assetInspector.settingMaxSize").c_str(), settings, "maxSize",
                                       4096, 1) ||
                        dirty;
                dirty = DrawBoolSetting(TEXT::Localize("ui.assetInspector.settingForcePowerOfTwo").c_str(), settings,
                                        "forcePowerOfTwo", false) ||
                        dirty;
                dirty = DrawBoolSetting(TEXT::Localize("ui.assetInspector.settingAllowResize").c_str(), settings,
                                        "allowResize", false) ||
                        dirty;
            } else if (record->type == AssetType::Sky) {
                dirty = DrawBoolSetting(TEXT::Localize("ui.assetInspector.settingCopySkyCubemap").c_str(), settings,
                                        "copySkyCubemap", true) ||
                        dirty;
                dirty = DrawBoolSetting(TEXT::Localize("ui.assetInspector.settingAutoBakeIbl").c_str(), settings,
                                        "autoBakeIBL", true) ||
                        dirty;
                dirty = DrawIntSetting(TEXT::Localize("ui.assetInspector.settingIrradianceSize").c_str(), settings,
                                       "irradianceSize", 64, 1) ||
                        dirty;
                dirty = DrawIntSetting(TEXT::Localize("ui.assetInspector.settingIrradianceSamples").c_str(), settings,
                                       "irradianceSampleCount", 256, 1) ||
                        dirty;
                dirty = DrawIntSetting(TEXT::Localize("ui.assetInspector.settingPrefilteredSize").c_str(), settings,
                                       "prefilteredSize", 256, 1) ||
                        dirty;
                dirty = DrawIntSetting(TEXT::Localize("ui.assetInspector.settingPrefilteredMipCount").c_str(), settings,
                                       "prefilteredMipCount", 7, 0) ||
                        dirty;
                dirty = DrawIntSetting(TEXT::Localize("ui.assetInspector.settingPrefilteredSamples").c_str(), settings,
                                       "prefilteredSampleCount", 1024, 1) ||
                        dirty;
                dirty = DrawIntSetting(TEXT::Localize("ui.assetInspector.settingBrdfLutSize").c_str(), settings,
                                       "brdfLutSize", 256, 1) ||
                        dirty;
                dirty = DrawIntSetting(TEXT::Localize("ui.assetInspector.settingBrdfSamples").c_str(), settings,
                                       "brdfSampleCount", 1024, 1) ||
                        dirty;
            } else if (record->type == AssetType::Model) {
                ImGui::TextWrapped("%s", TEXT::Localize("ui.assetInspector.modelSourceSettingsHint").c_str());
                dirty = EDITOR::ASSET_IMPORT_SETTINGS::DrawModelImportSettings(settings) || dirty;
            } else if (record->type == AssetType::Scene) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.sceneCookHint").c_str());
            } else {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.noEditableSettings").c_str());
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TEXT::Localize("ui.assetInspector.tabArtifacts").c_str())) {
            if (record->artifactManifest.artifacts.empty()) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.noArtifacts").c_str());
            } else if (ImGui::BeginTable("AssetArtifactsTable", 4,
                                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn(TEXT::Localize("ui.assetInspector.role").c_str());
                ImGui::TableSetupColumn(TEXT::Localize("ui.assetInspector.format").c_str(),
                                        ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn(TEXT::Localize("ui.assetInspector.exists").c_str(),
                                        ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn(TEXT::Localize("ui.assetInspector.path").c_str());
                ImGui::TableHeadersRow();

                for (size_t artifactIndex = 0; artifactIndex < record->artifactManifest.artifacts.size();
                     ++artifactIndex) {
                    const AssetArtifactDesc& artifact = record->artifactManifest.artifacts[artifactIndex];
                    const std::filesystem::path artifactPath = assetDatabase.GetProjectRoot() / artifact.path;
                    const bool exists = std::filesystem::exists(artifactPath);

                    ImGui::PushID(static_cast<int>(artifactIndex));
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(artifact.role.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(artifact.format.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(exists ? TEXT::Localize("ui.common.yes").c_str()
                                                  : TEXT::Localize("ui.common.no").c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(artifact.path.c_str());
                    if (ImGui::BeginPopupContextItem("ArtifactContext")) {
                        if (ImGui::MenuItem(TEXT::Localize("ui.assetInspector.openFolder").c_str())) {
                            EDITOR::SHELL::OpenFolderInExplorer(artifactPath.parent_path());
                        }
                        if (ImGui::MenuItem(TEXT::Localize("ui.assetInspector.copyPath").c_str())) {
                            ImGui::SetClipboardText(artifact.path.c_str());
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TEXT::Localize("ui.assetInspector.tabDependencies").c_str())) {
            if (record->artifactManifest.dependencies.empty()) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.noDependencies").c_str());
            } else if (ImGui::BeginTable("AssetDependenciesTable", 4,
                                         ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn(TEXT::Localize("ui.assetInspector.role").c_str());
                ImGui::TableSetupColumn("GUID");
                ImGui::TableSetupColumn(TEXT::Localize("ui.assetInspector.resolved").c_str(),
                                        ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn(TEXT::Localize("ui.assetInspector.path").c_str());
                ImGui::TableHeadersRow();

                for (const AssetDependencyDesc& dependency : record->artifactManifest.dependencies) {
                    const bool resolved =
                        dependency.guid.IsValid()
                            ? assetDatabase.FindByGuid(dependency.guid) != nullptr
                            : !dependency.path.empty() &&
                                  std::filesystem::exists(assetDatabase.GetProjectRoot() / dependency.path);

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(dependency.role.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(dependency.guid.value.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(resolved ? TEXT::Localize("ui.common.yes").c_str()
                                                    : TEXT::Localize("ui.common.no").c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(dependency.path.c_str());
                }

                ImGui::EndTable();
            }
            if (!record->artifactManifest.dependencies.empty()) {
                if (ImGui::Button(TEXT::Localize("ui.assetInspector.importDependencies").c_str())) {
                    (void)assetDatabase.QueueImportDependencies(record->guid, false);
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(TEXT::Localize("ui.assetInspector.tabPreview").c_str())) {
            if (record->type == AssetType::Texture) {
                if (record->artifactManifest.artifacts.empty()) {
                    ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.previewTextureWaiting").c_str());
                } else {
                    ImGui::Text(TEXT::Localize("ui.assetInspector.previewTextureArtifact").c_str(),
                                record->artifactManifest.artifacts.front().path.c_str());
                    ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.previewTextureSrv").c_str());
                }
            } else if (record->type == AssetType::Sky) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.previewSkyReserved").c_str());
                if (const AssetArtifactDesc* artifact = ASSETS::SEMANTICS::FindCompatibleAssetArtifact(
                        *record, ASSETS::SEMANTICS::AssetArtifactKind::SkyCubemap)) {
                    ImGui::Text(TEXT::Localize("ui.assetInspector.previewSkyCubemap").c_str(), artifact->path.c_str());
                }
            } else if (record->type == AssetType::Model) {
                DrawPathRow(TEXT::Localize("ui.assetInspector.modelSource").c_str(), record->sourcePath);
                RefreshFilePreview(assetDatabase, *record);
                DrawModelDiagnostics(modelInfo_, modelReport_);
            } else if (record->type == AssetType::Scene) {
                DrawPathRow(TEXT::Localize("ui.assetInspector.sceneSource").c_str(), record->sourcePath);
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.sceneManagedHint").c_str());
            } else {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.noPreview").c_str());
            }
            ImGui::EndTabItem();
        }

        if (record->type == AssetType::Material &&
            ImGui::BeginTabItem(TEXT::Localize("ui.assetInspector.tabPbrMaterial").c_str())) {
            DrawMaterialAssetEditor(assetDatabase, assetRegistry, selection, *record, applyRuntimeMaterialGuid_,
                                    applyRuntimeMaterialData_, pendingRefreshRuntimeMaterialGuid_);
            ImGui::EndTabItem();
        }

        if (record->type == AssetType::SpriteMaterial &&
            ImGui::BeginTabItem(TEXT::Localize("ui.spriteMaterial.title").c_str())) {
            DrawSpriteMaterialEditor(assetDatabase, assetRegistry, *record);
            ImGui::EndTabItem();
        }
        if (record->type == AssetType::Shader &&
            ImGui::BeginTabItem(TEXT::Localize("ui.spriteMaterial.title").c_str())) {
            const bool recordInvalidated = DrawSpriteShaderCreation(assetDatabase, assetRegistry, *record, selection);
            ImGui::EndTabItem();
            if (recordInvalidated) {
                ImGui::EndTabBar();
                ImGui::EndChild();
                return;
            }
        }

        if (ImGui::BeginTabItem(TEXT::Localize("ui.assetInspector.tabDebugJson").c_str())) {
            const std::string settingsText = settings.dump(2);
            ImGui::InputTextMultiline(TEXT::Localize("ui.assetInspector.importSettingsJson").c_str(),
                                      const_cast<char*>(settingsText.c_str()), settingsText.size() + 1,
                                      ImVec2(-1.0f, 160.0f), ImGuiInputTextFlags_ReadOnly);

            ImGui::SeparatorText(TEXT::Localize("ui.assetInspector.rawMetaJson").c_str());
            RefreshFilePreview(assetDatabase, *record);
            const std::string& metaText = rawMeta_;
            if (metaText.empty()) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.metaJsonUnavailable").c_str());
            } else {
                ImGui::InputTextMultiline("##RawMetaJson", const_cast<char*>(metaText.c_str()), metaText.size() + 1,
                                          ImVec2(-1.0f, 180.0f), ImGuiInputTextFlags_ReadOnly);
            }

            ImGui::SeparatorText(TEXT::Localize("ui.assetInspector.importReportJson").c_str());
            const std::string& reportText = rawReport_;
            if (reportText.empty()) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.importReportUnavailable").c_str());
            } else {
                ImGui::InputTextMultiline("##ImportReportJson", const_cast<char*>(reportText.c_str()),
                                          reportText.size() + 1, ImVec2(-1.0f, 180.0f), ImGuiInputTextFlags_ReadOnly);
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
    ImGui::EndChild();

    if (dirty) {
        draft.edited.displayName = displayNameBuffer.data();
        draft.edited.importSettingsJson = settings.dump(2);
    }
    ImGui::Separator();
    ImGui::BeginDisabled(importing || !record->CanImport());
    const bool hasDraft = draft.IsDirty();
    ImGui::BeginDisabled(!settingsValid);
    if (ImGui::Button(
            TEXT::Localize(hasDraft ? "ui.assetInspector.applyReimport" : "ui.assetInspector.reimport").c_str())) {
        if (hasDraft) {
            const auto result = EDITOR::ApplyAssetImportSettings(assetDatabase, record->guid, draft);
            importSettingsStatus_ = result.message;
        } else if (!assetDatabase.QueueImportAssets({record->guid}, "Reimport " + record->displayName)) {
            importSettingsStatus_ = TEXT::Localize("ui.assetInspector.queueFailed");
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasDraft && importSettingsStatus_.empty() && record->lastImportSucceeded);
    if (ImGui::Button(TEXT::Localize("ui.assetInspector.revertSettings").c_str())) {
        const AssetGuid guid = record->guid;
        if (assetDatabase.ScanAssets(false)) {
            if (const auto* reloaded = assetDatabase.FindByGuid(guid)) {
                draft.Reset(*reloaded);
                CopyToBuffer(draft.edited.displayName, displayNameBuffer.data(), displayNameBuffer.size());
                importSettingsStatus_.clear();
            }
        } else {
            importSettingsStatus_ = TEXT::Localize("ui.assetInspector.reloadFailed");
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.assetInspector.restoreDefaults").c_str()))
        draft.edited.importSettingsJson = importDefaults_.importSettingsJson;
    ImGui::EndDisabled();
    if (importing)
        ImGui::TextDisabled("%s", TEXT::Localize("ui.assetInspector.importInProgress").c_str());
    if (!importSettingsStatus_.empty())
        ImGui::TextWrapped("%s", importSettingsStatus_.c_str());
}

bool AssetInspectorPanel::ConsumeApplyRuntimeMaterialRequest(AssetGuid& outGuid, PbrMaterialAssetData& outData) const {
    if (!applyRuntimeMaterialGuid_.IsValid()) {
        return false;
    }

    outGuid = applyRuntimeMaterialGuid_;
    outData = applyRuntimeMaterialData_;
    applyRuntimeMaterialGuid_ = {};
    applyRuntimeMaterialData_ = {};
    return true;
}

std::string AssetInspectorPanel::ConsumeRefreshRuntimeMaterialGuid() const {
    std::string guid = std::move(refreshRuntimeMaterialGuid_);
    refreshRuntimeMaterialGuid_.clear();
    return guid;
}

} // namespace HIKARI
