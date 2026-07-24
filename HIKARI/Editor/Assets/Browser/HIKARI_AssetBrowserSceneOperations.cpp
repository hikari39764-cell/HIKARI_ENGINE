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

bool IsSameFilePath(const std::filesystem::path &lhs,
                    const std::filesystem::path &rhs) {

  const std::filesystem::path normalizedLhs = lhs.lexically_normal();
  const std::filesystem::path normalizedRhs = rhs.lexically_normal();
  if (TEXT::ToLowerAsciiCopy(normalizedLhs.generic_string()) ==
      TEXT::ToLowerAsciiCopy(normalizedRhs.generic_string())) {
    return true;
  }

  std::error_code ec{};
  return std::filesystem::equivalent(normalizedLhs, normalizedRhs, ec) && !ec;
}

void LogSceneAssetInfo(const std::string &message) {
  HIKARI_LOG_INFO("[SceneAsset] " + message);
}

void LogSceneAssetWarn(const std::string &message) {
  HIKARI_LOG_WARN("[SceneAsset] " + message);
}

bool MoveFileSafe(const std::filesystem::path &from,
                  const std::filesystem::path &to, std::string &outError) {

  std::error_code ec{};
  if (!std::filesystem::exists(from, ec)) {
    outError = "Source file is missing: " + from.generic_string();
    return false;
  }

  std::filesystem::create_directories(to.parent_path(), ec);
  if (ec) {
    outError = "Failed to create target directory: " + ec.message();
    return false;
  }

  ec.clear();
  if (std::filesystem::exists(to, ec)) {
    outError = "Target file already exists: " + to.generic_string();
    return false;
  }

  ec.clear();
  std::filesystem::rename(from, to, ec);
  if (ec) {
    outError = "Move failed: " + ec.message();
    return false;
  }
  return true;
}

void MoveFileBackBestEffort(const std::filesystem::path &from,
                            const std::filesystem::path &to) {

  std::error_code ec{};
  if (std::filesystem::exists(from, ec) && !std::filesystem::exists(to, ec)) {
    std::filesystem::rename(from, to, ec);
  }
}

bool UpdateSceneJsonSceneName(const std::filesystem::path &scenePath,
                              std::string_view sceneName,
                              std::string &outError) {

  nlohmann::json root{};
  if (!SERIALIZATION::JSON::ReadJsonFile(scenePath, root) ||
      !root.is_object()) {
    outError = "Scene JSON could not be read: " + scenePath.generic_string();
    return false;
  }

  root["sceneName"] = std::string(sceneName);
  if (!SERIALIZATION::JSON::WriteJsonFile(scenePath, root)) {
    outError = "Scene JSON could not be written: " + scenePath.generic_string();
    return false;
  }
  return true;
}

bool UpdateSceneMetaAfterMove(AssetDatabase &assetDatabase,
                              const AssetRecord &oldRecord,
                              const std::filesystem::path &relativeSource,
                              const std::filesystem::path &metaPath,
                              std::string_view displayName,
                              std::string &outError) {

  if (metaPath.empty()) {
    return true;
  }

  std::error_code ec{};
  if (!std::filesystem::exists(metaPath, ec)) {
    return true;
  }

  AssetMeta meta{};
  if (!assetDatabase.ReadMeta(metaPath, meta)) {
    outError = "Scene meta could not be read: " + metaPath.generic_string();
    return false;
  }

  AssetRecord writable = oldRecord;
  writable.sourcePath = relativeSource.lexically_normal();
  writable.metaPath = metaPath;
  writable.displayName = std::string(displayName);
  writable.meta = std::move(meta);
  writable.meta.sourcePath = writable.sourcePath.generic_string();
  writable.meta.displayName = std::string(displayName);

  if (!assetDatabase.WriteMeta(writable)) {
    outError = "Scene meta could not be written: " + metaPath.generic_string();
    return false;
  }
  return true;
}

bool DuplicateSceneAsset(AssetDatabase &assetDatabase,
                         const AssetRecord &record,
                         std::filesystem::path &outRelativePath,
                         std::string &outError) {

  const std::filesystem::path sourcePath =
      (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
  const std::string baseName = GetSceneAssetBaseName(sourcePath);
  const std::filesystem::path targetPath = MakeUniqueFilePath(
      sourcePath.parent_path() / (baseName + " Copy.scene.json"));

  nlohmann::json root{};
  if (!SERIALIZATION::JSON::ReadJsonFile(sourcePath, root) ||
      !root.is_object()) {
    outError = "Scene duplicate failed: source JSON could not be read";
    return false;
  }

  root["sceneName"] = GetSceneAssetBaseName(targetPath);
  if (!SERIALIZATION::JSON::WriteJsonFile(targetPath, root)) {
    outError = "Scene duplicate failed: destination could not be written";
    LogSceneAssetWarn("duplicate failed: " + outError);
    return false;
  }

  std::error_code ec{};
  outRelativePath =
      std::filesystem::relative(targetPath, assetDatabase.GetProjectRoot(), ec)
          .lexically_normal();
  if (ec) {
    outRelativePath = targetPath.lexically_normal();
  }
  LogSceneAssetInfo("duplicated: " + sourcePath.generic_string() + " -> " +
                    targetPath.generic_string());
  return true;
}

bool RenameSceneAsset(AssetDatabase &assetDatabase, const AssetRecord &record,
                      std::string_view newName,
                      std::filesystem::path &outRelativePath,
                      std::string &outError) {

  const std::string cleanName =
      SanitizeFileToken(std::string(newName), "Scene");
  const std::filesystem::path oldSource =
      (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
  const std::filesystem::path desiredSource =
      (oldSource.parent_path() / (cleanName + ".scene.json"))
          .lexically_normal();

  // 髯ｷ・ｷ隰疲ｻ・ｹ驛｢譎｢・ｽ・ｪ驛｢譎樔ｺらｹ晢ｽｻ驛｢譎｢・｣・ｰ驍ｵ・ｺ繝ｻ・ｧ驍ｵ・ｺ繝ｻ・ｯ驛｢譎・ｽｼ譁撰ｼ憺Δ・ｧ繝ｻ・､驛｢譎｢・ｽ・ｫ驛｢・ｧ髮区ｫ∝樺驍ｵ・ｺ闕ｵ譎｢・ｼ繝ｻ・ｸ・ｺ陞｢・ｹ・つ遶擾ｽｬ繝ｻ・｡繝ｻ・ｨ鬩穂ｼ夲ｽｽ・ｺ髯ｷ・ｷ鬮ｦ・ｪ隨・ｽ｡驍ｵ・ｺ陞滂ｽｧ鬩溽｢托ｽｭ蟶ｶ・ｺ蛟ｪ繝ｻ驛｢・ｧ闕ｵ謨鳴郢晢ｽｻ
  if (IsSameFilePath(oldSource, desiredSource)) {
    if (!UpdateSceneJsonSceneName(oldSource, cleanName, outError)) {
      LogSceneAssetWarn("rename failed: " + outError);
      return false;
    }
    if (!UpdateSceneMetaAfterMove(assetDatabase, record, record.sourcePath,
                                  record.metaPath, cleanName, outError)) {
      LogSceneAssetWarn("rename failed: " + outError);
      return false;
    }
    outRelativePath = record.sourcePath;
    LogSceneAssetInfo("renamed metadata only: " + oldSource.generic_string());
    return true;
  }

  const std::filesystem::path newSource = MakeUniqueFilePath(desiredSource);
  const std::filesystem::path newRelativeSource =
      PROJECT_PATHS::MakeProjectRelativePath(assetDatabase.GetProjectRoot(),
                                             newSource);
  const std::filesystem::path oldMetaPath = record.metaPath;
  const std::filesystem::path newMetaPath =
      assetDatabase.GetMetaPathForSource(newRelativeSource);

  std::error_code ec{};
  if (!std::filesystem::exists(oldSource, ec)) {
    outError = "Scene rename failed: source is missing";
    LogSceneAssetWarn("rename failed: " + outError);
    return false;
  }

  bool movedScene = false;
  bool movedMeta = false;
  // 鬩募∞・ｽ・ｻ髯ｷ閧ｴ蝮ｩ・つ雎郁ｲｻ・ｽ・ｸ繝ｻ・ｭ驍ｵ・ｺ繝ｻ・ｧ髯樊ｻゑｽｽ・ｱ髫ｰ・ｨ陷会ｽｱ繝ｻ・ｰ驍ｵ・ｺ雋・ｽｷ繝ｻ・ｰ繝ｻ・ｴ髯ｷ・ｷ陋ｹ・ｻ郢晢ｽｻ驍ｵ・ｲ遶乗剌・ｺ繝ｻ螯吶・・ｽ驍ｵ・ｺ繝ｻ・ｪ鬩包ｽｽ郢晢ｽｻ陝ｲ繝ｻ・ｸ・ｺ繝ｻ・ｧ髯ｷ蛹ｻ繝ｻ郢晢ｽｻ鬯ｩ貅ｷ隱ｿ繝ｻ・ｽ繝ｻ・ｮ驍ｵ・ｺ繝ｻ・ｸ髫ｰ魃会ｽｽ・ｻ驍ｵ・ｺ陷ｷ・ｶ・つ郢晢ｽｻ
  if (!MoveFileSafe(oldSource, newSource, outError)) {
    outError = "Scene rename failed: " + outError;
    LogSceneAssetWarn("rename failed: " + outError);
    return false;
  }
  movedScene = true;

  if (!oldMetaPath.empty() && std::filesystem::exists(oldMetaPath, ec)) {
    if (!MoveFileSafe(oldMetaPath, newMetaPath, outError)) {
      if (movedScene) {
        MoveFileBackBestEffort(newSource, oldSource);
      }
      outError = "Scene meta rename failed: " + outError;
      LogSceneAssetWarn("rename failed: " + outError);
      return false;
    }
    movedMeta = true;
  }

  if (!UpdateSceneJsonSceneName(newSource, cleanName, outError)) {
    if (movedMeta) {
      MoveFileBackBestEffort(newMetaPath, oldMetaPath);
    }
    if (movedScene) {
      MoveFileBackBestEffort(newSource, oldSource);
    }
    LogSceneAssetWarn("rename failed: " + outError);
    return false;
  }

  outRelativePath = newRelativeSource;
  if (!UpdateSceneMetaAfterMove(assetDatabase, record, outRelativePath,
                                newMetaPath, cleanName, outError)) {
    if (movedMeta) {
      MoveFileBackBestEffort(newMetaPath, oldMetaPath);
    }
    if (movedScene) {
      MoveFileBackBestEffort(newSource, oldSource);
    }
    LogSceneAssetWarn("rename failed: " + outError);
    return false;
  }

  LogSceneAssetInfo("renamed: " + oldSource.generic_string() + " -> " +
                    newSource.generic_string());
  return true;
}

std::filesystem::path
MakeSceneTrashDirectory(const AssetDatabase &assetDatabase) {
  const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
  return assetDatabase.GetLibraryRoot() / "Trash" /
         ("scene_" + std::to_string(ticks));
}

bool DeleteSceneAssetToTrash(AssetDatabase &assetDatabase,
                             const AssetRecord &record,
                             bool &outClearedStartupScene,
                             std::string &outError) {

  outClearedStartupScene = false;
  const std::filesystem::path sourcePath =
      (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
  const std::filesystem::path trashDirectory =
      MakeSceneTrashDirectory(assetDatabase);
  const std::filesystem::path trashSourcePath =
      trashDirectory / sourcePath.filename();
  const std::filesystem::path metaPath = record.metaPath;
  const std::filesystem::path trashMetaPath =
      metaPath.empty()
          ? std::filesystem::path{}
          : trashDirectory / "AssetMeta" / record.sourcePath.parent_path() /
                metaPath.filename();

  std::error_code ec{};
  if (!std::filesystem::exists(sourcePath, ec)) {
    outError = "Scene delete failed: source is missing";
    LogSceneAssetWarn("delete failed: " + outError);
    return false;
  }

  std::filesystem::create_directories(trashDirectory, ec);
  if (ec) {
    outError = "Scene delete failed: " + ec.message();
    LogSceneAssetWarn("delete failed: " + outError);
    return false;
  }

  if (std::filesystem::exists(trashSourcePath, ec)) {
    outError = "Scene delete failed: trash target already exists";
    LogSceneAssetWarn("delete failed: " + outError);
    return false;
  }
  if (!metaPath.empty() && std::filesystem::exists(metaPath, ec)) {
    ec.clear();
    if (std::filesystem::exists(trashMetaPath, ec)) {
      outError = "Scene delete failed: trash meta target already exists";
      LogSceneAssetWarn("delete failed: " + outError);
      return false;
    }
  }

  bool movedScene = false;
  if (!MoveFileSafe(sourcePath, trashSourcePath, outError)) {
    outError = "Scene delete failed: " + outError;
    LogSceneAssetWarn("delete failed: " + outError);
    return false;
  }
  movedScene = true;

  if (!metaPath.empty() && std::filesystem::exists(metaPath, ec)) {
    if (!MoveFileSafe(metaPath, trashMetaPath, outError)) {
      if (movedScene) {
        MoveFileBackBestEffort(trashSourcePath, sourcePath);
      }
      outError = "Scene meta trash move failed: " + outError;
      LogSceneAssetWarn("delete failed: " + outError);
      return false;
    }
  }

  // Startup Scene
  // 驛｢・ｧ髮区ｨ抵ｽ朱ｬｮ・ｯ繝ｻ・､驍ｵ・ｺ陷会ｽｱ隨ｳ繝ｻ謦ｻ繝ｻ・ｴ髯ｷ・ｷ陋ｹ・ｻ郢晢ｽｻ驍ｵ・ｲ繝ｻ・｣rojectSettings
  // 驍ｵ・ｺ繝ｻ・ｮ髯ｷ・ｿ郢ｧ蟲ｨ繝ｻ驛｢・ｧ郢ｧ繝ｻ繝ｻ髫ｴ蠑ｱ・・ｫ頑･｢譽碑ｬ費ｽｶ隨倥・・ｸ・ｲ郢晢ｽｻ
  ProjectSettingsService settings{};
  settings.Load(assetDatabase.GetProjectRoot());
  if (settings.GetSettings().startupSceneGuid == record.guid) {
    if (settings.SetStartupSceneGuid(AssetGuid{}) && settings.Save()) {
      outClearedStartupScene = true;
    } else {
      LogSceneAssetWarn(
          "delete warning: startupSceneGuid could not be cleared");
    }
  }

  LogSceneAssetInfo("deleted to trash: " + sourcePath.generic_string() +
                    " -> " + trashDirectory.generic_string());
  return true;
}

} // namespace HIKARI::EDITOR::ASSET_BROWSER
