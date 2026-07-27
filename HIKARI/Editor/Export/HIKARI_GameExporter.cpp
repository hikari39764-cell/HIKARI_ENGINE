#include "HIKARI_GameExporter.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <fstream>
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
#include "Editor/Export/HIKARI_GamePackageManifest.h"
#include "Editor/Export/HIKARI_NativeRuntimeDeployment.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Render3D/Upscaling/HIKARI_StreamlineDeployment.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr const char* kExportOutputFolderName = "HIKARI_Game";

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

        bool WriteRuntimeConfig(
            const std::filesystem::path& outputDirectory,
            const GameExportOptions& options,
            std::string& errorMessage) {
            const std::filesystem::path configPath = outputDirectory / "HIKARI" / "runtime_config.json";
            std::error_code ec{};
            std::filesystem::create_directories(configPath.parent_path(), ec);
            if (ec) {
                errorMessage = "Could not create runtime config directory: " + configPath.parent_path().string();
                return false;
            }

            const RENDER3D::RenderQualitySettings& quality =
                RENDER3D::GetRenderQualitySettings();
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
            if (!CopyDirectoryChecked(
                    engineContentSource,
                    outputDirectory / "EngineContent",
                    errorMessage)) {
                return false;
            }
            return true;
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
        const std::filesystem::path sourceExe =
            projectRoot / "out" / "install" / "game-shipping" / "HIKARI_Engine.exe";
        const std::filesystem::path sourceProjectModule =
            sourceExe.parent_path() /
            "HIKARI_GameProject.dll";
        const std::filesystem::path outputDirectory =
            ResolveOutputDirectory(projectRoot, options.outputDirectory);
        const std::filesystem::path outputExe = outputDirectory / "HIKARI_Game.exe";
        const std::filesystem::path outputProjectModule =
            outputDirectory /
            "HIKARI_GameProject.dll";
        const RENDER3D::RenderQualitySettings& renderQuality =
            RENDER3D::GetRenderQualitySettings();
        const RENDER3D::UPSCALING::StreamlineDeploymentOptions
            streamlineDeployment{
                RENDER3D::IsDlaaAntiAliasingMode(
                    renderQuality.antiAliasingMode) ||
                    RENDER3D::IsDlssAntiAliasingMode(
                        renderQuality.antiAliasingMode),
                renderQuality.frameGenerationMode ==
                    RENDER3D::RenderFrameGenerationMode::Dlss,
            };

        GameExportResult result{};
        result.outputDirectory = outputDirectory;

        std::string errorMessage{};
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
                options,
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
                options.startupSceneGuid,
                options.sceneGuids,
            });
        if (!cookResult.success) {
            result.message = cookResult.message;
            HIKARI_LOG_WARN(
                std::string("[GameExporter] ") +
                result.message);
            return result;
        }

        if (!ValidateGamePackageBoundary(
                outputDirectory,
                errorMessage) ||
            !WriteGamePackageManifest(
                outputDirectory,
                {
                    options.startupSceneGuid,
                    options.sceneGuids,
                },
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
            std::to_string(options.sceneGuids.size()) +
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
