#include "Editor/Assets/Inspector/HIKARI_AssetInspectorPanel.h"

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
  if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
    editData.materialName = nameBuffer;
    changed = true;
  }
  ImGui::TextDisabled("Shader: PBR");

  if (!materialStatus.empty()) {
    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "%s",
                       materialStatus.c_str());
  }

  // Keep the material editor close to the engine-facing HE texture slots.
  if (ImGui::CollapsingHeader("Base Color", ImGuiTreeNodeFlags_DefaultOpen)) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  "Albedo", editData.baseColorTexture, assetDatabase,
                  assetRegistry, &selection) ||
              changed;
    changed =
        ImGui::ColorEdit4("Color", &editData.baseColorFactor.x) || changed;
  }

  if (ImGui::CollapsingHeader("Normal")) {
    changed = EDITOR::DrawMaterialTextureSlot("Normal", editData.normalTexture,
                                              assetDatabase, assetRegistry,
                                              &selection) ||
              changed;
    changed =
        ImGui::DragFloat("Scale", &editData.normalScale, 0.01f, 0.0f, 4.0f) ||
        changed;
  }

  if (ImGui::CollapsingHeader("Metallic / Roughness",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  "MR Texture", editData.metallicRoughnessTexture,
                  assetDatabase, assetRegistry, &selection) ||
              changed;
    changed =
        ImGui::SliderFloat("Metallic", &editData.metallicFactor, 0.0f, 1.0f) ||
        changed;
    changed = ImGui::SliderFloat("Roughness", &editData.roughnessFactor, 0.0f,
                                 1.0f) ||
              changed;
  }

  if (ImGui::CollapsingHeader("Specular")) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  "Specular", editData.specularTexture, assetDatabase,
                  assetRegistry, &selection) ||
              changed;
    changed = EDITOR::DrawMaterialTextureSlot(
                  "Specular Color", editData.specularColorTexture,
                  assetDatabase, assetRegistry, &selection) ||
              changed;
    changed = ImGui::SliderFloat("Factor##Specular", &editData.specularFactor,
                                 0.0f, 2.0f) ||
              changed;
    changed =
        ImGui::ColorEdit3("Color##Specular", &editData.specularColorFactor.x) ||
        changed;
  }

  if (ImGui::CollapsingHeader("Ambient Occlusion")) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  "Occlusion", editData.occlusionTexture, assetDatabase,
                  assetRegistry, &selection) ||
              changed;
    changed = ImGui::SliderFloat("Strength", &editData.occlusionStrength, 0.0f,
                                 1.0f) ||
              changed;
  }

  if (ImGui::CollapsingHeader("Emissive")) {
    changed = EDITOR::DrawMaterialTextureSlot(
                  "Emissive", editData.emissiveTexture, assetDatabase,
                  assetRegistry, &selection) ||
              changed;
    changed =
        ImGui::ColorEdit3("Color##Emissive", &editData.emissiveFactor.x) ||
        changed;
    changed = ImGui::DragFloat("Strength##Emissive", &editData.emissiveStrength,
                               0.01f, 0.0f, 100.0f) ||
              changed;
  }

  if (ImGui::CollapsingHeader("Options")) {
    changed = ImGui::Checkbox("Double Sided", &editData.doubleSided) || changed;
    changed = ImGui::Checkbox("Unlit", &editData.unlit) || changed;
  }

  if (changed) {
    materialDirty = true;
  }

  if (materialDirty) {
    ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1.0f),
                       "Unsaved material edits");
  } else {
    ImGui::TextDisabled("Material file is clean");
  }

  if (ImGui::Button("Apply Runtime")) {
    outApplyRuntimeGuid = record.guid;
    outApplyRuntimeData = editData;
    materialStatus = "Applied to runtime preview";
  }
  ImGui::SameLine();
  if (ImGui::Button("Save Material")) {
    std::string error{};
    if (SavePbrMaterialAssetData(sourcePath, editData, error)) {
      materialDirty = false;
      materialStatus = "Material saved";
      if (assetDatabase.QueueImportAssets(
              {record.guid}, "Saved material " + record.displayName)) {
        outRefreshRuntimeGuid = record.guid.value;
      } else {
        materialStatus = "Material saved; import waits for the active batch";
      }
    } else {
      materialStatus = error.empty() ? "Material save failed" : error;
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Revert")) {
    std::string error{};
    if (LoadPbrMaterialAssetData(sourcePath, editData, error)) {
      materialStatus.clear();
      materialDirty = false;
      EDITOR::ClearMaterialTextureSlotPreviewCache();
    } else {
      materialStatus = error;
    }
  }

  return materialDirty;
}
#endif

} // namespace HIKARI::EDITOR::ASSET_INSPECTOR
