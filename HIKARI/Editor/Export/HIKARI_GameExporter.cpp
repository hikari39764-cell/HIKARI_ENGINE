#include "HIKARI_GameExporter.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <deque>
#include <fstream>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json.hpp>

#if defined(HIKARI_WITH_EDITOR)
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/Export/HIKARI_NativeRuntimeDeployment.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Render3D/Upscaling/HIKARI_StreamlineDeployment.h"
#include "Runtime/HIKARI_RuntimeHost.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
        struct ExportRecipe {
            const char* outputFolderName = nullptr;
            const char* sourceConfiguration = nullptr;
            const char* hostMode = nullptr;
            bool enableImGui = false;
            bool enablePortableObjectTools = false;
        };

        struct ExportContentManifest {
            bool restrictToSelectedScenes = false;
            std::unordered_set<std::string> selectedSceneGuids{};
            std::unordered_set<std::string> requiredAssetGuids{};
            std::vector<AssetMissingReference> missingReferences{};
        };

        ExportRecipe RecipeFor(GameExportTarget target) {
            switch (target) {
            case GameExportTarget::GameWithObjectTools:
                return {
                    "HIKARI_Game_WithObjectTools",
                    "Profile",
                    RuntimeHostModeName(RuntimeHostMode::ExportedGameWithTools),
                    true,
                    true
                };
            case GameExportTarget::Game:
            default:
                return {
                    "HIKARI_Game",
                    "Shipping",
                    RuntimeHostModeName(RuntimeHostMode::ExportedGame),
                    false,
                    false
                };
            }
        }


        std::string RelativeFileKey(const std::filesystem::path& path) {
            return TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
        }

        bool LooksLikeProjectRoot(const std::filesystem::path& path) {
            std::error_code ec{};
            return std::filesystem::exists(path / "HIKARI_UpdateTo3D.vcxproj", ec) && !ec;
        }

        std::filesystem::path FindProjectRoot() {
            std::filesystem::path current = std::filesystem::current_path();
            for (int i = 0; i < 6 && !current.empty(); ++i) {
                if (LooksLikeProjectRoot(current)) {
                    return current;
                }
                current = current.parent_path();
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

        std::filesystem::path MakeRelativePath(
            const std::filesystem::path& root,
            const std::filesystem::path& path) {
            std::error_code ec{};
            const std::filesystem::path relative = std::filesystem::relative(path, root, ec);
            if (!ec && !relative.empty()) {
                return relative.lexically_normal();
            }
            return path.lexically_normal();
        }

        std::filesystem::path ResolveOutputDirectory(
            const std::filesystem::path& projectRoot,
            const ExportRecipe& recipe,
            const std::filesystem::path& requestedDirectory) {
            if (requestedDirectory.empty()) {
                return NormalizeExportPath(projectRoot / "Build" / "Exports" / recipe.outputFolderName);
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

        bool FindDxcRuntimeDirectory(
            const std::filesystem::path& projectRoot,
            std::filesystem::path& outDirectory) {
            std::vector<std::filesystem::path> candidateDirs{
                projectRoot,
                projectRoot / "ThirdParty" / "DXC" / "bin" / "x64",
                projectRoot / "ThirdParty" / "DirectXShaderCompiler" / "bin" / "x64",
            };

            const std::filesystem::path kitsBin = L"C:\\Program Files (x86)\\Windows Kits\\10\\bin";
            std::error_code ec{};
            if (std::filesystem::exists(kitsBin, ec) && !ec) {
                for (const auto& entry : std::filesystem::directory_iterator(kitsBin, ec)) {
                    if (ec) {
                        break;
                    }
                    if (!entry.is_directory(ec) || ec) {
                        ec.clear();
                        continue;
                    }
                    candidateDirs.push_back(entry.path() / L"x64");
                }
            }

            std::sort(candidateDirs.begin(), candidateDirs.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.generic_string() > rhs.generic_string();
            });

            for (const std::filesystem::path& directory : candidateDirs) {
                ec.clear();
                if (std::filesystem::exists(directory / "dxcompiler.dll", ec) && !ec) {
                    outDirectory = directory;
                    return true;
                }
            }
            return false;
        }

        bool CopyDxcRuntime(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& outputDirectory,
            std::string& errorMessage) {
            std::filesystem::path dxcDirectory{};
            if (!FindDxcRuntimeDirectory(projectRoot, dxcDirectory)) {
                errorMessage = "Could not find dxcompiler.dll for export. Install Windows SDK or place DXC runtime under ThirdParty/DXC/bin/x64.";
                return false;
            }

            if (!CopyFileChecked(
                dxcDirectory / "dxcompiler.dll",
                outputDirectory / "dxcompiler.dll",
                errorMessage)) {
                return false;
            }

            std::error_code ec{};
            const std::filesystem::path dxil = dxcDirectory / "dxil.dll";
            if (std::filesystem::exists(dxil, ec) && !ec) {
                if (!CopyFileChecked(dxil, outputDirectory / "dxil.dll", errorMessage)) {
                    return false;
                }
            }

            HIKARI_LOG_INFO("[GameExporter] copied DXC runtime from " + dxcDirectory.string());
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

        bool BuildSelectedSceneSet(
            const GameExportOptions& options,
            std::unordered_set<std::string>& selectedSceneGuids,
            std::string& errorMessage) {
            selectedSceneGuids.clear();
            for (const std::string& guid : options.sceneGuids) {
                if (guid.empty()) {
                    continue;
                }
                selectedSceneGuids.insert(guid);
            }

            if (selectedSceneGuids.empty()) {
                if (!options.startupSceneGuid.empty()) {
                    errorMessage = "Startup scene was set but no scenes were selected for export.";
                    return false;
                }
                return true;
            }

            if (options.startupSceneGuid.empty()) {
                errorMessage = "Startup scene is required when exporting selected scenes.";
                return false;
            }

            if (selectedSceneGuids.find(options.startupSceneGuid) == selectedSceneGuids.end()) {
                errorMessage = "Startup scene must be included in exported scenes: " + options.startupSceneGuid;
                return false;
            }

            return true;
        }

        bool BuildExportContentManifest(
            const std::filesystem::path& projectRoot,
            const GameExportOptions& options,
            ExportContentManifest& manifest,
            std::string& errorMessage) {
            manifest = {};

            std::unordered_set<std::string> selectedSceneGuids{};
            if (!BuildSelectedSceneSet(options, selectedSceneGuids, errorMessage)) {
                return false;
            }

            manifest.selectedSceneGuids = selectedSceneGuids;
            if (selectedSceneGuids.empty()) {
                return true;
            }
            manifest.restrictToSelectedScenes = true;

            AssetDatabase assetDatabase{};
            if (!assetDatabase.Initialize(projectRoot)) {
                errorMessage = "Could not initialize asset database for selected-scene export.";
                return false;
            }
            if (!assetDatabase.ScanAssets(false)) {
                errorMessage = "Could not scan assets for selected-scene export.";
                return false;
            }

            std::deque<std::string> pendingGuids{};
            auto appendMissing = [&](std::string assetId, std::string owner, std::string role) {
                if (assetId.empty()) {
                    return;
                }
                manifest.missingReferences.push_back(AssetMissingReference{
                    std::move(assetId),
                    std::move(owner),
                    std::move(role)
                });
            };

            auto queueRecord = [&](const AssetRecord* record, const std::string& owner, const std::string& role) {
                if (!record || !record->guid.IsValid()) {
                    appendMissing({}, owner, role);
                    return;
                }

                if (record->type == AssetType::Scene &&
                    selectedSceneGuids.find(record->guid.value) == selectedSceneGuids.end()) {
                    return;
                }

                if (manifest.requiredAssetGuids.insert(record->guid.value).second) {
                    pendingGuids.push_back(record->guid.value);
                }
            };

            auto queueGuid = [&](const std::string& guid, const std::string& owner, const std::string& role) {
                if (guid.empty()) {
                    return;
                }

                const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ guid });
                if (!record) {
                    appendMissing(guid, owner, role);
                    return;
                }
                queueRecord(record, owner, role);
            };

            SceneSerializer sceneSerializer{};
            for (const std::string& guid : selectedSceneGuids) {
                const AssetRecord* sceneRecord = assetDatabase.FindByGuid(AssetGuid{ guid });
                if (!sceneRecord || sceneRecord->type != AssetType::Scene) {
                    errorMessage = "Selected scene was not found in the asset database: " + guid;
                    return false;
                }

                queueRecord(sceneRecord, "Export Selection", "Scene");

                const std::filesystem::path scenePath =
                    (projectRoot / sceneRecord->sourcePath).lexically_normal();
                SceneDocument document{};
                if (!sceneSerializer.LoadFromFile(scenePath.string(), document)) {
                    errorMessage = "Could not read selected scene: " + scenePath.string();
                    return false;
                }

                AssetUsageSummary sceneUsage = AnalyzeAssetUsage(document, assetDatabase);
                for (const std::string& usedGuid : sceneUsage.usedGuids) {
                    queueGuid(
                        usedGuid,
                        sceneRecord->displayName.empty() ? sceneRecord->sourcePath.generic_string() : sceneRecord->displayName,
                        "Scene Dependency");
                }
                for (AssetMissingReference& missing : sceneUsage.missingReferences) {
                    manifest.missingReferences.push_back(std::move(missing));
                }
            }

            while (!pendingGuids.empty()) {
                const std::string guid = std::move(pendingGuids.front());
                pendingGuids.pop_front();

                const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ guid });
                if (!record) {
                    continue;
                }

                for (const AssetDependencyDesc& dependency : record->artifactManifest.dependencies) {
                    if (ASSETS::SEMANTICS::IsSourceOnlyAssetDependencyRole(
                            dependency.role)) {
                        continue;
                    }
                    const AssetRecord* dependencyRecord = nullptr;
                    if (dependency.guid.IsValid()) {
                        dependencyRecord = assetDatabase.FindByGuid(dependency.guid);
                    } else if (!dependency.path.empty()) {
                        dependencyRecord = assetDatabase.FindByPath(dependency.path);
                    }

                    if (!dependencyRecord) {
                        appendMissing(
                            dependency.guid.IsValid() ? dependency.guid.value : dependency.path,
                            record->displayName.empty() ? record->sourcePath.generic_string() : record->displayName,
                            dependency.role.empty() ? "Asset Dependency" : dependency.role);
                        continue;
                    }

                    queueRecord(
                        dependencyRecord,
                        record->displayName.empty() ? record->sourcePath.generic_string() : record->displayName,
                        dependency.role.empty() ? "Asset Dependency" : dependency.role);
                }
            }

            return true;
        }

        bool IsRuntimeArtifactForExport(const AssetArtifactDesc& artifact) {
            if (artifact.path.empty()) {
                return false;
            }

            if (ASSETS::SEMANTICS::MatchesAssetArtifact(
                artifact,
                ASSETS::SEMANTICS::AssetArtifactKind::DebugTextureDds,
                true)) {
                return false;
            }

            const std::string path = TEXT::ToLowerAsciiCopy(artifact.path);
            if (path.find("/captures/") != std::string::npos ||
                path.ends_with("_capture.dds")) {
                return false;
            }

            return true;
        }

        bool CopyProjectRelativeFile(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& outputDirectory,
            const std::filesystem::path& projectRelativePath,
            std::string& errorMessage) {
            if (projectRelativePath.empty()) {
                return true;
            }

            const std::filesystem::path sourcePath = projectRelativePath.is_absolute()
                ? projectRelativePath.lexically_normal()
                : (projectRoot / projectRelativePath).lexically_normal();
            const std::filesystem::path targetRelativePath =
                MakeRelativePath(projectRoot, sourcePath);
            return CopyFileChecked(sourcePath, outputDirectory / targetRelativePath, errorMessage);
        }

        bool ShouldCopySourceForSelectedExport(const AssetRecord& record) {
            switch (record.type) {
            case AssetType::Texture:
            case AssetType::Model:
                return false;
            default:
                return true;
            }
        }

        bool CopyAssetSourceAndMeta(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            const std::filesystem::path& outputDirectory,
            std::string& errorMessage) {
            if (ShouldCopySourceForSelectedExport(record)) {
                if (!CopyProjectRelativeFile(
                    assetDatabase.GetProjectRoot(),
                    outputDirectory,
                    record.sourcePath,
                    errorMessage)) {
                    return false;
                }
            }

            if (!record.metaPath.empty()) {
                if (!CopyProjectRelativeFile(
                    assetDatabase.GetProjectRoot(),
                    outputDirectory,
                    record.metaPath,
                    errorMessage)) {
                    return false;
                }
            }

            return true;
        }

        bool CopyAssetArtifacts(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            const std::filesystem::path& outputDirectory,
            std::string& errorMessage) {
            for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
                if (!IsRuntimeArtifactForExport(artifact)) {
                    continue;
                }

                if (!CopyProjectRelativeFile(
                    assetDatabase.GetProjectRoot(),
                    outputDirectory,
                    artifact.path,
                    errorMessage)) {
                    return false;
                }
            }
            return true;
        }

        bool CopySelectedAssetContent(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& outputDirectory,
            const ExportContentManifest& manifest,
            std::string& errorMessage) {
            AssetDatabase assetDatabase{};
            if (!assetDatabase.Initialize(projectRoot)) {
                errorMessage = "Could not initialize asset database for selected asset copy.";
                return false;
            }
            if (!assetDatabase.ScanAssets(false)) {
                errorMessage = "Could not scan assets for selected asset copy.";
                return false;
            }

            std::vector<std::string> guids(
                manifest.requiredAssetGuids.begin(),
                manifest.requiredAssetGuids.end());
            std::sort(guids.begin(), guids.end());

            for (const std::string& guid : guids) {
                const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ guid });
                if (!record) {
                    errorMessage = "Required asset disappeared during export: " + guid;
                    return false;
                }

                if (!CopyAssetSourceAndMeta(assetDatabase, *record, outputDirectory, errorMessage) ||
                    !CopyAssetArtifacts(assetDatabase, *record, outputDirectory, errorMessage)) {
                    return false;
                }
            }

            HIKARI_LOG_INFO(
                "[GameExporter] selected-scene content assets=" +
                std::to_string(guids.size()) +
                " missingReferences=" +
                std::to_string(manifest.missingReferences.size()));
            return true;
        }

        bool CopySelectedLightingContent(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& outputDirectory,
            const ExportContentManifest& manifest,
            std::string& errorMessage) {
            std::vector<std::string> sceneGuids(
                manifest.selectedSceneGuids.begin(),
                manifest.selectedSceneGuids.end());
            std::sort(sceneGuids.begin(), sceneGuids.end());

            for (const std::string& sceneGuid : sceneGuids) {
                if (!CopyDirectoryChecked(
                    projectRoot / "Library" / "Generated" / "Lighting" / sceneGuid,
                    outputDirectory / "Library" / "Generated" / "Lighting" / sceneGuid,
                    errorMessage,
                    true)) {
                    return false;
                }
            }
            return true;
        }

        bool CopyStartupSpineOpContent(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& outputDirectory,
            std::string& errorMessage) {
            const std::filesystem::path spineRoot = projectRoot / "Assets" / "Spine";
            return CopyDirectoryChecked(
                spineRoot,
                outputDirectory / "Assets" / "Spine",
                errorMessage);
        }

        bool WriteRuntimeConfig(
            const std::filesystem::path& outputDirectory,
            const ExportRecipe& recipe,
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
                    { "hostMode", recipe.hostMode },
                    { "enableImGui", recipe.enableImGui },
                    { "enableEditorUI", false },
                    { "enablePortableObjectTools", recipe.enablePortableObjectTools },
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

        bool CopyRuntimeContent(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& outputDirectory,
            const ExportRecipe& recipe,
            const GameExportOptions& options,
            const ExportContentManifest& contentManifest,
            std::string& errorMessage) {
            (void)options;

            const std::vector<std::pair<std::filesystem::path, std::filesystem::path>> directories{
                { "Data", "Data" },
                { "ProjectSettings", "ProjectSettings" },
                { "Library/AssetDatabase", "Library/AssetDatabase" },
                { "Library/ShaderCache", "Library/ShaderCache" },
                { "Library/Generated/IBL", "Library/Generated/IBL" },
                { "HIKARI/Shaders", "HIKARI/Shaders" },
            };

            if (contentManifest.restrictToSelectedScenes) {
                if (!CopySelectedAssetContent(
                    projectRoot,
                    outputDirectory,
                    contentManifest,
                    errorMessage)) {
                    return false;
                }
            } else {
                if (!CopyDirectoryChecked(
                    projectRoot / "Assets",
                    outputDirectory / "Assets",
                    errorMessage)) {
                    return false;
                }
                if (!CopyDirectoryChecked(
                    projectRoot / "Library" / "Imported",
                    outputDirectory / "Library" / "Imported",
                    errorMessage)) {
                    return false;
                }
            }

            if (!CopyStartupSpineOpContent(projectRoot, outputDirectory, errorMessage)) {
                return false;
            }

            for (const auto& [sourceRelative, targetRelative] : directories) {
                if (!CopyDirectoryChecked(
                    projectRoot / sourceRelative,
                    outputDirectory / targetRelative,
                    errorMessage)) {
                    return false;
                }
            }

            if (!CopyDxcRuntime(projectRoot, outputDirectory, errorMessage)) {
                return false;
            }

            if (contentManifest.restrictToSelectedScenes) {
                if (!CopySelectedLightingContent(
                    projectRoot,
                    outputDirectory,
                    contentManifest,
                    errorMessage)) {
                    return false;
                }
            } else if (!CopyDirectoryChecked(
                projectRoot / "Library" / "Generated" / "Lighting",
                outputDirectory / "Library" / "Generated" / "Lighting",
                errorMessage,
                true)) {
                return false;
            }

            if (recipe.enablePortableObjectTools) {
                if (!CopyDirectoryChecked(
                    projectRoot / "HIKARI/Icon",
                    outputDirectory / "HIKARI/Icon",
                    errorMessage)) {
                    return false;
                }

                const std::filesystem::path pixRuntime =
                    projectRoot / "ThirdParty/WinPixEventRuntime/bin/x64/WinPixEventRuntime.dll";
                if (!CopyFileChecked(pixRuntime, outputDirectory / "WinPixEventRuntime.dll", errorMessage)) {
                    return false;
                }
            }

            return true;
        }
#endif
    }

    std::filesystem::path GameExporter::GetDefaultOutputDirectory(GameExportTarget target) {
#if defined(HIKARI_WITH_EDITOR)
        const ExportRecipe recipe = RecipeFor(target);
        const std::filesystem::path projectRoot = FindProjectRoot();
        return ResolveOutputDirectory(projectRoot, recipe, {});
#else
        (void)target;
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

    GameExportResult GameExporter::Export(GameExportTarget target) {
        GameExportOptions options{};
        options.target = target;
        return Export(options);
    }

    GameExportResult GameExporter::Export(const GameExportOptions& options) {
#if defined(HIKARI_WITH_EDITOR)
        const ExportRecipe recipe = RecipeFor(options.target);
        const std::filesystem::path projectRoot = FindProjectRoot();
        const std::filesystem::path sourceExe =
            projectRoot.parent_path() / "Generated" / "Outputs" / recipe.sourceConfiguration / "HIKARI_UpdateTo3D.exe";
        const std::filesystem::path outputDirectory =
            ResolveOutputDirectory(projectRoot, recipe, options.outputDirectory);
        const std::filesystem::path outputExe = outputDirectory / "HIKARI_Game.exe";
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
        ExportContentManifest contentManifest{};
        if (!BuildExportContentManifest(projectRoot, options, contentManifest, errorMessage)) {
            result.message = errorMessage;
            HIKARI_LOG_WARN(result.message);
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
        if (!CopyFileChecked(sourceExe, outputExe, errorMessage) ||
            !nativeRuntime.success ||
            !RENDER3D::UPSCALING::DeployStreamlineRuntime(
                sourceExe.parent_path(),
                outputDirectory,
                streamlineDeployment,
                errorMessage) ||
            !WriteRuntimeConfig(outputDirectory, recipe, options, errorMessage) ||
            !CopyRuntimeContent(projectRoot, outputDirectory, recipe, options, contentManifest, errorMessage)) {
            if (errorMessage.empty() && !nativeRuntime.success) {
                errorMessage = nativeRuntime.message;
            }
            result.message = errorMessage;
            HIKARI_LOG_WARN(std::string("[GameExporter] ") + result.message);
            return result;
        }

        result.success = true;
        if (options.sceneGuids.empty()) {
            result.message = "Exported to " + outputDirectory.string();
        } else {
            result.message =
                "Exported " + std::to_string(options.sceneGuids.size()) +
                " scene(s), " + std::to_string(contentManifest.requiredAssetGuids.size()) +
                " asset(s) to " + outputDirectory.string();
        }
        HIKARI_LOG_INFO(std::string("[GameExporter] ") + result.message);
        return result;
#else
        (void)options;
        return { false, {}, "Game export is only available in editor builds." };
#endif
    }

} // namespace HIKARI::EDITOR
