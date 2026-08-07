#include "Editor/Assets/Inspector/HIKARI_AssetInspectorPanel.h"

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
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Editor/Assets/HIKARI_AssetImportSettingsEditor.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
#include "Editor/Assets/Inspector/HIKARI_AssetInspectorInternal.h"

namespace HIKARI::EDITOR::ASSET_INSPECTOR {

#if defined(HIKARI_WITH_EDITOR)
namespace {

std::string LocalizeMaterialStatus(const std::string& status) {
  if (status == "Runtime preview updated live") {
    return TEXT::Localize("ui.assetInspector.runtimePreviewLive");
  }
  if (status == "Runtime preview refreshed") {
    return TEXT::Localize("ui.assetInspector.runtimePreviewRefreshed");
  }
  if (status == "Material saved") {
    return TEXT::Localize("ui.assetInspector.materialSaved");
  }
  if (status == "Material saved; import waits for the active batch") {
    return TEXT::Localize("ui.assetInspector.materialSavedWaitImport");
  }
  if (status == "Material save failed") {
    return TEXT::Localize("ui.assetInspector.materialSaveFailed");
  }
  return status;
}

} // namespace

void CopyToBuffer(const std::string &text, char *buffer, size_t bufferSize) {
  if (bufferSize == 0) {
    return;
  }
  const size_t copySize = (std::min)(text.size(), bufferSize - 1);
  std::memcpy(buffer, text.data(), copySize);
  buffer[copySize] = '\0';
}

bool DrawMaterialAssetEditor(AssetDatabase &assetDatabase,
                             AssetRegistry &assetRegistry,
                             EditorSelection &selection, AssetRecord &record,
                             AssetGuid &outApplyRuntimeGuid,
                             PbrMaterialAssetData &outApplyRuntimeData,
                             std::string &outRefreshRuntimeGuid) {

  static std::string loadedMaterialGuid{};
  static PbrMaterialAssetData editData{};
  static std::string materialStatus{};
  static bool materialDirty = false;

  const std::filesystem::path sourcePath =
      (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
  if (loadedMaterialGuid != record.guid.value) {
    loadedMaterialGuid = record.guid.value;
    std::string error{};
    if (!LoadPbrMaterialAssetData(sourcePath, editData, error)) {
      editData = {};
      editData.materialName = record.displayName.empty()
                                  ? record.sourcePath.stem().string()
                                  : record.displayName;
      materialStatus = error;
    } else {
      materialStatus.clear();
    }
    materialDirty = false;
  }

  bool changed = false;
  char nameBuffer[256]{};
  CopyToBuffer(editData.materialName, nameBuffer, sizeof(nameBuffer));
  if (ImGui::InputText(
          TEXT::Localize("ui.assetInspector.name").c_str(),
          nameBuffer,
          sizeof(nameBuffer))) {
    editData.materialName = nameBuffer;
    changed = true;
  }
  ImGui::TextDisabled(
      "%s",
      TEXT::Localize("ui.assetInspector.shaderPbr").c_str());

  if (!materialStatus.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                       LocalizeMaterialStatus(materialStatus).c_str());
  }

  // Keep the material editor close to the engine-facing HE texture slots.
  if (ImGui::CollapsingHeader(
          TEXT::Localize(
              "ui.assetInspector.baseColor").c_str(),
          ImGuiTreeNodeFlags_DefaultOpen)) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  TEXT::Localize(
                      "ui.assetInspector.albedo").c_str(),
                  editData.baseColorTexture, assetDatabase,
                  assetRegistry, &selection) ||
              changed;
    changed =
        ImGui::ColorEdit4(
            TEXT::Localize("ui.assetInspector.color").c_str(),
            &editData.baseColorFactor.x) || changed;
  }

  if (ImGui::CollapsingHeader(
          TEXT::Localize("ui.assetInspector.normal").c_str())) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  TEXT::Localize(
                      "ui.assetInspector.normal").c_str(),
                  editData.normalTexture,
                                              assetDatabase, assetRegistry,
                                              &selection) ||
              changed;
    changed =
        ImGui::DragFloat(
            TEXT::Localize("ui.assetInspector.scale").c_str(),
            &editData.normalScale, 0.01f, 0.0f, 4.0f) ||
        changed;
  }

  if (ImGui::CollapsingHeader(
          TEXT::Localize(
              "ui.assetInspector.metallicRoughness").c_str(),
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  TEXT::Localize(
                      "ui.assetInspector.mrTexture").c_str(),
                  editData.metallicRoughnessTexture,
                  assetDatabase, assetRegistry, &selection) ||
              changed;
    changed =
        ImGui::SliderFloat(
            TEXT::Localize("ui.assetInspector.metallic").c_str(),
            &editData.metallicFactor, 0.0f, 1.0f) ||
        changed;
    changed = ImGui::SliderFloat(
                  TEXT::Localize("ui.assetInspector.roughness").c_str(),
                  &editData.roughnessFactor, 0.0f, 1.0f) ||
              changed;
  }

  if (ImGui::CollapsingHeader(
          TEXT::Localize("ui.assetInspector.specular").c_str())) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  TEXT::Localize(
                      "ui.assetInspector.specular").c_str(),
                  editData.specularTexture, assetDatabase,
                  assetRegistry, &selection) ||
              changed;
    changed = EDITOR::DrawMaterialTextureSlot(
                  TEXT::Localize(
                      "ui.assetInspector.specularColor").c_str(),
                  editData.specularColorTexture,
                  assetDatabase, assetRegistry, &selection) ||
              changed;
    changed = ImGui::SliderFloat(
                  (TEXT::Localize("ui.assetInspector.factor") +
                      "##Specular").c_str(),
                  &editData.specularFactor, 0.0f, 2.0f) ||
              changed;
    changed =
        ImGui::ColorEdit3(
            (TEXT::Localize("ui.assetInspector.color") +
                "##Specular").c_str(),
            &editData.specularColorFactor.x) ||
        changed;
  }

  if (ImGui::CollapsingHeader(
          TEXT::Localize(
              "ui.assetInspector.ambientOcclusion").c_str())) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  TEXT::Localize(
                      "ui.assetInspector.occlusion").c_str(),
                  editData.occlusionTexture, assetDatabase,
                  assetRegistry, &selection) ||
              changed;
    changed = ImGui::SliderFloat(
                  TEXT::Localize("ui.assetInspector.strength").c_str(),
                  &editData.occlusionStrength, 0.0f, 1.0f) ||
              changed;
  }

  if (ImGui::CollapsingHeader(
          TEXT::Localize("ui.assetInspector.emissive").c_str())) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  TEXT::Localize(
                      "ui.assetInspector.emissive").c_str(),
                  editData.emissiveTexture, assetDatabase,
                  assetRegistry, &selection) ||
              changed;
    changed =
        ImGui::ColorEdit3(
            (TEXT::Localize("ui.assetInspector.color") +
                "##Emissive").c_str(),
            &editData.emissiveFactor.x) ||
        changed;
    changed = ImGui::DragFloat(
                  (TEXT::Localize("ui.assetInspector.strength") +
                      "##Emissive").c_str(),
                  &editData.emissiveStrength, 0.01f, 0.0f, 100.0f) ||
              changed;
  }

  if (ImGui::CollapsingHeader(
          TEXT::Localize("ui.assetInspector.options").c_str())) {
    changed = ImGui::Checkbox(
                  TEXT::Localize(
                      "ui.assetInspector.doubleSided").c_str(),
                  &editData.doubleSided) || changed;
    changed = ImGui::Checkbox(
                  TEXT::Localize("ui.assetInspector.unlit").c_str(),
                  &editData.unlit) || changed;
  }

  if (changed) {
    materialDirty = true;
    outApplyRuntimeGuid = record.guid;
    outApplyRuntimeData = editData;
    materialStatus = "Runtime preview updated live";
  }

  if (materialDirty) {
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f),
                       "%s",
                       TEXT::Localize(
                           "ui.assetInspector.unsavedEdits").c_str());
  } else {
    ImGui::TextDisabled(
        "%s",
        TEXT::Localize(
            "ui.assetInspector.materialClean").c_str());
  }

  ImGui::TextDisabled(
      "%s",
      TEXT::Localize(
          "ui.assetInspector.runtimePreviewHint").c_str());
  if (ImGui::Button(
          TEXT::Localize(
              "ui.assetInspector.refreshRuntimeNow").c_str())) {
    outApplyRuntimeGuid = record.guid;
    outApplyRuntimeData = editData;
    materialStatus = "Runtime preview refreshed";
  }
  ImGui::SameLine();
  if (ImGui::Button(
          TEXT::Localize(
              "ui.assetInspector.saveMaterial").c_str())) {
    std::string error{};
    if (SavePbrMaterialAssetData(sourcePath, editData, error)) {
      materialDirty = false;
      materialStatus = "Material saved";
      if (assetDatabase.QueueImportAssets(
              {record.guid}, "Saved material " + record.displayName)) {
        outRefreshRuntimeGuid = record.guid.value;
      } else {
        materialStatus =
            "Material saved; import waits for the active batch";
      }
    } else {
      materialStatus =
          error.empty() ? "Material save failed" : error;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button(
          TEXT::Localize("ui.assetInspector.revert").c_str())) {
    std::string error{};
    if (LoadPbrMaterialAssetData(sourcePath, editData, error)) {
      materialStatus.clear();
      materialDirty = false;
      EDITOR::ClearMaterialTextureSlotPreviewCache();
      outApplyRuntimeGuid = record.guid;
      outApplyRuntimeData = editData;
    } else {
      materialStatus = error;
    }
  }

  return materialDirty;
}
#endif

} // namespace HIKARI::EDITOR::ASSET_INSPECTOR
