#include "Assets/Cooking/HIKARI_AssetCooker.h"

#include <algorithm>
#include <deque>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "Assets/Cooking/HIKARI_CookedContentManifest.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI::ASSETS::COOKING {
    namespace {

        struct CookPlan {
            std::unordered_set<std::string> assetGuids{};
            std::unordered_set<std::string> sceneGuids{};
            size_t missingReferenceCount = 0u;
        };

        std::string MakePathKey(
            const std::filesystem::path& path) {

            return TEXT::ToLowerAsciiCopy(
                path.lexically_normal().generic_string());
        }

        bool IsPathWithinRoot(
            const std::filesystem::path& path,
            const std::filesystem::path& root) {
            const std::string pathKey = MakePathKey(path);
            std::string rootKey = MakePathKey(root);
            while (!rootKey.empty() && rootKey.back() == '/') {
                rootKey.pop_back();
            }
            return !rootKey.empty() &&
                (pathKey == rootKey ||
                 (pathKey.size() > rootKey.size() &&
                  pathKey.compare(0u, rootKey.size(), rootKey) == 0 &&
                  pathKey[rootKey.size()] == '/'));
        }

        bool IsDeclaredProjectAssetRoot(
            const std::filesystem::path& root) {
            if (!IsSafeCookedRelativePath(root)) {
                return false;
            }
            const std::string key = MakePathKey(root);
            return key == "assets" || key.rfind("assets/", 0u) == 0u;
        }

        bool CopyFile(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            size_t& fileCount,
            std::string& outMessage) {

            std::error_code ec{};
            if (!std::filesystem::is_regular_file(source, ec) ||
                ec) {
                outMessage =
                    "Cook source file is missing: " +
                    source.generic_string();
                return false;
            }

            std::filesystem::create_directories(
                destination.parent_path(),
                ec);
            if (ec) {
                outMessage =
                    "Could not create cooked directory: " +
                    destination.parent_path().generic_string();
                return false;
            }

            std::filesystem::copy_file(
                source,
                destination,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec) {
                outMessage =
                    "Could not publish cooked file: " +
                    source.generic_string() + " -> " +
                    destination.generic_string() + ": " +
                    ec.message();
                return false;
            }
            ++fileCount;
            return true;
        }

        bool CopyDirectory(
            const std::filesystem::path& source,
            const std::filesystem::path& destination,
            size_t& fileCount,
            std::string& outMessage,
            bool generatedRuntimeOnly = false) {

            std::error_code ec{};
            if (!std::filesystem::exists(source, ec) || ec) {
                return true;
            }
            if (!std::filesystem::is_directory(source, ec) || ec) {
                outMessage =
                    "Cook directory source is invalid: " +
                    source.generic_string();
                return false;
            }

            std::filesystem::recursive_directory_iterator iterator(
                source,
                std::filesystem::directory_options::
                    skip_permission_denied,
                ec);
            const std::filesystem::recursive_directory_iterator end{};
            for (; !ec && iterator != end; iterator.increment(ec)) {
                if (!iterator->is_regular_file(ec)) {
                    ec.clear();
                    continue;
                }

                const std::filesystem::path relative =
                    std::filesystem::relative(
                        iterator->path(),
                        source,
                        ec);
                if (ec ||
                    !IsSafeCookedRelativePath(relative)) {
                    outMessage =
                        "Could not resolve cooked directory file: " +
                        iterator->path().generic_string();
                    return false;
                }

                const std::string relativeKey =
                    TEXT::ToLowerAsciiCopy(
                        relative.generic_string());
                if (relative.filename() == ".gitkeep" ||
                    relativeKey.ends_with(
                        ".hikari.asset.json")) {
                    continue;
                }
                if (generatedRuntimeOnly &&
                    (relativeKey.find("/captures/") !=
                         std::string::npos ||
                     relativeKey.ends_with("_capture.dds"))) {
                    continue;
                }

                if (!CopyFile(
                        iterator->path(),
                        destination / relative,
                        fileCount,
                        outMessage)) {
                    return false;
                }
            }

            if (ec) {
                outMessage =
                    "Could not enumerate cook directory: " +
                    source.generic_string() + ": " +
                    ec.message();
                return false;
            }
            return true;
        }

        bool IsRuntimeArtifact(
            const AssetArtifactDesc& artifact) {

            if (artifact.path.empty()) {
                return false;
            }
            if (ASSETS::SEMANTICS::MatchesAssetArtifact(
                    artifact,
                    ASSETS::SEMANTICS::
                        AssetArtifactKind::DebugTextureDds,
                    true)) {
                return false;
            }

            const std::string path =
                TEXT::ToLowerAsciiCopy(artifact.path);
            return path.find("/captures/") ==
                    std::string::npos &&
                !path.ends_with("_capture.dds");
        }

        bool HasArtifactKind(
            const std::vector<AssetArtifactDesc>& artifacts,
            ASSETS::SEMANTICS::AssetArtifactKind kind) {

            return std::any_of(
                artifacts.begin(),
                artifacts.end(),
                [kind](const AssetArtifactDesc& artifact) {
                    return ASSETS::SEMANTICS::
                        MatchesAssetArtifact(
                            artifact,
                            kind,
                            true);
                });
        }

        bool RequiresSourcePayload(
            const AssetRecord& record,
            const std::vector<AssetArtifactDesc>& artifacts) {

            switch (record.type) {
            case AssetType::Texture:
                return !HasArtifactKind(
                    artifacts,
                    ASSETS::SEMANTICS::
                        AssetArtifactKind::MainTexture);
            case AssetType::Model:
                return !HasArtifactKind(
                    artifacts,
                    ASSETS::SEMANTICS::
                        AssetArtifactKind::MainModel);
            case AssetType::Sky:
                return !HasArtifactKind(
                    artifacts,
                    ASSETS::SEMANTICS::
                        AssetArtifactKind::SkyCubemap);
            case AssetType::Material:
                return !HasArtifactKind(
                    artifacts,
                    ASSETS::SEMANTICS::
                        AssetArtifactKind::Material);
            case AssetType::VfxEffect:
                return !HasArtifactKind(
                    artifacts,
                    ASSETS::SEMANTICS::
                        AssetArtifactKind::VfxProgram);
            default:
                return true;
            }
        }

        bool RequiresPrimaryArtifact(
            AssetType type) {

            return type == AssetType::Texture ||
                type == AssetType::Model ||
                type == AssetType::Sky ||
                type == AssetType::VfxEffect;
        }

        std::filesystem::path MakeRuntimePayloadPath(
            const std::filesystem::path& sourcePath) {

            return (
                std::filesystem::path("RuntimePayload") /
                sourcePath).lexically_normal();
        }

        bool BuildCookPlan(
            const AssetCookOptions& options,
            AssetDatabase& assetDatabase,
            CookPlan& outPlan,
            std::string& outMessage) {

            CookPlan plan{};
            for (const std::string& sceneGuid :
                 options.sceneGuids) {
                if (!sceneGuid.empty()) {
                    plan.sceneGuids.insert(sceneGuid);
                }
            }

            if (plan.sceneGuids.empty()) {
                if (!options.startupSceneGuid.empty()) {
                    outMessage =
                        "Startup scene was set but no scenes were "
                        "selected for cooking.";
                    return false;
                }
                for (const AssetRecord* record :
                     assetDatabase.CollectAll()) {
                    if (record != nullptr &&
                        record->guid.IsValid()) {
                        plan.assetGuids.insert(
                            record->guid.value);
                    }
                }
                outPlan = std::move(plan);
                return true;
            }

            if (options.startupSceneGuid.empty()) {
                outMessage =
                    "Startup scene is required when cooking selected "
                    "scenes.";
                return false;
            }
            if (!plan.sceneGuids.contains(
                    options.startupSceneGuid)) {
                outMessage =
                    "Startup scene must be included in cooked scenes: " +
                    options.startupSceneGuid;
                return false;
            }

            std::deque<std::string> pendingGuids{};
            auto queueRecord =
                [&](const AssetRecord* record) {
                    if (record == nullptr ||
                        !record->guid.IsValid()) {
                        ++plan.missingReferenceCount;
                        return;
                    }
                    if (record->type == AssetType::Scene &&
                        !plan.sceneGuids.contains(
                            record->guid.value)) {
                        return;
                    }
                    if (plan.assetGuids.insert(
                            record->guid.value).second) {
                        pendingGuids.push_back(
                            record->guid.value);
                    }
                };

            SceneSerializer sceneSerializer{};
            for (const std::string& sceneGuid :
                 plan.sceneGuids) {
                const AssetRecord* sceneRecord =
                    assetDatabase.FindByGuid(
                        AssetGuid{ sceneGuid });
                if (sceneRecord == nullptr ||
                    sceneRecord->type != AssetType::Scene) {
                    outMessage =
                        "Cooked scene was not found in the asset "
                        "database: " + sceneGuid;
                    return false;
                }

                queueRecord(sceneRecord);
                SceneDocument document{};
                const std::filesystem::path scenePath =
                    options.projectRoot /
                    sceneRecord->sourcePath;
                if (!sceneSerializer.LoadFromFile(
                        scenePath.lexically_normal().string(),
                        document)) {
                    outMessage =
                        "Could not read scene while building cook plan: " +
                        scenePath.generic_string();
                    return false;
                }

                AssetUsageSummary usage =
                    AnalyzeAssetUsage(
                        document,
                        assetDatabase);
                for (const std::string& usedGuid :
                     usage.usedGuids) {
                    const AssetRecord* usedRecord =
                        assetDatabase.FindByGuid(
                            AssetGuid{ usedGuid });
                    if (usedRecord == nullptr) {
                        ++plan.missingReferenceCount;
                        continue;
                    }
                    queueRecord(usedRecord);
                }
                plan.missingReferenceCount +=
                    usage.missingReferences.size();
            }

            const std::vector<const AssetRecord*> allRecords =
                assetDatabase.CollectAll();
            for (const std::filesystem::path& root :
                 options.assetRoots) {
                if (!IsDeclaredProjectAssetRoot(root)) {
                    outMessage =
                        "Cook asset root must be a safe path below Assets: " +
                        root.generic_string();
                    return false;
                }
                size_t matched = 0u;
                for (const AssetRecord* record : allRecords) {
                    if (record != nullptr &&
                        IsPathWithinRoot(record->sourcePath, root)) {
                        queueRecord(record);
                        ++matched;
                    }
                }
                if (matched == 0u) {
                    outMessage =
                        "Cook asset root did not resolve any registered assets: " +
                        root.generic_string();
                    return false;
                }
            }

            const std::filesystem::path dialogueStylePath =
                options.projectRoot /
                std::string(DIALOGUE::kDialogueProjectStylePath);
            std::error_code dialogueStyleError{};
            if (std::filesystem::is_regular_file(
                    dialogueStylePath,
                    dialogueStyleError) &&
                !dialogueStyleError) {
                DIALOGUE::DialogueProjectStyle dialogueStyle{};
                std::string dialogueStyleMessage{};
                if (!DIALOGUE::LoadDialogueProjectStyle(
                        options.projectRoot,
                        dialogueStyle,
                        &dialogueStyleMessage)) {
                    outMessage =
                        "Could not read dialogue UI style while building "
                        "the cook plan: " + dialogueStyleMessage;
                    return false;
                }
                for (const DIALOGUE::DialogueSpeakerProfile& speaker :
                     dialogueStyle.speakers) {
                    if (speaker.portraitAssetGuid.empty()) {
                        continue;
                    }
                    const AssetRecord* portraitRecord =
                        assetDatabase.FindByGuid(
                            AssetGuid{ speaker.portraitAssetGuid });
                    if (portraitRecord == nullptr ||
                        portraitRecord->type != AssetType::Texture) {
                        outMessage =
                            "Dialogue speaker '" + speaker.id +
                            "' references a missing or non-texture portrait "
                            "asset: " + speaker.portraitAssetGuid;
                        return false;
                    }
                    queueRecord(portraitRecord);
                }
            }
            else if (dialogueStyleError) {
                outMessage =
                    "Could not inspect dialogue UI style while building "
                    "the cook plan: " + dialogueStyleError.message();
                return false;
            }

            while (!pendingGuids.empty()) {
                const std::string guid =
                    std::move(pendingGuids.front());
                pendingGuids.pop_front();

                const AssetRecord* record =
                    assetDatabase.FindByGuid(
                        AssetGuid{ guid });
                if (record == nullptr) {
                    ++plan.missingReferenceCount;
                    continue;
                }

                for (const AssetDependencyDesc& dependency :
                     record->artifactManifest.dependencies) {
                    if (ASSETS::SEMANTICS::
                            IsSourceOnlyAssetDependencyRole(
                                dependency.role)) {
                        continue;
                    }

                    const AssetRecord* dependencyRecord = nullptr;
                    if (dependency.guid.IsValid()) {
                        dependencyRecord =
                            assetDatabase.FindByGuid(
                                dependency.guid);
                    }
                    else if (!dependency.path.empty()) {
                        dependencyRecord =
                            assetDatabase.FindByPath(
                                dependency.path);
                    }
                    if (dependencyRecord == nullptr) {
                        ++plan.missingReferenceCount;
                        continue;
                    }
                    queueRecord(dependencyRecord);
                }
            }

            outPlan = std::move(plan);
            return true;
        }

        bool CopyOpeningAnimation(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& contentRoot,
            size_t& fileCount,
            std::string& outMessage) {

            const std::filesystem::path sourceRoot =
                projectRoot / "Assets" / "Spine";
            const std::filesystem::path atlasPath =
                sourceRoot / "op.atlas";
            const std::filesystem::path skeletonPath =
                sourceRoot / "op.json";

            std::error_code ec{};
            const bool hasAtlas =
                std::filesystem::is_regular_file(
                    atlasPath,
                    ec) && !ec;
            ec.clear();
            const bool hasSkeleton =
                std::filesystem::is_regular_file(
                    skeletonPath,
                    ec) && !ec;
            if (!hasAtlas && !hasSkeleton) {
                return true;
            }
            if (!hasAtlas || !hasSkeleton) {
                outMessage =
                    "Optional opening animation is incomplete under " +
                    sourceRoot.generic_string();
                return false;
            }

            const std::filesystem::path destinationRoot =
                contentRoot / "RuntimePayload" / "Spine";
            if (!CopyFile(
                    atlasPath,
                    destinationRoot / "op.atlas",
                    fileCount,
                    outMessage) ||
                !CopyFile(
                    skeletonPath,
                    destinationRoot / "op.json",
                    fileCount,
                    outMessage)) {
                return false;
            }

            std::ifstream atlas(atlasPath);
            if (!atlas) {
                outMessage =
                    "Could not read opening Spine atlas: " +
                    atlasPath.generic_string();
                return false;
            }

            std::unordered_set<std::string> copiedPages{};
            std::string line{};
            while (std::getline(atlas, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                const std::filesystem::path pagePath = line;
                const std::string extension =
                    TEXT::ToLowerAsciiCopy(
                        pagePath.extension().string());
                if (line.find(':') != std::string::npos ||
                    (extension != ".png" &&
                     extension != ".jpg" &&
                     extension != ".jpeg" &&
                     extension != ".dds") ||
                    !IsSafeCookedRelativePath(pagePath) ||
                    !copiedPages.insert(
                        MakePathKey(pagePath)).second) {
                    continue;
                }

                if (!CopyFile(
                        sourceRoot / pagePath,
                        destinationRoot / pagePath,
                        fileCount,
                        outMessage)) {
                    return false;
                }
            }
            return true;
        }

        bool CopyRuntimeSettingsAndData(
            const AssetCookOptions& options,
            size_t& fileCount,
            std::string& outMessage) {

            const std::filesystem::path settingsSource =
                options.projectRoot / "ProjectSettings";
            const std::filesystem::path settingsDestination =
                options.contentRoot / "ProjectSettings";
            for (const std::filesystem::path& filename :
                 {
                     std::filesystem::path(
                         "project_settings.json"),
                      std::filesystem::path(
                          "render_quality.json"),
                      std::filesystem::path(
                          "runtime_bindings.json"),
                      std::filesystem::path(
                          "DialogueUiStyle.json"),
                  }) {
                std::error_code ec{};
                const std::filesystem::path source =
                    settingsSource / filename;
                if (!std::filesystem::exists(source, ec) || ec) {
                    continue;
                }
                if (!CopyFile(
                        source,
                        settingsDestination / filename,
                        fileCount,
                        outMessage)) {
                    return false;
                }
            }

            if (!CopyDirectory(
                    settingsSource / "Input",
                    settingsDestination / "Input",
                    fileCount,
                    outMessage) ||
                !CopyDirectory(
                    settingsSource / "Physics",
                    settingsDestination / "Physics",
                    fileCount,
                    outMessage) ||
                !CopyDirectory(
                    options.projectRoot / "Data",
                    options.contentRoot / "Data",
                    fileCount,
                    outMessage)) {
                return false;
            }
            return true;
        }

        bool CopyDeclaredRuntimePayloads(
            const AssetCookOptions& options,
            size_t& fileCount,
            std::string& outMessage) {
            std::unordered_set<std::string> copiedRoots{};
            for (const std::filesystem::path& root :
                 options.runtimePayloadRoots) {
                if (!IsDeclaredProjectAssetRoot(root)) {
                    outMessage =
                        "Runtime payload root must be a safe path below Assets: " +
                        root.generic_string();
                    return false;
                }
                if (!copiedRoots.insert(MakePathKey(root)).second) {
                    continue;
                }
                const std::filesystem::path source =
                    options.projectRoot / root;
                std::error_code ec{};
                if (!std::filesystem::is_directory(source, ec) || ec) {
                    outMessage =
                        "Declared runtime payload root is missing: " +
                        source.generic_string();
                    return false;
                }
                if (!CopyDirectory(
                        source,
                        options.contentRoot / "RuntimePayload" / root,
                        fileCount,
                        outMessage)) {
                    return false;
                }
            }
            return true;
        }

        bool CopyLightingContent(
            const AssetCookOptions& options,
            const CookPlan& plan,
            size_t& fileCount,
            std::string& outMessage) {

            const std::filesystem::path generatedSource =
                options.projectRoot /
                "Library" / "Generated";
            const std::filesystem::path generatedDestination =
                options.contentRoot /
                "Library" / "Generated";
            if (!CopyDirectory(
                    generatedSource / "IBL",
                    generatedDestination / "IBL",
                    fileCount,
                    outMessage,
                    true)) {
                return false;
            }

            if (plan.sceneGuids.empty()) {
                return CopyDirectory(
                    generatedSource / "Lighting",
                    generatedDestination / "Lighting",
                    fileCount,
                    outMessage,
                    true);
            }

            std::vector<std::string> sceneGuids(
                plan.sceneGuids.begin(),
                plan.sceneGuids.end());
            std::sort(sceneGuids.begin(), sceneGuids.end());
            for (const std::string& sceneGuid : sceneGuids) {
                if (!CopyDirectory(
                        generatedSource /
                            "Lighting" / sceneGuid,
                        generatedDestination /
                            "Lighting" / sceneGuid,
                        fileCount,
                        outMessage,
                        true)) {
                    return false;
                }
            }
            return true;
        }

        bool CookProceduralGeometry(
            const AssetCookOptions& options,
            const CookPlan& plan,
            const AssetDatabase& assetDatabase,
            size_t& fileCount,
            std::string& outMessage) {

            std::vector<const AssetRecord*> sceneRecords{};
            if (plan.sceneGuids.empty()) {
                for (const AssetRecord* record :
                     assetDatabase.CollectByType(
                         AssetType::Scene)) {
                    if (record != nullptr &&
                        plan.assetGuids.contains(
                            record->guid.value)) {
                        sceneRecords.push_back(record);
                    }
                }
            }
            else {
                for (const std::string& sceneGuid :
                     plan.sceneGuids) {
                    const AssetRecord* record =
                        assetDatabase.FindByGuid(
                            AssetGuid{ sceneGuid });
                    if (record != nullptr) {
                        sceneRecords.push_back(record);
                    }
                }
            }
            std::sort(
                sceneRecords.begin(),
                sceneRecords.end(),
                [](const AssetRecord* lhs,
                   const AssetRecord* rhs) {
                    return lhs->guid.value < rhs->guid.value;
                });

            SceneSerializer serializer{};
            std::unordered_set<std::string> publishedPaths{};
            for (const AssetRecord* sceneRecord :
                 sceneRecords) {
                SceneDocument document{};
                const std::filesystem::path scenePath =
                    options.projectRoot /
                    sceneRecord->sourcePath;
                if (!serializer.LoadFromFile(
                        scenePath.lexically_normal().string(),
                        document)) {
                    outMessage =
                        "Could not read scene while cooking procedural "
                        "geometry: " +
                        scenePath.generic_string();
                    return false;
                }

                for (const SceneObjectData& object :
                     document.objects) {
                    for (const SceneComponentData& componentData :
                         object.components) {
                        if (componentData.type !=
                            "ProceduralMeshComponent") {
                            continue;
                        }

                        ProceduralMeshComponent component{};
                        component.Deserialize(
                            componentData.properties);
                        const std::filesystem::path generatedPath =
                            PROCEDURAL::
                                GetOrCreateClusteredGeometryPath(
                                    component.GetSettings(),
                                    options.projectRoot);
                        if (generatedPath.empty()) {
                            outMessage =
                                "Could not cook procedural clustered "
                                "geometry for scene " +
                                sceneRecord->guid.value;
                            return false;
                        }

                        std::error_code relativeError{};
                        const std::filesystem::path relativePath =
                            std::filesystem::relative(
                                generatedPath,
                                options.projectRoot,
                                relativeError);
                        if (relativeError ||
                            !IsSafeCookedRelativePath(
                                relativePath)) {
                            outMessage =
                                "Procedural clustered geometry escaped "
                                "the project root: " +
                                generatedPath.generic_string();
                            return false;
                        }
                        if (!publishedPaths.insert(
                                MakePathKey(
                                    relativePath)).second) {
                            continue;
                        }

                        if (!CopyFile(
                                generatedPath,
                                options.contentRoot /
                                    relativePath,
                                fileCount,
                                outMessage)) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }

    } // namespace

    AssetCookResult AssetCooker::Cook(
        const AssetCookOptions& options) const {

        AssetCookResult result{};
        if (options.projectRoot.empty() ||
            options.contentRoot.empty()) {
            result.message =
                "Cook project root and content root are required.";
            return result;
        }

        std::error_code ec{};
        if (!std::filesystem::is_regular_file(
                options.projectRoot /
                    kProjectDescriptorFilename,
                ec) ||
            ec) {
            result.message =
                "Cook project descriptor is missing: " +
                (options.projectRoot /
                    kProjectDescriptorFilename).generic_string();
            return result;
        }

        std::filesystem::create_directories(
            options.contentRoot,
            ec);
        if (ec) {
            result.message =
                "Could not create cooked content root: " +
                options.contentRoot.generic_string();
            return result;
        }

        AssetDatabase assetDatabase{};
        if (!assetDatabase.Initialize(
                options.projectRoot) ||
            !assetDatabase.ScanAssets(false)) {
            result.message =
                "Could not scan authored assets for cooking.";
            return result;
        }

        CookPlan plan{};
        if (!BuildCookPlan(
                options,
                assetDatabase,
                plan,
                result.message)) {
            return result;
        }
        result.missingReferenceCount =
            plan.missingReferenceCount;

        std::vector<std::string> assetGuids(
            plan.assetGuids.begin(),
            plan.assetGuids.end());
        std::sort(assetGuids.begin(), assetGuids.end());

        std::unordered_map<
            std::string,
            std::filesystem::path> cookedSourceByOriginalPath{};
        std::unordered_map<
            std::string,
            std::vector<AssetArtifactDesc>>
                runtimeArtifactsByGuid{};

        for (const std::string& guid : assetGuids) {
            const AssetRecord* record =
                assetDatabase.FindByGuid(
                    AssetGuid{ guid });
            if (record == nullptr ||
                record->duplicateGuid) {
                result.message =
                    "Cook asset record is invalid or duplicated: " +
                    guid;
                return result;
            }
            if (!IsSafeCookedRelativePath(
                    record->sourcePath)) {
                result.message =
                    "Authored asset source path is unsafe: " +
                    record->sourcePath.generic_string();
                return result;
            }

            std::vector<AssetArtifactDesc> runtimeArtifacts{};
            for (const AssetArtifactDesc& artifact :
                 record->artifactManifest.artifacts) {
                if (IsRuntimeArtifact(artifact)) {
                    runtimeArtifacts.push_back(artifact);
                }
            }
            if (RequiresPrimaryArtifact(record->type) &&
                RequiresSourcePayload(
                    *record,
                    runtimeArtifacts)) {
                result.message =
                    "Required cooked artifact is missing for asset " +
                    guid + " (" +
                    record->sourcePath.generic_string() +
                    "). Reimport the asset before exporting.";
                return result;
            }

            const bool sourcePayload =
                RequiresSourcePayload(
                    *record,
                    runtimeArtifacts);
            cookedSourceByOriginalPath[
                MakePathKey(record->sourcePath)] =
                sourcePayload
                    ? MakeRuntimePayloadPath(
                        record->sourcePath)
                    : record->sourcePath;
            runtimeArtifactsByGuid.emplace(
                guid,
                std::move(runtimeArtifacts));
        }

        CookedContentManifest manifest{};
        manifest.startupSceneGuid =
            options.startupSceneGuid;
        manifest.exportedSceneGuids =
            options.sceneGuids;
        manifest.assets.reserve(assetGuids.size());

        for (const std::string& guid : assetGuids) {
            const AssetRecord* sourceRecord =
                assetDatabase.FindByGuid(
                    AssetGuid{ guid });
            if (sourceRecord == nullptr) {
                result.message =
                    "Cook asset disappeared: " + guid;
                return result;
            }

            AssetRecord cookedRecord = *sourceRecord;
            cookedRecord.metaPath.clear();
            cookedRecord.artifactManifestPath.clear();
            cookedRecord.sourcePath =
                cookedSourceByOriginalPath.at(
                    MakePathKey(sourceRecord->sourcePath));
            cookedRecord.meta.sourcePath =
                cookedRecord.sourcePath.generic_string();
            cookedRecord.artifactManifest.sourcePath =
                cookedRecord.sourcePath.generic_string();
            cookedRecord.artifactManifest.artifacts =
                runtimeArtifactsByGuid.at(guid);

            std::vector<AssetDependencyDesc> dependencies{};
            for (AssetDependencyDesc dependency :
                 sourceRecord->artifactManifest.dependencies) {
                if (ASSETS::SEMANTICS::
                        IsSourceOnlyAssetDependencyRole(
                            dependency.role)) {
                    continue;
                }

                const AssetRecord* dependencyRecord = nullptr;
                if (dependency.guid.IsValid()) {
                    dependencyRecord =
                        assetDatabase.FindByGuid(
                            dependency.guid);
                }
                else if (!dependency.path.empty()) {
                    dependencyRecord =
                        assetDatabase.FindByPath(
                            dependency.path);
                }
                if (dependencyRecord == nullptr ||
                    !plan.assetGuids.contains(
                        dependencyRecord->guid.value)) {
                    continue;
                }

                dependency.guid =
                    dependencyRecord->guid;
                const auto cookedPath =
                    cookedSourceByOriginalPath.find(
                        MakePathKey(
                            dependencyRecord->sourcePath));
                dependency.path =
                    cookedPath ==
                        cookedSourceByOriginalPath.end()
                        ? std::string{}
                        : cookedPath->second.generic_string();
                dependencies.push_back(
                    std::move(dependency));
            }
            cookedRecord.artifactManifest.dependencies =
                std::move(dependencies);

            if (cookedRecord.sourcePath !=
                sourceRecord->sourcePath) {
                if (!CopyFile(
                        options.projectRoot /
                            sourceRecord->sourcePath,
                        options.contentRoot /
                            cookedRecord.sourcePath,
                        result.fileCount,
                        result.message)) {
                    return result;
                }
            }

            for (const AssetArtifactDesc& artifact :
                 cookedRecord.artifactManifest.artifacts) {
                if (!IsSafeCookedRelativePath(
                        artifact.path) ||
                    !CopyFile(
                        options.projectRoot /
                            artifact.path,
                        options.contentRoot /
                            artifact.path,
                        result.fileCount,
                        result.message)) {
                    if (result.message.empty()) {
                        result.message =
                            "Cooked artifact path is unsafe: " +
                            artifact.path;
                    }
                    return result;
                }
            }

            cookedRecord.sourceExists = true;
            cookedRecord.metaExists = true;
            cookedRecord.importOutdated = false;
            cookedRecord.lastImportSucceeded = true;
            cookedRecord.duplicateGuid = false;
            cookedRecord.importerMissing = false;
            cookedRecord.artifactMissing = false;
            cookedRecord.lastImportMessage =
                "Published by HIKARI AssetCooker";
            manifest.assets.push_back(
                std::move(cookedRecord));
        }

        if (!CopyFile(
                options.projectRoot /
                    kProjectDescriptorFilename,
                options.contentRoot /
                    kProjectDescriptorFilename,
                result.fileCount,
                result.message) ||
            !CopyRuntimeSettingsAndData(
                options,
                result.fileCount,
                result.message) ||
            !CopyDeclaredRuntimePayloads(
                options,
                result.fileCount,
                result.message) ||
            !CopyLightingContent(
                options,
                plan,
                result.fileCount,
                result.message) ||
            !CookProceduralGeometry(
                options,
                plan,
                assetDatabase,
                result.fileCount,
                result.message) ||
            !CopyOpeningAnimation(
                options.projectRoot,
                options.contentRoot,
                result.fileCount,
                result.message)) {
            return result;
        }

        if (!WriteCookedContentManifest(
                options.contentRoot /
                    kCookedContentManifestFilename,
                manifest,
                &result.message)) {
            return result;
        }
        ++result.fileCount;

        result.assetCount = manifest.assets.size();
        result.success = true;
        result.message =
            "Cooked " +
            std::to_string(result.assetCount) +
            " asset(s) and published " +
            std::to_string(result.fileCount) +
            " file(s).";
        if (result.missingReferenceCount > 0u) {
            result.message +=
                " Missing optional/reference entries: " +
                std::to_string(
                    result.missingReferenceCount) +
                ".";
        }
        HIKARI_LOG_INFO(
            "[AssetCooker] " + result.message);
        return result;
    }

} // namespace HIKARI::ASSETS::COOKING
