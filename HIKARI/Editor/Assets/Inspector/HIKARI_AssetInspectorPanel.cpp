#include "HIKARI_AssetInspectorPanel.h"

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
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Editor/Assets/HIKARI_AssetImportSettingsEditor.h"
#include "Editor/Assets/Inspector/HIKARI_AssetInspectorInternal.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Platform/HIKARI_EditorShellActions.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

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

const char *CoordinateSystemItems[] = {"RightHanded_YUp", "LeftHanded_YUp",
                                       "RightHanded_ZUp"};

const char *GeneratePolicyItems[] = {"IfMissing", "Always", "Never"};

int FindItemIndex(const char *const *items, int count,
                  const std::string &value) {
  for (int i = 0; i < count; ++i) {
    if (value == items[i]) {
      return i;
    }
  }
  return 0;
}

nlohmann::json ReadSettings(const AssetRecord &record) {
  nlohmann::json settings =
      nlohmann::json::parse(record.meta.importSettingsJson, nullptr, false);
  if (!settings.is_object()) {
    settings = nlohmann::json::object();
  }
  return settings;
}

std::string ReadTextFile(const std::filesystem::path &path) {
  std::ifstream ifs(path);
  if (!ifs.is_open()) {
    return {};
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

#if defined(HIKARI_WITH_EDITOR)
bool DrawComboSetting(const char *label, nlohmann::json &settings,
                      const char *key, const char *const *items,
                      int itemCount) {

  int current = FindItemIndex(items, itemCount, settings.value(key, ""));
  if (ImGui::Combo(label, &current, items, itemCount)) {
    settings[key] = items[current];
    return true;
  }
  return false;
}

bool DrawBoolSetting(const char *label, nlohmann::json &settings,
                     const char *key, bool fallback) {
  bool value = settings.value(key, fallback);
  if (ImGui::Checkbox(label, &value)) {
    settings[key] = value;
    return true;
  }
  return false;
}

bool DrawIntSetting(const char *label, nlohmann::json &settings,
                    const char *key, int fallback, int minimum = 0) {
  int value = settings.value(key, fallback);
  if (ImGui::InputInt(label, &value)) {
    settings[key] = (std::max)(minimum, value);
    return true;
  }
  return false;
}

bool DrawFloatSetting(const char *label, nlohmann::json &settings,
                      const char *key, float fallback, float minimum = 0.0f) {
  float value = settings.value(key, fallback);
  if (ImGui::InputFloat(label, &value)) {
    settings[key] = (std::max)(minimum, value);
    return true;
  }
  return false;
}

void DrawPathRow(const char *label, const std::filesystem::path &path) {
  ImGui::Text("%s: %s", label, path.generic_string().c_str());
}

#endif
} // namespace

void AssetInspectorPanel::Draw(AssetDatabase &assetDatabase,
                               AssetRegistry &assetRegistry,
                               EditorSelection &selection) const {
#if defined(HIKARI_WITH_EDITOR)
  if (!pendingRefreshRuntimeMaterialGuid_.empty()) {
    const AssetImportBatchStatus status = assetDatabase.GetQueuedImportStatus();
    if (!status.active && status.completed) {
      if (!status.canceled && status.failed == 0) {
        EDITOR::ClearMaterialTextureSlotPreviewCache();
        refreshRuntimeMaterialGuid_ = pendingRefreshRuntimeMaterialGuid_;
      }
      pendingRefreshRuntimeMaterialGuid_.clear();
    }
  }
  if (selection.selectedAssetGuid.empty()) {
    ImGui::TextDisabled("No Asset selected");
    return;
  }

  AssetRecord *record =
      assetDatabase.FindByGuid(AssetGuid{selection.selectedAssetGuid});
  if (!record) {
    ImGui::TextDisabled("Selected Asset is no longer in the database");
    return;
  }

  static std::string loadedGuid{};
  static std::array<char, 256> displayNameBuffer{};
  static std::array<char, 16384> importSettingsBuffer{};
  if (loadedGuid != record->guid.value) {
    loadedGuid = record->guid.value;
    CopyToBuffer(record->displayName, displayNameBuffer.data(),
                 displayNameBuffer.size());
    CopyToBuffer(record->meta.importSettingsJson, importSettingsBuffer.data(),
                 importSettingsBuffer.size());
  }

  bool dirty = false;
  nlohmann::json settings = ReadSettings(*record);
  const AssetImportState importState = GetImportState(*record);

  auto saveMeta = [&]() {
    record->meta.displayName = displayNameBuffer.data();
    record->displayName = record->meta.displayName;
    record->meta.importSettingsJson = importSettingsBuffer.data();
    if (assetDatabase.WriteMeta(*record)) {
      record->importOutdated = true;
      record->lastImportMessage =
          "[AssetDatabase] Meta saved; reimport required";
    }
  };

  if (ImGui::BeginTabBar("AssetInspectorTabs",
                         ImGuiTabBarFlags_FittingPolicyScroll)) {
    if (ImGui::BeginTabItem("Summary")) {
      if (ImGui::InputText("Display Name", displayNameBuffer.data(),
                           displayNameBuffer.size())) {
        dirty = true;
      }

      ImGui::Text("GUID: %s", record->guid.value.c_str());
      ImGui::Text("Type: %s", ASSETS::SEMANTICS::ToString(record->type).data());
      DrawPathRow("Source Path", record->sourcePath);
      DrawPathRow("Meta Path", record->metaPath);
      ImGui::Text("Importer: %s", record->meta.importerId.empty()
                                      ? "<none>"
                                      : record->meta.importerId.c_str());
      ImGui::Text("Importer Version: %u", record->meta.importerVersion);
      DrawPathRow("Imported Directory", record->importedDirectory);
      ImGui::Text("State: %s", ToString(importState));
      if (!record->lastImportMessage.empty()) {
        ImGui::TextWrapped("Last Import Message: %s",
                           record->lastImportMessage.c_str());
      }

      if (ImGui::Button("Copy GUID")) {
        ImGui::SetClipboardText(record->guid.value.c_str());
      }
      ImGui::SameLine();
      if (ImGui::Button("Show Source")) {
        EDITOR::SHELL::ShowFileInExplorer(assetDatabase.GetProjectRoot() /
                                          record->sourcePath);
      }
      ImGui::SameLine();
      if (ImGui::Button("Show Imported Directory") &&
          !record->importedDirectory.empty()) {
        EDITOR::SHELL::OpenFolderInExplorer(record->importedDirectory);
      }

      if (ImGui::Button("Reimport")) {
        (void)assetDatabase.QueueImportAssets(
            {record->guid}, "Reimport " + record->displayName);
      }
      ImGui::SameLine();
      if (ImGui::Button("Import Dependencies")) {
        (void)assetDatabase.QueueImportDependencies(record->guid, false);
      }
      ImGui::SameLine();
      if (ImGui::Button("Save Meta")) {
        if (dirty) {
          record->meta.importSettingsJson = settings.dump(2);
          CopyToBuffer(record->meta.importSettingsJson,
                       importSettingsBuffer.data(),
                       importSettingsBuffer.size());
        }
        saveMeta();
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Import Settings")) {
      if (record->type == AssetType::Texture) {
        dirty = DrawComboSetting("Usage", settings, "usage",
                                 TextureUsageItems.data(),
                                 static_cast<int>(TextureUsageItems.size())) ||
                dirty;
        dirty =
            DrawComboSetting("Color Space", settings, "colorSpace",
                             TextureColorSpaceItems.data(),
                             static_cast<int>(TextureColorSpaceItems.size())) ||
            dirty;
        dirty = DrawComboSetting(
                    "Compression", settings, "compression",
                    TextureCompressionItems.data(),
                    static_cast<int>(TextureCompressionItems.size())) ||
                dirty;
        dirty =
            DrawComboSetting("Mip Policy", settings, "mipPolicy",
                             TextureMipPolicyItems.data(),
                             static_cast<int>(TextureMipPolicyItems.size())) ||
            dirty;
        dirty =
            DrawIntSetting("Max Size", settings, "maxSize", 4096, 1) || dirty;
        dirty = DrawBoolSetting("Force Power Of Two", settings,
                                "forcePowerOfTwo", false) ||
                dirty;
        dirty =
            DrawBoolSetting("Allow Resize", settings, "allowResize", false) ||
            dirty;
      } else if (record->type == AssetType::Sky) {
        dirty = DrawBoolSetting("Copy Sky Cubemap", settings, "copySkyCubemap",
                                true) ||
                dirty;
        dirty =
            DrawBoolSetting("Auto Bake IBL", settings, "autoBakeIBL", true) ||
            dirty;
        dirty = DrawIntSetting("Irradiance Size", settings, "irradianceSize",
                               64, 1) ||
                dirty;
        dirty = DrawIntSetting("Irradiance Samples", settings,
                               "irradianceSampleCount", 256, 1) ||
                dirty;
        dirty = DrawIntSetting("Prefiltered Size", settings, "prefilteredSize",
                               256, 1) ||
                dirty;
        dirty = DrawIntSetting("Prefiltered Mip Count", settings,
                               "prefilteredMipCount", 7, 0) ||
                dirty;
        dirty = DrawIntSetting("Prefiltered Samples", settings,
                               "prefilteredSampleCount", 1024, 1) ||
                dirty;
        dirty =
            DrawIntSetting("BRDF LUT Size", settings, "brdfLutSize", 256, 1) ||
            dirty;
        dirty = DrawIntSetting("BRDF Samples", settings, "brdfSampleCount",
                               1024, 1) ||
                dirty;
      } else if (record->type == AssetType::Model) {
        dirty =
            DrawFloatSetting("Unit Scale", settings, "unitScale", 1.0f, 0.0f) ||
            dirty;
        dirty = DrawComboSetting("Coordinate System", settings,
                                 "coordinateSystem", CoordinateSystemItems,
                                 IM_ARRAYSIZE(CoordinateSystemItems)) ||
                dirty;
        dirty = DrawComboSetting("Generate Normals", settings,
                                 "generateNormals", GeneratePolicyItems,
                                 IM_ARRAYSIZE(GeneratePolicyItems)) ||
                dirty;
        dirty = DrawComboSetting("Generate Tangents", settings,
                                 "generateTangents", GeneratePolicyItems,
                                 IM_ARRAYSIZE(GeneratePolicyItems)) ||
                dirty;
        dirty = DrawBoolSetting("Load Materials", settings, "loadMaterials",
                                true) ||
                dirty;
        dirty =
            DrawBoolSetting("Load Textures", settings, "loadTextures", true) ||
            dirty;
        dirty = EDITOR::ASSET_IMPORT_SETTINGS::DrawModelClusteredGeometryCookSettings(
                    settings, true) ||
                dirty;
      } else if (record->type == AssetType::Scene) {
        dirty = DrawBoolSetting("Cook Scene", settings, "cookScene", false) ||
                dirty;
        ImGui::TextDisabled("Scene cook is reserved; SceneSerializer JSON "
                            "remains the runtime source.");
      } else {
        ImGui::TextDisabled(
            "No editable import settings for this asset type yet");
      }

      if (dirty) {
        record->meta.displayName = displayNameBuffer.data();
        record->displayName = record->meta.displayName;
        record->meta.importSettingsJson = settings.dump(2);
        CopyToBuffer(record->meta.importSettingsJson,
                     importSettingsBuffer.data(), importSettingsBuffer.size());
      }
      if (ImGui::Button("Save Meta")) {
        saveMeta();
      }
      ImGui::SameLine();
      if (ImGui::Button("Reimport")) {
        (void)assetDatabase.QueueImportAssets(
            {record->guid}, "Reimport " + record->displayName);
      }

      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Artifacts")) {
      if (record->artifactManifest.artifacts.empty()) {
        ImGui::TextDisabled("No artifacts");
      } else if (ImGui::BeginTable("AssetArtifactsTable", 4,
                                   ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed,
                                80.0f);
        ImGui::TableSetupColumn("Exists", ImGuiTableColumnFlags_WidthFixed,
                                70.0f);
        ImGui::TableSetupColumn("Path");
        ImGui::TableHeadersRow();

        for (const AssetArtifactDesc &artifact :
             record->artifactManifest.artifacts) {
          const std::filesystem::path artifactPath =
              assetDatabase.GetProjectRoot() / artifact.path;
          const bool exists = std::filesystem::exists(artifactPath);

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(artifact.role.c_str());
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(artifact.format.c_str());
          ImGui::TableSetColumnIndex(2);
          ImGui::TextUnformatted(exists ? "Yes" : "No");
          ImGui::TableSetColumnIndex(3);
          ImGui::TextUnformatted(artifact.path.c_str());
          if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Open Folder")) {
              EDITOR::SHELL::OpenFolderInExplorer(artifactPath.parent_path());
            }
            if (ImGui::MenuItem("Copy Path")) {
              ImGui::SetClipboardText(artifact.path.c_str());
            }
            ImGui::EndPopup();
          }
        }

        ImGui::EndTable();
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Dependencies")) {
      if (record->artifactManifest.dependencies.empty()) {
        ImGui::TextDisabled("No dependencies");
      } else if (ImGui::BeginTable("AssetDependenciesTable", 4,
                                   ImGuiTableFlags_RowBg |
                                       ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Role");
        ImGui::TableSetupColumn("GUID");
        ImGui::TableSetupColumn("Resolved", ImGuiTableColumnFlags_WidthFixed,
                                80.0f);
        ImGui::TableSetupColumn("Path");
        ImGui::TableHeadersRow();

        for (const AssetDependencyDesc &dependency :
             record->artifactManifest.dependencies) {
          const bool resolved =
              dependency.guid.IsValid()
                  ? assetDatabase.FindByGuid(dependency.guid) != nullptr
                  : !dependency.path.empty() &&
                        std::filesystem::exists(assetDatabase.GetProjectRoot() /
                                                dependency.path);

          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::TextUnformatted(dependency.role.c_str());
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(dependency.guid.value.c_str());
          ImGui::TableSetColumnIndex(2);
          ImGui::TextUnformatted(resolved ? "Yes" : "No");
          ImGui::TableSetColumnIndex(3);
          ImGui::TextUnformatted(dependency.path.c_str());
        }

        ImGui::EndTable();
      }
      if (!record->artifactManifest.dependencies.empty()) {
        if (ImGui::Button("Import Dependencies")) {
          (void)assetDatabase.QueueImportDependencies(record->guid, false);
        }
      }
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Preview")) {
      if (record->type == AssetType::Texture) {
        if (record->artifactManifest.artifacts.empty()) {
          ImGui::TextDisabled(
              "Texture preview waits for an imported DDS artifact");
        } else {
          ImGui::Text("Texture artifact: %s",
                      record->artifactManifest.artifacts.front().path.c_str());
          ImGui::TextDisabled(
              "SRV preview loading is reserved for the texture preview pass");
        }
      } else if (record->type == AssetType::Sky) {
        ImGui::TextDisabled("Cubemap face/equirect preview is reserved for the "
                            "sky preview pass");
        if (const AssetArtifactDesc *artifact =
                ASSETS::SEMANTICS::FindCompatibleAssetArtifact(
                    *record,
                    ASSETS::SEMANTICS::AssetArtifactKind::SkyCubemap)) {
          ImGui::Text("Sky cubemap: %s", artifact->path.c_str());
        }
      } else if (record->type == AssetType::Model) {
        DrawPathRow("Model Source", record->sourcePath);
        DrawModelDiagnostics(assetDatabase, *record);
      } else if (record->type == AssetType::Scene) {
        DrawPathRow("Scene Source", record->sourcePath);
        ImGui::TextDisabled("Scene is managed as a project asset; runtime "
                            "scene switching still uses the scene catalog.");
      } else {
        ImGui::TextDisabled("No preview for this asset type yet");
      }
      ImGui::EndTabItem();
    }

    if (record->type == AssetType::Material &&
        ImGui::BeginTabItem("PBR Material")) {
      DrawMaterialAssetEditor(assetDatabase, assetRegistry, selection, *record,
                              applyRuntimeMaterialGuid_,
                              applyRuntimeMaterialData_,
                              pendingRefreshRuntimeMaterialGuid_);
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Debug JSON")) {
      if (ImGui::InputTextMultiline(
              "Import Settings JSON", importSettingsBuffer.data(),
              importSettingsBuffer.size(), ImVec2(-1.0f, 160.0f))) {
        record->meta.importSettingsJson = importSettingsBuffer.data();
        dirty = true;
      }
      if (ImGui::Button("Save Raw Import Settings")) {
        saveMeta();
      }

      ImGui::SeparatorText("Raw Meta JSON");
      const std::string metaText = ReadTextFile(record->metaPath);
      if (metaText.empty()) {
        ImGui::TextDisabled("Meta JSON is not available");
      } else {
        ImGui::InputTextMultiline("##RawMetaJson",
                                  const_cast<char *>(metaText.c_str()),
                                  metaText.size() + 1, ImVec2(-1.0f, 180.0f),
                                  ImGuiInputTextFlags_ReadOnly);
      }

      ImGui::SeparatorText("Import Report JSON");
      const std::string reportText =
          ReadTextFile(record->importedDirectory / "import_report.json");
      if (reportText.empty()) {
        ImGui::TextDisabled("Import report is not available");
      } else {
        ImGui::InputTextMultiline("##ImportReportJson",
                                  const_cast<char *>(reportText.c_str()),
                                  reportText.size() + 1, ImVec2(-1.0f, 180.0f),
                                  ImGuiInputTextFlags_ReadOnly);
      }

      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  if (dirty) {
    record->meta.displayName = displayNameBuffer.data();
    record->displayName = record->meta.displayName;
  }
#else
  (void)assetDatabase;
  (void)assetRegistry;
  (void)selection;
#endif
}

bool AssetInspectorPanel::ConsumeApplyRuntimeMaterialRequest(
    AssetGuid &outGuid, PbrMaterialAssetData &outData) const {

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
