#include "Core/Text/HIKARI_AsciiCase.h"
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

std::filesystem::path
MakeUniqueCompoundSuffixFilePath(const std::filesystem::path &absoluteDirectory,
                                 const std::string &baseName,
                                 const std::string &compoundSuffix) {

  std::error_code ec{};
  std::filesystem::path candidate =
      absoluteDirectory / (baseName + compoundSuffix);
  if (!std::filesystem::exists(candidate, ec)) {
    return candidate;
  }

  for (int i = 1; i < 10000; ++i) {
    candidate = absoluteDirectory /
                (baseName + "_" + std::to_string(i) + compoundSuffix);
    ec.clear();
    if (!std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }

  return absoluteDirectory / (baseName + "_9999" + compoundSuffix);
}

std::string SanitizeFileToken(const std::string &raw,
                              const std::string &fallback) {
  std::string out{};
  out.reserve(raw.size());
  for (char ch : raw) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c) != 0 || ch == '_' || ch == '-') {
      out.push_back(ch);
    } else if (std::isspace(c) != 0) {
      out.push_back(' ');
    }
  }

  while (!out.empty() &&
         std::isspace(static_cast<unsigned char>(out.front())) != 0) {
    out.erase(out.begin());
  }
  while (!out.empty() &&
         std::isspace(static_cast<unsigned char>(out.back())) != 0) {
    out.pop_back();
  }
  return out.empty() ? fallback : out;
}

std::string GetSceneAssetBaseName(const std::filesystem::path &path) {
  std::string filename = path.filename().string();
  const std::string lower = TEXT::ToLowerAsciiCopy(filename);
  constexpr std::string_view kSceneJsonSuffix = ".scene.json";
  const std::string suffix(kSceneJsonSuffix);
  if (lower.size() >= kSceneJsonSuffix.size() &&
      lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
    filename.resize(filename.size() - kSceneJsonSuffix.size());
    return filename;
  }
  return path.stem().string();
}

bool IsScenesDirectoryPath(const std::filesystem::path &path) {
  const std::string generic =
      TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
  return generic == "assets/scenes" || generic.rfind("assets/scenes/", 0) == 0;
}

bool IsMaterialsDirectoryPath(const std::filesystem::path &path) {
  const std::string generic =
      TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
  return generic == "assets/materials" ||
         generic.rfind("assets/materials/", 0) == 0;
}

bool CreateDefaultMaterialAsset(AssetDatabase &assetDatabase,
                                const std::filesystem::path &currentDirectory,
                                std::filesystem::path &outRelativePath,
                                std::string &outError) {

  const std::filesystem::path materialDirectory =
      IsMaterialsDirectoryPath(currentDirectory)
          ? currentDirectory
          : std::filesystem::path("Assets/Materials");
  const std::filesystem::path absoluteMaterialDirectory =
      (assetDatabase.GetProjectRoot() / materialDirectory).lexically_normal();
  const std::filesystem::path absoluteMaterialPath =
      MakeUniqueCompoundSuffixFilePath(absoluteMaterialDirectory,
                                       "New Material", ".material.json");

  PbrMaterialAssetData data{};
  data.materialName = absoluteMaterialPath.stem().stem().string();
  if (data.materialName.empty()) {
    data.materialName = "New Material";
  }

  // Material Asset 驍ｵ・ｺ繝ｻ・ｯ Texture 驍ｵ・ｺ繝ｻ・ｮ GUID
  // 驛｢・ｧ陷代・・ｽ・ｿ隴弱・莠憺し・ｺ陷会ｽｱ・つ遶乗劼・ｽ・ｮ雋翫・諤咎し・ｺ繝ｻ・ｮ
  // HTEX 驍ｵ・ｺ繝ｻ・ｯ runtime builder
  // 驍ｵ・ｺ霑ｹ螟ｲ・ｽ・ｧ繝ｻ・｣髮手ｶ｣・ｽ・ｺ驍ｵ・ｺ陷ｷ・ｶ繝ｻ迢暦ｽｸ・ｲ郢晢ｽｻ
  if (!SavePbrMaterialAssetData(absoluteMaterialPath, data, outError)) {
    return false;
  }

  std::error_code relativeEc{};
  outRelativePath =
      std::filesystem::relative(absoluteMaterialPath,
                                assetDatabase.GetProjectRoot(), relativeEc)
          .lexically_normal();
  if (relativeEc) {
    outError = "Failed to resolve material path: " + relativeEc.message();
    return false;
  }
  return true;
}

bool CreateEmptySceneAsset(AssetDatabase &assetDatabase,
                           const std::filesystem::path &currentDirectory,
                           std::filesystem::path &outRelativePath,
                           std::string &outError) {

  const std::filesystem::path sceneDirectory =
      IsScenesDirectoryPath(currentDirectory)
          ? currentDirectory
          : std::filesystem::path("Assets/Scenes");
  const std::filesystem::path absoluteScenePath = MakeUniqueFilePath(
      (assetDatabase.GetProjectRoot() / sceneDirectory / "New Scene.scene.json")
          .lexically_normal());

  std::error_code ec{};
  std::filesystem::create_directories(absoluteScenePath.parent_path(), ec);
  if (ec) {
    outError = "Failed to create scene directory: " + ec.message();
    return false;
  }

  const std::string sceneName = absoluteScenePath.stem().stem().string();
  const nlohmann::json sceneJson{
      {"version", 1},
      {"sceneName", sceneName.empty() ? "New Scene" : sceneName},
      {"systems", nlohmann::json::array({
                      {{"systemId", "ModelRenderSystem"},
                       {"enabled", true},
                       {"executionOrder", 100},
                       {"settings", nlohmann::json::object()}},
                  })},
      {"objects", nlohmann::json::array()},
      {"environment",
       {
           {"ambient",
            {
                {"color", nlohmann::json::array({1.0f, 1.0f, 1.0f})},
                {"intensity", 0.2f},
            }},
           {"directional",
            {
                {"enabled", true},
                {"color", nlohmann::json::array({1.0f, 1.0f, 1.0f})},
                {"direction", nlohmann::json::array(
                                  {0.26832816f, -0.89442718f, -0.35777089f})},
                {"intensity", 1.0f},
            }},
           {"pointLights", nlohmann::json::array()},
           {"sky",
            {
                {"enabled", false},
                {"skyAsset", ""},
                {"exposure", 1.0f},
                {"followCamera", true},
                {"scale", 0.05f},
                {"tint", nlohmann::json::array({1.0f, 1.0f, 1.0f})},
                {"yaw", 0.0f},
            }},
           {"specularIntensity", 0.2f},
           {"specularPower", 32.0f},
       }},
  };

  std::string writeMessage{};
  if (!SERIALIZATION::JSON::WriteJsonFile(absoluteScenePath, sceneJson,
                                          &writeMessage)) {
    outError = "Failed to write scene file: " + writeMessage;
    return false;
  }

  outRelativePath = std::filesystem::relative(
                        absoluteScenePath, assetDatabase.GetProjectRoot(), ec)
                        .lexically_normal();
  if (ec) {
    outRelativePath = absoluteScenePath.lexically_normal();
  }
  return true;
}

bool CreateFolderFromBrowser(AssetDatabase &assetDatabase,
                             std::filesystem::path &currentDirectory,
                             std::string &lastOperationMessage) {

  const std::filesystem::path parentDirectory =
      assetDatabase.GetProjectRoot() / currentDirectory;
  const std::filesystem::path newFolder = MakeUniqueFolderPath(parentDirectory);
  std::error_code ec{};
  std::filesystem::create_directories(newFolder, ec);
  if (ec) {
    lastOperationMessage = "Folder creation failed: " + ec.message();
    return false;
  }

  std::error_code relativeEc{};
  std::filesystem::path relative = std::filesystem::relative(
      newFolder, assetDatabase.GetProjectRoot(), relativeEc);
  if (!relativeEc) {
    currentDirectory = relative.lexically_normal();
  }
  assetDatabase.ScanAssets(true);
  lastOperationMessage = "Folder created";
  return true;
}

bool CreateSceneFromBrowser(AssetDatabase &assetDatabase,
                            std::filesystem::path &currentDirectory,
                            EditorSelection &selection,
                            std::string &lastOperationMessage) {

  std::filesystem::path scenePath{};
  std::string error{};
  if (!CreateEmptySceneAsset(assetDatabase, currentDirectory, scenePath,
                             error)) {
    lastOperationMessage = error.empty() ? "Scene creation failed" : error;
    LogSceneAssetWarn("new scene failed: " + lastOperationMessage);
    return false;
  }

  assetDatabase.ScanAssets(true);
  currentDirectory = scenePath.parent_path();
  if (const AssetRecord *sceneRecord = assetDatabase.FindByPath(scenePath)) {
    SelectRecord(*sceneRecord, selection);
  }
  lastOperationMessage = "Scene asset created";
  LogSceneAssetInfo("new scene: " + scenePath.generic_string());
  return true;
}

bool CreateMaterialFromBrowser(AssetDatabase &assetDatabase,
                               std::filesystem::path &currentDirectory,
                               EditorSelection &selection,
                               std::string &lastOperationMessage) {

  std::filesystem::path materialPath{};
  std::string error{};
  if (!CreateDefaultMaterialAsset(assetDatabase, currentDirectory, materialPath,
                                  error)) {
    lastOperationMessage = error.empty() ? "Material creation failed" : error;
    HIKARI_LOG_WARN("[MaterialAsset] new material failed: " +
                    lastOperationMessage);
    return false;
  }

  assetDatabase.ScanAssets(true);
  currentDirectory = materialPath.parent_path();
  if (const AssetRecord *materialRecord =
          assetDatabase.FindByPath(materialPath)) {
    SelectRecord(*materialRecord, selection);
  }
  lastOperationMessage = "Material asset created";
  HIKARI_LOG_INFO("[MaterialAsset] new material: " +
                  materialPath.generic_string());
  return true;
}

} // namespace HIKARI::EDITOR::ASSET_BROWSER
