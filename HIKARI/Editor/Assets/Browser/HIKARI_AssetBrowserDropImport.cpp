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

bool EndsWithCaseInsensitive(const std::string &value,
                             std::string_view suffix) {
  if (suffix.size() > value.size()) {
    return false;
  }
  const std::string tail =
      TEXT::ToLowerAsciiCopy(value.substr(value.size() - suffix.size()));
  return tail == TEXT::ToLowerAsciiCopy(std::string(suffix));
}

bool IsAssetsRootPath(const std::filesystem::path &path) {
  return TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string()) ==
         "assets";
}

bool IsSupportedImportSource(const std::filesystem::path &path) {
  return ASSETS::SEMANTICS::ClassifyAssetTypeFromPath(path) !=
         AssetType::Unknown;
}

std::filesystem::path
SuggestedTargetDirectory(const std::filesystem::path &currentDirectory,
                         const std::filesystem::path &sourcePath) {

  if (!currentDirectory.empty() && !IsAssetsRootPath(currentDirectory)) {
    return currentDirectory;
  }

  const std::string filename =
      TEXT::ToLowerAsciiCopy(sourcePath.filename().string());
  const std::string extension =
      TEXT::ToLowerAsciiCopy(sourcePath.extension().string());
  AssetType type = ASSETS::SEMANTICS::ClassifyAssetTypeFromPath(sourcePath);
  if (type == AssetType::Texture && extension == ".dds" &&
      (filename.find("sky") != std::string::npos ||
       filename.find("cube") != std::string::npos ||
       filename.find("cubemap") != std::string::npos)) {
    type = AssetType::Sky;
  }

  if (type == AssetType::Model) {
    return "Assets/Models";
  }
  if (type == AssetType::Scene) {
    return "Assets/Scenes";
  }
  if (type == AssetType::Material) {
    return "Assets/Materials";
  }
  if (type == AssetType::VfxEffect) {
    return "Assets/Vfx";
  }
  if (type == AssetType::Sky) {
    return "Assets/Skies";
  }
  if (type == AssetType::Sequence) {
    return "Assets/Sequences";
  }
  if (type == AssetType::AnimationStateMachine) {
    return "Assets/Animations";
  }
  return "Assets/Textures";
}

bool IsPathInside(const std::filesystem::path &path,
                  const std::filesystem::path &directory) {
  std::error_code ec{};
  const std::filesystem::path relative =
      std::filesystem::relative(path, directory, ec);
  if (ec || relative.empty()) {
    return false;
  }
  const std::string native = relative.generic_string();
  return native != "." && native.find("..") != 0;
}

std::filesystem::path
MakeUniqueFilePath(const std::filesystem::path &absolutePath) {
  std::error_code ec{};
  if (!std::filesystem::exists(absolutePath, ec)) {
    return absolutePath;
  }

  const std::filesystem::path parent = absolutePath.parent_path();
  const std::string stem = absolutePath.stem().string();
  const std::string extension = absolutePath.extension().string();
  for (int i = 1; i < 10000; ++i) {
    std::filesystem::path candidate =
        parent / (stem + "_" + std::to_string(i) + extension);
    ec.clear();
    if (!std::filesystem::exists(candidate, ec)) {
      return candidate;
    }
  }
  return parent / (stem + "_9999" + extension);
}

bool CopySourceFileIntoProject(const AssetDatabase &assetDatabase,
                               const std::filesystem::path &sourceFile,
                               const std::filesystem::path &targetRelativePath,
                               bool preserveCompanionName,
                               std::filesystem::path &outRelativePath,
                               std::string &outError) {

  std::error_code ec{};
  const std::filesystem::path sourceAbsolute =
      std::filesystem::absolute(sourceFile, ec).lexically_normal();
  if (ec) {
    outError = "Failed to resolve source path: " + ec.message();
    return false;
  }

  if (IsPathInside(sourceAbsolute, assetDatabase.GetAssetsRoot())) {
    outRelativePath = std::filesystem::relative(
                          sourceAbsolute, assetDatabase.GetProjectRoot(), ec)
                          .lexically_normal();
    if (ec) {
      outError = "Failed to make source path project-relative: " + ec.message();
      return false;
    }
    return true;
  }

  std::filesystem::path destinationAbsolute =
      assetDatabase.GetProjectRoot() / targetRelativePath;
  destinationAbsolute = destinationAbsolute.lexically_normal();
  if (!preserveCompanionName) {
    destinationAbsolute = MakeUniqueFilePath(destinationAbsolute);
  }
  std::filesystem::create_directories(destinationAbsolute.parent_path(), ec);
  if (ec) {
    outError = "Failed to create target directory: " + ec.message();
    return false;
  }

  const std::filesystem::copy_options copyOptions =
      preserveCompanionName ? std::filesystem::copy_options::overwrite_existing
                            : std::filesystem::copy_options::none;
  std::filesystem::copy_file(sourceAbsolute, destinationAbsolute, copyOptions,
                             ec);
  if (ec) {
    outError = "Failed to copy " + sourceAbsolute.generic_string() + ": " +
               ec.message();
    return false;
  }

  outRelativePath = std::filesystem::relative(
                        destinationAbsolute, assetDatabase.GetProjectRoot(), ec)
                        .lexically_normal();
  if (ec) {
    outError = "Failed to make imported path project-relative: " + ec.message();
    return false;
  }
  return true;
}

void CollectDroppedFiles(
    const std::filesystem::path &droppedPath,
    const std::filesystem::path &currentDirectory,
    const AssetDatabase &assetDatabase,
    std::vector<std::filesystem::path> &outProjectRelativeFiles,
    int &copiedCompanionCount, int &skippedCount, std::string &lastError) {

  std::error_code ec{};
  if (std::filesystem::is_directory(droppedPath, ec)) {
    const std::filesystem::path targetRoot =
        (currentDirectory.empty() || IsAssetsRootPath(currentDirectory))
            ? std::filesystem::path("Assets") / droppedPath.filename()
            : currentDirectory / droppedPath.filename();

    std::filesystem::recursive_directory_iterator it(
        droppedPath, std::filesystem::directory_options::skip_permission_denied,
        ec);
    const std::filesystem::recursive_directory_iterator end{};
    if (ec) {
      lastError = "Failed to read dropped folder: " + ec.message();
      ++skippedCount;
      return;
    }

    for (; it != end; it.increment(ec)) {
      if (ec) {
        lastError =
            "Failed to continue reading dropped folder: " + ec.message();
        ec.clear();
        ++skippedCount;
        continue;
      }

      const std::filesystem::directory_entry entry = *it;
      std::error_code entryEc{};
      if (!entry.is_regular_file(entryEc)) {
        ++skippedCount;
        continue;
      }

      const std::filesystem::path entryPath = entry.path();
      const bool supportedAsset = IsSupportedImportSource(entryPath);
      const bool copyOnlySidecar =
          ASSETS::SEMANTICS::IsAssetCompanionSource(entryPath);
      if (!supportedAsset && !copyOnlySidecar) {
        ++skippedCount;
        continue;
      }

      std::filesystem::path relativeInside =
          std::filesystem::relative(entryPath, droppedPath, entryEc);
      if (entryEc) {
        ++skippedCount;
        continue;
      }
      std::filesystem::path copiedRelative{};
      const std::filesystem::path targetRelative =
          (targetRoot / relativeInside).lexically_normal();
      if (CopySourceFileIntoProject(assetDatabase, entryPath, targetRelative,
                                    copyOnlySidecar, copiedRelative,
                                    lastError)) {
        if (supportedAsset) {
          outProjectRelativeFiles.push_back(copiedRelative);
        } else {
          ++copiedCompanionCount;
        }
      } else {
        ++skippedCount;
      }
    }
    return;
  }

  if (ec) {
    lastError = "Failed to inspect dropped path: " + ec.message();
    ++skippedCount;
    return;
  }

  const bool regularFile = std::filesystem::is_regular_file(droppedPath, ec);
  if (ec) {
    lastError = "Failed to inspect dropped file: " + ec.message();
    ++skippedCount;
    return;
  }

  const bool supportedAsset = IsSupportedImportSource(droppedPath);
  const bool companionSource =
      ASSETS::SEMANTICS::IsAssetCompanionSource(droppedPath);
  if (!regularFile || (!supportedAsset && !companionSource)) {
    ++skippedCount;
    return;
  }

  const std::filesystem::path targetDirectory =
      companionSource &&
              (currentDirectory.empty() || IsAssetsRootPath(currentDirectory))
          ? std::filesystem::path("Assets/Models")
          : SuggestedTargetDirectory(currentDirectory, droppedPath);
  const std::filesystem::path targetRelative =
      (targetDirectory / droppedPath.filename()).lexically_normal();
  std::filesystem::path copiedRelative{};
  if (CopySourceFileIntoProject(assetDatabase, droppedPath, targetRelative,
                                companionSource, copiedRelative, lastError)) {
    if (supportedAsset) {
      outProjectRelativeFiles.push_back(copiedRelative);
    } else {
      ++copiedCompanionCount;
    }
  } else {
    ++skippedCount;
  }
}

void ProcessDroppedFiles(AssetDatabase &assetDatabase,
                         const std::filesystem::path &currentDirectory,
                         EditorSelection &selection,
                         std::string &lastOperationMessage) {

  std::vector<std::filesystem::path> droppedFiles =
      PLATFORM::ConsumeDroppedFiles();
  if (droppedFiles.empty()) {
    return;
  }

  std::vector<std::filesystem::path> copiedFiles;
  int copiedCompanionCount = 0;
  int skippedCount = 0;
  std::string lastError{};
  for (const std::filesystem::path &dropped : droppedFiles) {
    try {
      CollectDroppedFiles(dropped, currentDirectory, assetDatabase, copiedFiles,
                          copiedCompanionCount, skippedCount, lastError);
    } catch (const std::exception &ex) {
      ++skippedCount;
      lastError = std::string("Drop failed: ") + ex.what();
      HIKARI_LOG_ERROR("[AssetBrowser][Drop] " + lastError);
    } catch (...) {
      ++skippedCount;
      lastError = "Drop failed: unknown exception";
      HIKARI_LOG_ERROR("[AssetBrowser][Drop] " + lastError);
    }
  }

  if (copiedFiles.empty()) {
    if (copiedCompanionCount > 0) {
      assetDatabase.ScanAssets(false);
      lastOperationMessage =
          "Copied " + std::to_string(copiedCompanionCount) +
          " companion file(s); hidden from the Asset Browser";
    } else {
      lastOperationMessage = lastError.empty()
                                 ? "Drop ignored: no supported asset files"
                                 : lastError;
    }
    return;
  }

  assetDatabase.ScanAssets(true);

  std::vector<AssetGuid> importGuids;
  importGuids.reserve(copiedFiles.size());
  std::filesystem::path firstRelativePath{};
  for (const std::filesystem::path &relativePath : copiedFiles) {
    const AssetRecord *record = assetDatabase.FindByPath(relativePath);
    if (!record) {
      continue;
    }
    if (firstRelativePath.empty()) {
      firstRelativePath = relativePath;
    }
    importGuids.push_back(record->guid);
  }

  const bool importQueued =
      assetDatabase.QueueImportAssets(importGuids, "Dropped assets");
  if (!firstRelativePath.empty()) {
    if (const AssetRecord *refreshed =
            assetDatabase.FindByPath(firstRelativePath)) {
      SelectRecord(*refreshed, selection);
    }
  }

  lastOperationMessage =
      "Dropped " + std::to_string(copiedFiles.size()) +
      (importQueued ? " file(s); background import queued"
                    : " file(s); another import batch is already running");
  if (copiedCompanionCount > 0) {
    lastOperationMessage += ", copied " + std::to_string(copiedCompanionCount) +
                            " hidden companion file(s)";
  }
  if (skippedCount > 0) {
    lastOperationMessage += ", skipped " + std::to_string(skippedCount);
  }
}

} // namespace HIKARI::EDITOR::ASSET_BROWSER
