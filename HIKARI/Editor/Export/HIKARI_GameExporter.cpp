#include "HIKARI_GameExporter.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Export/HIKARI_GameExecutableIcon.h"

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
#include "Editor/Export/HIKARI_GameExportPublication.h"
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
        constexpr const char* kProjectCookManifestPath =
            "ProjectSettings/game_export.json";

        struct ProjectCookManifest {
            GameExportIdentity identity;
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

            if (!assetDatabase.ScanAssets(false)) {
                return {};
            }

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

            if (!options.identity->windowTitle.empty()) {
                runtime["windowTitle"] = options.identity->windowTitle;
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
                if (!ParseGameExportIdentity(root, outManifest.identity, outMessage)) return false;
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
        if (!effectiveOptions.identity) effectiveOptions.identity = cookManifest.identity;
        const auto& identity = *effectiveOptions.identity;
        if (!ValidateGameExportIdentity(projectRoot, identity, cookManifestError))
            return {false, {}, cookManifestError};
        ProjectSettingsService projectSettings{};
        if (projectSettings.Load(projectRoot)) {
            if (effectiveOptions.startupSceneGuid.empty() &&
                IsValidAssetGuid(
                    projectSettings.GetSettings().startupSceneGuid.value)) {
                effectiveOptions.startupSceneGuid =
                    projectSettings.GetSettings().startupSceneGuid.value;
            }
        }
        effectiveOptions.sceneGuids.insert(
            effectiveOptions.sceneGuids.end(),
            cookManifest.sceneGuids.begin(),
            cookManifest.sceneGuids.end());
        if (!effectiveOptions.startupSceneGuid.empty() &&
            !IsValidAssetGuid(effectiveOptions.startupSceneGuid)) {
            return {
                false,
                {},
                "Startup Scene GUID must be 32 hexadecimal characters."
            };
        }
        for (const std::string& sceneGuid :
             effectiveOptions.sceneGuids) {
            if (!IsValidAssetGuid(sceneGuid)) {
                return {
                    false,
                    {},
                    "Export Scene GUID must be 32 hexadecimal characters: " +
                        sceneGuid
                };
            }
        }
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
                effectiveOptions.identity->executableName,
                outputExecutableFileName,
                executableNameError)) {
            return {
                false,
                outputDirectory,
                std::move(executableNameError),
            };
        }
        effectiveOptions.identity->executableName =
            TEXT::PathToUtf8(outputExecutableFileName);
        std::filesystem::path sourceExe;
        if (!ResolveGameShippingHostExecutable(projectRoot / "out/install/game-shipping", sourceExe, executableNameError))
            return {false, outputDirectory, executableNameError};
        const std::filesystem::path sourceProjectModule =
            ResolveInstalledProjectModule(
                projectRoot,
                sourceExe.parent_path());

        GameExportResult result{};
        result.outputDirectory = outputDirectory;

        std::string errorMessage{};
        const auto report = [&](const std::string& stage, float fraction) {
            if (effectiveOptions.progress && !effectiveOptions.progress(stage, fraction)) {
                result.message = "Export cancelled. The previous package was preserved.";
                return false;
            }
            return true;
        };
        if (!report("Checking Shipping artifacts", 0.05f)) return result;
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
        RENDER3D::UPSCALING::StreamlineDeploymentOptions
            streamlineDeployment{
                RENDER3D::IsDlaaAntiAliasingMode(
                    renderQuality.antiAliasingMode) ||
                    RENDER3D::IsDlssAntiAliasingMode(
                        renderQuality.antiAliasingMode),
                renderQuality.frameGenerationMode ==
                    RENDER3D::RenderFrameGenerationMode::Dlss,
            };

        streamlineDeployment.requireDlss = streamlineDeployment.requireDlss ||
            std::filesystem::exists(sourceExe.parent_path() / "sl.dlss.dll");
        streamlineDeployment.requireFrameGeneration = streamlineDeployment.requireFrameGeneration ||
            std::filesystem::exists(sourceExe.parent_path() / "sl.dlss_g.dll");

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

        GameExportPublication publication;
        if (!publication.Begin(outputDirectory, projectRoot, errorMessage)) {
            result.message = errorMessage;
            return result;
        }
        const auto& stagingDirectory = publication.StagingDirectory();
        const auto outputExe = stagingDirectory / outputExecutableFileName;
        const auto outputProjectModule = stagingDirectory / "HIKARI_GameProject.dll";
        if (!report("Copying Shipping runtime", 0.15f)) return result;

        const NativeRuntimeDeploymentResult nativeRuntime =
            DeployNativeRuntimeDependencies(
                sourceExe.parent_path(),
                stagingDirectory);
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
                stagingDirectory,
                streamlineDeployment,
                errorMessage) ||
            !WriteRuntimeConfig(
                stagingDirectory,
                effectiveOptions,
                renderQuality,
                errorMessage) ||
            !CopyInstalledEngineContent(
                sourceExe.parent_path(),
                stagingDirectory,
                errorMessage)) {
            result.message = errorMessage;
            HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
            return result;
        }

        if (!identity.iconPath.empty() && !EmbedGameExecutableIcon(outputExe,
            projectRoot / TEXT::PathFromUtf8(identity.iconPath), errorMessage)) {
            result.message = errorMessage;
            return result;
        }

        if (!report("Checking and cooking project assets", 0.3f)) return result;
        const ASSETS::COOKING::AssetCookResult cookResult =
            ASSETS::COOKING::AssetCooker{}.Cook({
                projectRoot,
                stagingDirectory / "Content",
                effectiveOptions.startupSceneGuid,
                effectiveOptions.sceneGuids,
                cookManifest.assetRoots,
                cookManifest.runtimePayloadRoots,
                [&](const std::string& stage, float fraction) { return report(stage, 0.3f + 0.5f * fraction); },
            });
        if (!cookResult.success) {
            result.message = cookResult.message;
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") +
                result.message);
            return result;
        }

        if (!report("Validating package and hashing files", 0.85f)) return result;
        const GamePackageManifestOptions packageManifestOptions{
            effectiveOptions.startupSceneGuid,
            effectiveOptions.sceneGuids,
            GameExportWindowModeName(
                effectiveOptions.windowMode),
            effectiveOptions.identity->executableName,
        };
        if (!ValidateGamePackageBoundary(
                stagingDirectory,
                packageManifestOptions,
                errorMessage) ||
            !WriteGamePackageManifest(
                stagingDirectory,
                packageManifestOptions,
                errorMessage)) {
            result.message = errorMessage;
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") +
                result.message);
            return result;
        }

        if (!report("Publishing validated package", 0.98f)) return result;
        if (!publication.Publish(errorMessage)) {
            result.message = errorMessage;
            return result;
        }

        result.success = true;
        if (effectiveOptions.progress) effectiveOptions.progress("Export complete", 1.0f);
        result.message =
            "Exported " +
            std::to_string(effectiveOptions.sceneGuids.size()) +
            " scene(s), " +
            std::to_string(cookResult.assetCount) +
            " cooked asset(s), and " +
            std::to_string(cookResult.fileCount) +
            " cooked file(s) to " +
            TEXT::PathToUtf8(outputDirectory);
        HIKARI_LOG_INFO(std::string("[GameExporter] ") + result.message);
        return result;
#else
        (void)options;
        return { false, {}, "Game export is only available in editor builds." };
#endif
    }

} // namespace HIKARI::EDITOR
