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
#include "Assets/Material/HIKARI_MaterialAssetData.h"
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

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"

namespace HIKARI::EDITOR::ASSET_BROWSER {

std::string ToAssetTypeText(AssetType type) {
  if (type == AssetType::ModelMaterialBinding) {
    return TEXT::Localize("ui.assetBrowser.typeMaterialSet");
  }
  switch (type) {
  case AssetType::Texture:
    return TEXT::Localize("ui.assetBrowser.typeTexture");
  case AssetType::Model:
    return TEXT::Localize("ui.assetBrowser.typeModel");
  case AssetType::Scene:
    return TEXT::Localize("ui.assetBrowser.typeScene");
  case AssetType::Material:
    return TEXT::Localize("ui.assetBrowser.typeMaterial");
  case AssetType::Sky:
    return TEXT::Localize("ui.assetBrowser.typeSky");
  case AssetType::VfxEffect:
    return TEXT::Localize("ui.assetBrowser.typeVfx");
  case AssetType::Sequence:
    return TEXT::Localize("ui.assetBrowser.typeSequence");
  case AssetType::AnimationStateMachine:
    return TEXT::Localize("ui.assetBrowser.typeAnimationStateMachine");
  case AssetType::Dialogue:
    return TEXT::Localize("ui.assetBrowser.typeDialogue");
  case AssetType::Unknown:
  default:
    return ASSETS::SEMANTICS::ToString(type).data();
  }
}

std::filesystem::path
MakeUniqueFolderPath(const std::filesystem::path &parentDirectory) {
  std::filesystem::path candidate = parentDirectory / "New Folder";
  std::error_code ec{};
  if (!std::filesystem::exists(candidate, ec)) {
    return candidate;
  }

  for (int i = 2; i < 1000; ++i) {
    candidate = parentDirectory / ("New Folder " + std::to_string(i));
    ec.clear();
    if (!std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }

  return parentDirectory / "New Folder 999";
}

void SelectRecord(const AssetRecord &record, EditorSelection &selection) {
  selection.ClearObjects();
  selection.selectedAssetGuid = record.guid.value;
  selection.selectedAssetPath = record.sourcePath.generic_string();
  selection.selectedAsset = nullptr;
}

std::string FirstArtifactPath(const AssetRecord &record) {
  for (const AssetArtifactDesc &artifact : record.artifactManifest.artifacts) {
    if (!artifact.path.empty()) {
      return artifact.path;
    }
  }
  return {};
}

} // namespace HIKARI::EDITOR::ASSET_BROWSER
