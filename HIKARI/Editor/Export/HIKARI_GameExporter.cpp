#include "HIKARI_GameExporter.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "Assets/Cooking/HIKARI_AssetCooker.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Export/HIKARI_GamePackageManifest.h"
#include "Editor/Export/HIKARI_GameShippingArtifactValidator.h"
#include "Editor/Export/HIKARI_NativeRuntimeDeployment.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"
#include "Render3D/Upscaling/HIKARI_StreamlineDeployment.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr const char* kExportOutputFolderName = "HIKARI_Game";
        constexpr const char* kDefaultExportExecutableName =
            "HIKARI_Game.exe";
        constexpr const char* kProjectCookManifestPath =
            "ProjectSettings/game_export.json";

        struct ProjectCookManifest {
            std::string executableName{};
            std::string windowTitle{};
            std::vector<std::string> sceneGuids{};
            std::vector<std::filesystem::path> assetRoots{};
            std::vector<std::filesystem::path> runtimePayloadRoots{};
        };

        const char* GameExportWindowModeName(
            GameExportWindowMode mode) noexcept {
            switch (mode) {
            case GameExportWindowMode::Windowed:
                return "Windowed";
            case GameExportWindowMode::TopmostFullscreen:
                return "Fullscreen";
            case GameExportWindowMode::BorderlessFullscreen:
            default:
                return "Borderless";
            }
        }

        bool ResolveExportExecutableFileName(
            std::string_view requestedName,
            std::filesystem::path& outFileName,
            std::string& outMessage) {

            const std::string_view name = requestedName.empty()
                ? std::string_view(kDefaultExportExecutableName)
                : requestedName;
            if (name.find_first_of("<>:\"/\\|?*") !=
                    std::string_view::npos ||
                name.back() == '.' || name.back() == ' ') {
                outMessage =
                    "Game export executableName must be one safe filename.";
                return false;
            }
            for (const unsigned char value : name) {
                if (value < 0x20u) {
                    outMessage =
                        "Game export executableName contains a control character.";
                    return false;
                }
            }

            const std::filesystem::path candidate =
                TEXT::PathFromUtf8(name);
            if (candidate.empty() || candidate.is_absolute() ||
                candidate.has_parent_path() ||
                candidate.filename() != candidate ||
                candidate.native().size() > 240u ||
                TEXT::ToLowerAsciiCopy(
                    TEXT::PathToUtf8(candidate.extension())) != ".exe") {
                outMessage =
                    "Game export executableName must be a local .exe filename.";
                return false;
            }

            outFileName = candidate;
            outMessage.clear();
            return true;
        }

        std::string RelativeFileKey(const std::filesystem::path& path) {
            return TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
        }

        std::filesystem::path FindProjectRoot() {
            const ProjectContext& projectContext =
                GetProjectContext();
            if (projectContext.IsOpen()) {
                return projectContext.GetRoot();
            }

            const std::filesystem::path descriptorPath =
                FindProjectDescriptor(
                    std::filesystem::current_path());
            if (!descriptorPath.empty()) {
                return descriptorPath.parent_path();
            }

            return std::filesystem::current_path();
        }

        std::filesystem::path NormalizeExportPath(const std::filesystem::path& path) {
            std::error_code ec{};
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (!ec && !normalized.empty()) {
                return normalized;
            }

            ec.clear();
            normalized = std::filesystem::absolute(path, ec);
            if (!ec && !normalized.empty()) {
                return normalized.lexically_normal();
            }

            return path.lexically_normal();
        }

        std::filesystem::path ResolveOutputDirectory(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& requestedDirectory) {
            if (requestedDirectory.empty()) {
                return NormalizeExportPath(projectRoot / "Build" / "Exports" / kExportOutputFolderName);
            }

            std::filesystem::path outputDirectory = requestedDirectory;
            if (outputDirectory.is_relative()) {
                outputDirectory = projectRoot / outputDirectory;
            }
            return NormalizeExportPath(outputDirectory);
        }

        bool SameExistingPath(const std::filesystem::path& lhs, const std::filesystem::path& rhs) {
            std::error_code ec{};
            const bool same = std::filesystem::equivalent(lhs, rhs, ec);
            return !ec && same;
        }

        bool IsDirectoryEmpty(
            const std::filesystem::path& directory,
            bool& isEmpty,
            std::string& errorMessage) {
            std::error_code ec{};
            std::filesystem::directory_iterator it(directory, ec);
            if (ec) {
                errorMessage = "Could not inspect export directory: " + directory.string();
                return false;
            }

            isEmpty = it == std::filesystem::directory_iterator{};
            return true;
        }

        bool LooksLikeExistingExportDirectory(const std::filesystem::path& directory) {
            std::error_code ec{};
            if (std::filesystem::exists(directory / "HIKARI" / "runtime_config.json", ec) && !ec) {
                return true;
            }

            ec.clear();
            return std::filesystem::exists(directory / "HIKARI_Game.exe", ec) && !ec;
        }

        bool PrepareOutputDirectory(
            const std::filesystem::path& outputDirectory,
            const std::filesystem::path& projectRoot,
            std::string& errorMessage) {
            if (outputDirectory.empty()) {
                errorMessage = "Output directory is empty.";
                return false;
            }

            if (outputDirectory == outputDirectory.root_path()) {
                errorMessage = "Refusing to export to a drive root: " + outputDirectory.string();
                return false;
            }

            if (SameExistingPath(outputDirectory, projectRoot) ||
                SameExistingPath(outputDirectory, projectRoot.parent_path())) {
                errorMessage = "Refusing to export over the project directory: " + outputDirectory.string();
                return false;
            }

            std::error_code ec{};
            const bool exists = std::filesystem::exists(outputDirectory, ec);
            if (ec) {
                errorMessage = "Could not inspect export directory: " + outputDirectory.string();
                return false;
            }

            if (exists) {
                if (!std::filesystem::is_directory(outputDirectory, ec) || ec) {
                    errorMessage = "Output path is not a directory: " + outputDirectory.string();
                    return false;
                }

                bool isEmpty = false;
                if (!IsDirectoryEmpty(outputDirectory, isEmpty, errorMessage)) {
                    return false;
                }

                if (!isEmpty && !LooksLikeExistingExportDirectory(outputDirectory)) {
                    errorMessage =
                        "Output directory is not empty and does not look like a HIKARI export folder: " +
                        outputDirectory.string();
                    return false;
                }

                std::filesystem::remove_all(outputDirectory, ec);
                if (ec) {
                    errorMessage = "Could not clean export directory: " + outputDirectory.string();
                    return false;
                }
            }

            std::filesystem::create_directories(outputDirectory, ec);
            if (ec) {
                errorMessage = "Could not create export directory: " + outputDirectory.string();
                return false;
            }

            return true;
        }

        bool CopyFileChecked(
            const std::filesystem::path& from,
            const std::filesystem::path& to,
            std::string& errorMessage) {
            std::error_code ec{};
            if (!std::filesystem::exists(from, ec) || ec) {
                errorMessage = "Missing file: " + from.string();
                return false;
            }

            std::filesystem::create_directories(to.parent_path(), ec);
            if (ec) {
                errorMessage = "Could not create directory: " + to.parent_path().string();
                return false;
            }

            std::filesystem::copy_file(
                from,
                to,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec) {
                errorMessage = "Could not copy file: " + from.string() + " -> " + to.string();
                return false;
            }
            return true;
        }

        bool CopyDirectoryChecked(
            const std::filesystem::path& from,
            const std::filesystem::path& to,
            std::string& errorMessage,
            bool generatedRuntimeOnly = false,
            const std::unordered_set<std::string>* excludedRelativeFiles = nullptr) {
            std::error_code ec{};
            if (!std::filesystem::exists(from, ec) || ec) {
                return true;
            }

            std::filesystem::create_directories(to, ec);
            if (ec) {
                errorMessage = "Could not create directory: " + to.string();
                return false;
            }

            const std::filesystem::recursive_directory_iterator end{};
            std::filesystem::recursive_directory_iterator it(from, ec);
            if (ec) {
                errorMessage = "Could not iterate directory: " + from.string();
                return false;
            }

            for (; it != end; it.increment(ec)) {
                if (ec) {
                    errorMessage = "Could not iterate directory: " + from.string();
                    return false;
                }

                const std::filesystem::path relativePath = std::filesystem::relative(it->path(), from, ec);
                if (ec) {
                    errorMessage = "Could not compute relative export path: " + it->path().string();
                    return false;
                }

                const std::filesystem::path targetPath = to / relativePath;
                if (it->is_directory(ec)) {
                    if (ec) {
                        errorMessage = "Could not inspect directory: " + it->path().string();
                        return false;
                    }
                    std::filesystem::create_directories(targetPath, ec);
                    if (ec) {
                        errorMessage = "Could not create directory: " + targetPath.string();
                        return false;
                    }
                    continue;
                }

                if (!it->is_regular_file(ec)) {
                    if (ec) {
                        errorMessage = "Could not inspect file: " + it->path().string();
                        return false;
                    }
                    continue;
                }

                if (excludedRelativeFiles &&
                    excludedRelativeFiles->find(RelativeFileKey(relativePath)) != excludedRelativeFiles->end()) {
                    continue;
                }

                if (generatedRuntimeOnly) {
                    const std::string genericPath = TEXT::ToLowerAsciiCopy(relativePath.generic_string());
                    if (genericPath.find("/captures/") != std::string::npos ||
                        genericPath.ends_with("_capture.dds")) {
                        continue;
                    }
                }

                std::filesystem::create_directories(targetPath.parent_path(), ec);
                if (ec) {
                    errorMessage = "Could not create directory: " + targetPath.parent_path().string();
                    return false;
                }

                std::filesystem::copy_file(
                    it->path(),
                    targetPath,
                    std::filesystem::copy_options::overwrite_existing,
                    ec);
                if (ec) {
                    errorMessage = "Could not copy file: " + it->path().string() + " -> " + targetPath.string();
                    return false;
                }
            }
            return true;
        }

        std::vector<GameExportSceneInfo> CollectSceneAssetsFromProjectRoot(
            const std::filesystem::path& projectRoot) {
            AssetDatabase assetDatabase{};
            if (!assetDatabase.Initialize(projectRoot)) {
                return {};
            }

            assetDatabase.ScanAssets(false);

            ProjectSettingsService settings{};
            settings.Load(projectRoot);
            const AssetGuid projectStartupGuid = settings.GetSettings().startupSceneGuid;

            std::vector<const AssetRecord*> records = assetDatabase.CollectByType(AssetType::Scene);
            std::sort(records.begin(), records.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
                if (!lhs || !rhs) {
                    return lhs < rhs;
                }
                return GetAssetRecordDisplayName(*lhs) <
                    GetAssetRecordDisplayName(*rhs);
            });

            std::vector<GameExportSceneInfo> scenes{};
            scenes.reserve(records.size());
            for (const AssetRecord* record : records) {
                if (!record || !record->guid.IsValid()) {
                    continue;
                }

                GameExportSceneInfo info{};
                info.guid = record->guid.value;
                info.displayName = GetAssetRecordDisplayName(*record);
                info.sourcePath = record->sourcePath;
                info.projectStartup = projectStartupGuid.IsValid() && record->guid == projectStartupGuid;
                scenes.push_back(std::move(info));
            }
            return scenes;
        }

        bool ResolveExportRenderQuality(
            const std::filesystem::path& projectRoot,
            const GameExportOptions& options,
            RENDER3D::RenderQualitySettings& outQuality,
            std::string& errorMessage) {
            outQuality = RENDER3D::GetRenderQualitySettings();
            std::string qualityMessage{};
            if (!RENDER3D::LoadRenderQualityProfile(
                    projectRoot,
                    outQuality,
                    &qualityMessage) &&
                !qualityMessage.empty()) {
                errorMessage =
                    "Could not load project render quality for export: " +
                    qualityMessage;
                return false;
            }
            switch (options.windowMode) {
            case GameExportWindowMode::Windowed:
                outQuality.windowMode =
                    RENDER3D::WindowPresentationMode::Windowed;
                break;
            case GameExportWindowMode::TopmostFullscreen:
                outQuality.windowMode =
                    RENDER3D::WindowPresentationMode::Fullscreen;
                break;
            case GameExportWindowMode::BorderlessFullscreen:
            default:
                outQuality.windowMode =
                    RENDER3D::WindowPresentationMode::BorderlessWindow;
                break;
            }
            return true;
        }

        bool WriteRuntimeConfig(
            const std::filesystem::path& outputDirectory,
            const GameExportOptions& options,
            const RENDER3D::RenderQualitySettings& quality,
            std::string& errorMessage) {
            const std::filesystem::path configPath = outputDirectory / "HIKARI" / "runtime_config.json";
            std::error_code ec{};
            std::filesystem::create_directories(configPath.parent_path(), ec);
            if (ec) {
                errorMessage = "Could not create runtime config directory: " + configPath.parent_path().string();
                return false;
            }

            RENDER3D::RenderResolution windowResolution =
                RENDER3D::ResolveFixedRenderResolution(quality.windowSize);
            if (windowResolution.width <= 0 || windowResolution.height <= 0) {
                windowResolution = { 1280, 720 };
            }

            nlohmann::json runtime{
                    { "hostMode", RuntimeHostModeName(RuntimeHostMode::ExportedGame) },
                    { "projectRoot", "../Content" },
                    { "enableImGui", false },
                    { "enableEditorUI", false },
                    { "enableDebugLayer", false },
                    { "enableDebugCamera", false },
                    { "resizableWindow",
                        quality.windowMode ==
                        RENDER3D::WindowPresentationMode::Windowed },
                    { "windowWidth", windowResolution.width },
                    { "windowHeight", windowResolution.height },
                    { "renderQuality",
                        RENDER3D::SerializeRenderQualitySettings(quality) },
            };

            if (!options.windowTitle.empty()) {
                runtime["windowTitle"] = options.windowTitle;
            }

            if (!options.startupSceneGuid.empty()) {
                runtime["startupSceneGuid"] = options.startupSceneGuid;
            }
            if (!options.sceneGuids.empty()) {
                runtime["exportedSceneGuids"] = options.sceneGuids;
            }

            const nlohmann::json root{
                { "runtime", std::move(runtime) }
            };

            std::ofstream ofs(configPath);
            if (!ofs) {
                errorMessage = "Could not write runtime config: " + configPath.string();
                return false;
            }
            ofs << root.dump(4);
            return true;
        }

        bool CopyInstalledEngineContent(
            const std::filesystem::path& runtimeRoot,
            const std::filesystem::path& outputDirectory,
            std::string& errorMessage) {

            const std::filesystem::path engineContentSource =
                runtimeRoot / "EngineContent";
            std::error_code engineContentError{};
            if (!std::filesystem::is_directory(
                    engineContentSource,
                    engineContentError) ||
                engineContentError) {
                errorMessage =
                    "Installed EngineContent is missing: " +
                    engineContentSource.string();
                return false;
            }
            std::unordered_set<std::string> sourceOnlyExclusions{};
            std::error_code scanError{};
            std::filesystem::recursive_directory_iterator iterator(
                engineContentSource,
                std::filesystem::directory_options::skip_permission_denied,
                scanError);
            const std::filesystem::recursive_directory_iterator end{};
            for (; !scanError && iterator != end;
                 iterator.increment(scanError)) {
                if (!iterator->is_regular_file(scanError)) {
                    scanError.clear();
                    continue;
                }
                const std::filesystem::path relative =
                    std::filesystem::relative(
                        iterator->path(), engineContentSource, scanError);
                if (scanError) {
                    break;
                }
                const std::string key = RelativeFileKey(relative);
                const bool sandSource =
                    key.rfind("sand/textures/", 0u) == 0u &&
                    key.rfind("sand/textures/cooked/", 0u) != 0u;
                const bool terrainSource =
                    key.rfind("terrain/sources/", 0u) == 0u ||
                    (key.rfind("terrain/textures/", 0u) == 0u &&
                     key.rfind("terrain/textures/cooked/", 0u) != 0u);
                const bool vfxTextureSource =
                    key.rfind("vfx/textures/", 0u) == 0u &&
                    key.rfind("vfx/textures/cooked/", 0u) != 0u;
                if (sandSource || terrainSource || vfxTextureSource ||
                    key == "terrain/readme.md") {
                    sourceOnlyExclusions.insert(key);
                }
            }
            if (scanError) {
                errorMessage =
                    "Could not inspect installed EngineContent: " +
                    scanError.message();
                return false;
            }
            if (!CopyDirectoryChecked(
                    engineContentSource,
                    outputDirectory / "EngineContent",
                    errorMessage,
                    false,
                    &sourceOnlyExclusions)) {
                return false;
            }
            return true;
        }

        bool LoadProjectCookManifest(
            const std::filesystem::path& projectRoot,
            ProjectCookManifest& outManifest,
            std::string& outMessage) {
            outManifest = {};
            outMessage.clear();
            const std::filesystem::path manifestPath =
                projectRoot / kProjectCookManifestPath;
            std::error_code ec{};
            if (!std::filesystem::exists(manifestPath, ec) || ec) {
                return true;
            }
            try {
                std::ifstream stream(manifestPath, std::ios::binary);
                if (!stream) {
                    outMessage =
                        "Could not open project game export manifest: " +
                        manifestPath.generic_string();
                    return false;
                }
                nlohmann::json root{};
                stream >> root;
                if (!root.is_object() || root.value("version", 0) != 1) {
                    outMessage =
                        "Project game export manifest must use version 1: " +
                        manifestPath.generic_string();
                    return false;
                }
                const auto windowTitle = root.find("windowTitle");
                if (windowTitle != root.end()) {
                    if (!windowTitle->is_string() ||
                        windowTitle->get_ref<const std::string&>().empty()) {
                        outMessage =
                            "Project game export manifest contains an invalid windowTitle: " +
                            manifestPath.generic_string();
                        return false;
                    }
                    outManifest.windowTitle =
                        windowTitle->get_ref<const std::string&>();
                }
                const auto executableName = root.find("executableName");
                if (executableName != root.end()) {
                    if (!executableName->is_string() ||
                        executableName->get_ref<const std::string&>().empty()) {
                        outMessage =
                            "Project game export manifest contains an invalid executableName: " +
                            manifestPath.generic_string();
                        return false;
                    }
                    outManifest.executableName =
                        executableName->get_ref<const std::string&>();
                }
                const auto readStrings = [&](const char* key,
                                             auto&& visitor) {
                    const auto found = root.find(key);
                    if (found == root.end()) {
                        return true;
                    }
                    if (!found->is_array()) {
                        return false;
                    }
                    for (const auto& node : *found) {
                        if (!node.is_string() || node.get_ref<
                                const std::string&>().empty()) {
                            return false;
                        }
                        visitor(node.get_ref<const std::string&>());
                    }
                    return true;
                };
                if (!readStrings(
                        "sceneGuids",
                        [&](const std::string& value) {
                            outManifest.sceneGuids.push_back(value);
                        }) ||
                    !readStrings(
                        "assetRoots",
                        [&](const std::string& value) {
                            outManifest.assetRoots.emplace_back(value);
                        }) ||
                    !readStrings(
                        "runtimePayloadRoots",
                        [&](const std::string& value) {
                            outManifest.runtimePayloadRoots.emplace_back(
                                value);
                        })) {
                    outMessage =
                        "Project game export manifest contains an invalid list: " +
                        manifestPath.generic_string();
                    return false;
                }
                for (const std::string& guid : outManifest.sceneGuids) {
                    if (!AssetGuid{ guid }.IsValid()) {
                        outMessage =
                            "Project game export manifest contains an invalid scene GUID: " +
                            guid;
                        return false;
                    }
                }
                return true;
            }
            catch (const std::exception& error) {
                outMessage =
                    "Could not parse project game export manifest: " +
                    std::string(error.what());
                return false;
            }
        }

        std::filesystem::path ResolveInstalledProjectModule(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path&) {
            // Formal exports consume only the installed project product.
            // Library/build and engine-directory fallbacks can silently mix
            // incompatible SDK or stale gameplay binaries.
            return projectRoot / "out" / "install" / "game-shipping" /
                "HIKARI_GameProject.dll";
        }
#endif
    }

    std::filesystem::path GameExporter::GetDefaultOutputDirectory() {
#if defined(HIKARI_WITH_EDITOR)
        const std::filesystem::path projectRoot = FindProjectRoot();
        return ResolveOutputDirectory(projectRoot, {});
#else
        return {};
#endif
    }

    std::vector<GameExportSceneInfo> GameExporter::CollectSceneAssets() {
#if defined(HIKARI_WITH_EDITOR)
        return CollectSceneAssetsFromProjectRoot(FindProjectRoot());
#else
        return {};
#endif
    }

    GameExportResult GameExporter::Export() {
        return Export(GameExportOptions{});
    }

    GameExportResult GameExporter::Export(const GameExportOptions& options) {
#if defined(HIKARI_WITH_EDITOR)
        const std::filesystem::path projectRoot = FindProjectRoot();
        GameExportOptions effectiveOptions = options;
        ProjectCookManifest cookManifest{};
        std::string cookManifestError{};
        if (!LoadProjectCookManifest(
                projectRoot,
                cookManifest,
                cookManifestError)) {
            GameExportResult failure{};
            failure.message = cookManifestError;
            return failure;
        }
        if (effectiveOptions.windowTitle.empty()) {
            effectiveOptions.windowTitle = cookManifest.windowTitle;
        }
        if (effectiveOptions.executableFileName.empty()) {
            effectiveOptions.executableFileName =
                cookManifest.executableName;
        }
        ProjectSettingsService projectSettings{};
        if (projectSettings.Load(projectRoot)) {
            if (effectiveOptions.startupSceneGuid.empty() &&
                projectSettings.GetSettings().startupSceneGuid.IsValid()) {
                effectiveOptions.startupSceneGuid =
                    projectSettings.GetSettings().startupSceneGuid.value;
            }
        }
        effectiveOptions.sceneGuids.insert(
            effectiveOptions.sceneGuids.end(),
            cookManifest.sceneGuids.begin(),
            cookManifest.sceneGuids.end());
        if (effectiveOptions.sceneGuids.empty() &&
            !effectiveOptions.startupSceneGuid.empty()) {
            // Export() is also used by non-UI callers.  Resolve the canonical
            // project startup scene here so a first headless export cooks a
            // deterministic closure instead of silently selecting every
            // authored asset.
            effectiveOptions.sceneGuids.push_back(
                effectiveOptions.startupSceneGuid);
        }
        if (!effectiveOptions.startupSceneGuid.empty() &&
            std::find(
                effectiveOptions.sceneGuids.begin(),
                effectiveOptions.sceneGuids.end(),
                effectiveOptions.startupSceneGuid) ==
                effectiveOptions.sceneGuids.end()) {
            // The runtime cannot start from a scene omitted from the cooked
            // selection.  Keep an explicit story/battle selection valid by
            // adding the canonical startup scene before sorting/deduping.
            effectiveOptions.sceneGuids.push_back(
                effectiveOptions.startupSceneGuid);
        }
        std::sort(
            effectiveOptions.sceneGuids.begin(),
            effectiveOptions.sceneGuids.end());
        effectiveOptions.sceneGuids.erase(
            std::unique(
                effectiveOptions.sceneGuids.begin(),
                effectiveOptions.sceneGuids.end()),
            effectiveOptions.sceneGuids.end());
        const std::filesystem::path outputDirectory =
            ResolveOutputDirectory(projectRoot, options.outputDirectory);
        std::filesystem::path outputExecutableFileName{};
        std::string executableNameError{};
        if (!ResolveExportExecutableFileName(
                effectiveOptions.executableFileName,
                outputExecutableFileName,
                executableNameError)) {
            return {
                false,
                outputDirectory,
                std::move(executableNameError),
            };
        }
        effectiveOptions.executableFileName =
            TEXT::PathToUtf8(outputExecutableFileName);
        const std::filesystem::path sourceExe =
            projectRoot / "out" / "install" / "game-shipping" /
                outputExecutableFileName;
        const std::filesystem::path sourceProjectModule =
            ResolveInstalledProjectModule(
                projectRoot,
                sourceExe.parent_path());
        const std::filesystem::path outputExe =
            outputDirectory / outputExecutableFileName;
        const std::filesystem::path outputProjectModule =
            outputDirectory /
            "HIKARI_GameProject.dll";
        GameExportResult result{};
        result.outputDirectory = outputDirectory;

        std::string errorMessage{};
        RENDER3D::RenderQualitySettings renderQuality{};
        if (!ResolveExportRenderQuality(
                projectRoot,
                effectiveOptions,
                renderQuality,
                errorMessage)) {
            result.message = errorMessage;
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") + result.message);
            return result;
        }
        const RENDER3D::UPSCALING::StreamlineDeploymentOptions
            streamlineDeployment{
                RENDER3D::IsDlaaAntiAliasingMode(
                    renderQuality.antiAliasingMode) ||
                    RENDER3D::IsDlssAntiAliasingMode(
                        renderQuality.antiAliasingMode),
                renderQuality.frameGenerationMode ==
                    RENDER3D::RenderFrameGenerationMode::Dlss,
            };

        std::error_code sourceExeError{};
        if (!std::filesystem::is_regular_file(sourceExe, sourceExeError) ||
            sourceExeError) {
            result.message =
                "Installed Shipping runtime is missing: " +
                sourceExe.string() +
                ". Run .\\Tools\\Build\\Build-Hikari.ps1 "
                "-Preset game-shipping -Install before exporting.";
            HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
            return result;
        }
        sourceExeError.clear();
        if (!std::filesystem::is_regular_file(
                sourceProjectModule,
                sourceExeError) ||
            sourceExeError) {
            result.message =
                "Installed GameProject module is missing: " +
                sourceProjectModule.string() +
                ". Run .\\Tools\\Build\\Build-Hikari.ps1 "
                "-Preset game-shipping -Project \"" +
                projectRoot.string() +
                "\" -Install before exporting.";
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") +
                result.message);
            return result;
        }
        if (!ValidateGameShippingArtifacts(
                projectRoot,
                sourceExe,
                sourceProjectModule,
                errorMessage)) {
            result.message = errorMessage;
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") + result.message);
            return result;
        }

        if (!PrepareOutputDirectory(outputDirectory, projectRoot, errorMessage)) {
            result.message = errorMessage;
            HIKARI_LOG_WARN(result.message);
            return result;
        }

        const NativeRuntimeDeploymentResult nativeRuntime =
            DeployNativeRuntimeDependencies(
                sourceExe.parent_path(),
                outputDirectory);
        if (!nativeRuntime.success) {
            result.message = nativeRuntime.message;
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") +
                result.message);
            return result;
        }
        if (!CopyFileChecked(
                sourceExe,
                outputExe,
                errorMessage) ||
            !CopyFileChecked(
                sourceProjectModule,
                outputProjectModule,
                errorMessage) ||
            !RENDER3D::UPSCALING::DeployStreamlineRuntime(
                sourceExe.parent_path(),
                outputDirectory,
                streamlineDeployment,
                errorMessage) ||
            !WriteRuntimeConfig(
                outputDirectory,
                effectiveOptions,
                renderQuality,
                errorMessage) ||
            !CopyInstalledEngineContent(
                sourceExe.parent_path(),
                outputDirectory,
                errorMessage)) {
            result.message = errorMessage;
            HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
            return result;
        }

        const ASSETS::COOKING::AssetCookResult cookResult =
            ASSETS::COOKING::AssetCooker{}.Cook({
                projectRoot,
                outputDirectory / "Content",
                effectiveOptions.startupSceneGuid,
                effectiveOptions.sceneGuids,
                cookManifest.assetRoots,
                cookManifest.runtimePayloadRoots,
            });
        if (!cookResult.success) {
            result.message = cookResult.message;
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") +
                result.message);
            return result;
        }

        const GamePackageManifestOptions packageManifestOptions{
            effectiveOptions.startupSceneGuid,
            effectiveOptions.sceneGuids,
            GameExportWindowModeName(
                effectiveOptions.windowMode),
            effectiveOptions.executableFileName,
        };
        if (!ValidateGamePackageBoundary(
                outputDirectory,
                packageManifestOptions,
                errorMessage) ||
            !WriteGamePackageManifest(
                outputDirectory,
                packageManifestOptions,
                errorMessage)) {
            result.message = errorMessage;
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") +
                result.message);
            return result;
        }

        result.success = true;
        result.message =
            "Exported " +
            std::to_string(effectiveOptions.sceneGuids.size()) +
            " scene(s), " +
            std::to_string(cookResult.assetCount) +
            " cooked asset(s), and " +
            std::to_string(cookResult.fileCount) +
            " cooked file(s) to " +
            outputDirectory.string();
        HIKARI_LOG_INFO(std::string("[GameExporter] ") + result.message);
        return result;
#else
        (void)options;
        return { false, {}, "Game export is only available in editor builds." };
#endif
    }

} // namespace HIKARI::EDITOR
