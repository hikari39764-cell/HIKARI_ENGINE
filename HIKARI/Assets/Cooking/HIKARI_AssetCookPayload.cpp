#include "Assets/Cooking/HIKARI_AssetCookInternal.h"
#include "Core/IO/HIKARI_PathNormalization.h"
#include "Assets/Cooking/HIKARI_CookedContentManifest.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "RenderContracts/Lighting/HIKARI_LightingBakeManifest.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/HIKARI_ProjectDescriptor.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include <algorithm>
#include <fstream>

namespace HIKARI::ASSETS::COOKING::INTERNAL {
bool CopyCookPayloadFile(const std::filesystem::path& source, const std::filesystem::path& destination,
                         size_t& fileCount, std::string& outMessage) {
    std::error_code ec{};
    if (!std::filesystem::is_regular_file(source, ec) || ec) {
        outMessage = "Cook source file is missing: " + source.generic_string();
        return false;
    }

    std::filesystem::create_directories(destination.parent_path(), ec);
    if (ec) {
        outMessage = "Could not create cooked directory: " + destination.parent_path().generic_string();
        return false;
    }

    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        outMessage = "Could not publish cooked file: " + source.generic_string() + " -> " +
                     destination.generic_string() + ": " + ec.message();
        return false;
    }
    ++fileCount;
    return true;
}

bool CopyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination, size_t& fileCount,
                   std::string& outMessage, bool generatedRuntimeOnly = false) {
    std::error_code ec{};
    if (!std::filesystem::exists(source, ec) || ec) {
        return true;
    }
    if (!std::filesystem::is_directory(source, ec) || ec) {
        outMessage = "Cook directory source is invalid: " + source.generic_string();
        return false;
    }

    std::filesystem::recursive_directory_iterator iterator(
        source, std::filesystem::directory_options::skip_permission_denied, ec);
    const std::filesystem::recursive_directory_iterator end{};
    for (; !ec && iterator != end; iterator.increment(ec)) {
        if (!iterator->is_regular_file(ec)) {
            ec.clear();
            continue;
        }

        const std::filesystem::path relative = std::filesystem::relative(iterator->path(), source, ec);
        if (ec || !PATHS::IsSafeRelativePath(relative)) {
            outMessage = "Could not resolve cooked directory file: " + iterator->path().generic_string();
            return false;
        }

        const std::string relativeKey = TEXT::ToLowerAsciiCopy(relative.generic_string());
        if (relative.filename() == ".gitkeep" || relativeKey.ends_with(".bak") || relativeKey.ends_with(".tmp") ||
            relativeKey.ends_with(".log") || relativeKey.ends_with("~") ||
            relativeKey.ends_with(".hikari.asset.json")) {
            continue;
        }
        if (generatedRuntimeOnly && LIGHTING::IsLightingBakeCaptureFile(relativeKey)) {
            continue;
        }

        if (!CopyCookPayloadFile(iterator->path(), destination / relative, fileCount, outMessage)) {
            return false;
        }
    }

    if (ec) {
        outMessage = "Could not enumerate cook directory: " + source.generic_string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool CopyOpeningAnimation(const std::filesystem::path& projectRoot, const std::filesystem::path& contentRoot,
                          size_t& fileCount, std::string& outMessage) {
    const std::filesystem::path sourceRoot = projectRoot / "Assets" / "Spine";
    const std::filesystem::path atlasPath = sourceRoot / "op.atlas";
    const std::filesystem::path skeletonPath = sourceRoot / "op.json";

    std::error_code ec{};
    const bool hasAtlas = std::filesystem::is_regular_file(atlasPath, ec) && !ec;
    ec.clear();
    const bool hasSkeleton = std::filesystem::is_regular_file(skeletonPath, ec) && !ec;
    if (!hasAtlas && !hasSkeleton) {
        return true;
    }
    if (!hasAtlas || !hasSkeleton) {
        outMessage = "Optional opening animation is incomplete under " + sourceRoot.generic_string();
        return false;
    }

    const std::filesystem::path destinationRoot = contentRoot / "RuntimePayload" / "Spine";
    if (!CopyCookPayloadFile(atlasPath, destinationRoot / "op.atlas", fileCount, outMessage) ||
        !CopyCookPayloadFile(skeletonPath, destinationRoot / "op.json", fileCount, outMessage)) {
        return false;
    }

    std::ifstream atlas(atlasPath);
    if (!atlas) {
        outMessage = "Could not read opening Spine atlas: " + atlasPath.generic_string();
        return false;
    }

    std::unordered_set<std::string> copiedPages{};
    std::string line{};
    while (std::getline(atlas, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::filesystem::path pagePath = TEXT::PathFromUtf8(line);
        const std::string extension = TEXT::ToLowerAsciiCopy(pagePath.extension().string());
        if (line.find(':') != std::string::npos ||
            (extension != ".png" && extension != ".jpg" && extension != ".jpeg" && extension != ".dds") ||
            !PATHS::IsSafeRelativePath(pagePath) || !copiedPages.insert(MakePathKey(pagePath)).second) {
            continue;
        }

        if (!CopyCookPayloadFile(sourceRoot / pagePath, destinationRoot / pagePath, fileCount, outMessage)) {
            return false;
        }
    }
    return true;
}

bool CopyRuntimeSettingsAndData(const AssetCookOptions& options, size_t& fileCount, std::string& outMessage) {
    const std::filesystem::path settingsSource = options.projectRoot / "ProjectSettings";
    const std::filesystem::path settingsDestination = options.contentRoot / "ProjectSettings";
    for (const std::filesystem::path& filename : {
             std::filesystem::path("project_settings.json"),
             std::filesystem::path("render_quality.json"),
             std::filesystem::path("runtime_bindings.json"),
             std::filesystem::path("DialogueUiStyle.json"),
         }) {
        std::error_code ec{};
        const std::filesystem::path source = settingsSource / filename;
        if (!std::filesystem::exists(source, ec) || ec) {
            continue;
        }
        if (!CopyCookPayloadFile(source, settingsDestination / filename, fileCount, outMessage)) {
            return false;
        }
    }

    if (!CopyDirectory(settingsSource / "Content", settingsDestination / "Content", fileCount, outMessage) ||
        !CopyDirectory(settingsSource / "Input", settingsDestination / "Input", fileCount, outMessage) ||
        !CopyDirectory(settingsSource / "Physics", settingsDestination / "Physics", fileCount, outMessage) ||
        !CopyDirectory(options.projectRoot / "Data", options.contentRoot / "Data", fileCount, outMessage)) {
        return false;
    }
    return true;
}

bool CopyDeclaredRuntimePayloads(const AssetCookOptions& options, size_t& fileCount, std::string& outMessage) {
    std::unordered_set<std::string> copiedRoots{};
    for (const std::filesystem::path& root : options.runtimePayloadRoots) {
        if (!IsDeclaredProjectAssetRoot(root)) {
            outMessage = "Runtime payload root must be a safe path below Assets: " + root.generic_string();
            return false;
        }
        if (!copiedRoots.insert(MakePathKey(root)).second) {
            continue;
        }
        const std::filesystem::path source = options.projectRoot / root;
        std::error_code ec{};
        if (!std::filesystem::is_directory(source, ec) || ec) {
            outMessage = "Declared runtime payload root is missing: " + source.generic_string();
            return false;
        }
        if (!CopyDirectory(source, options.contentRoot / "RuntimePayload" / root, fileCount, outMessage)) {
            return false;
        }
    }
    return true;
}

bool CopyLightingContent(const AssetCookOptions& options, const CookPlan& plan, size_t& fileCount,
                         std::string& outMessage) {
    const std::filesystem::path generatedSource = options.projectRoot / "Library" / "Generated";
    const std::filesystem::path generatedDestination = options.contentRoot / "Library" / "Generated";
    if (!CopyDirectory(generatedSource / "IBL", generatedDestination / "IBL", fileCount, outMessage, true)) {
        return false;
    }

    if (plan.sceneGuids.empty()) {
        return CopyDirectory(generatedSource / "Lighting", generatedDestination / "Lighting", fileCount, outMessage,
                             true);
    }

    std::vector<std::string> sceneGuids(plan.sceneGuids.begin(), plan.sceneGuids.end());
    std::sort(sceneGuids.begin(), sceneGuids.end());
    for (const std::string& sceneGuid : sceneGuids) {
        if (!CopyDirectory(generatedSource / "Lighting" / sceneGuid, generatedDestination / "Lighting" / sceneGuid,
                           fileCount, outMessage, true)) {
            return false;
        }
    }
    return true;
}

bool CookProceduralGeometry(const AssetCookOptions& options, const CookPlan& plan, const AssetDatabase& assetDatabase,
                            size_t& fileCount, std::string& outMessage) {
    std::vector<const AssetRecord*> sceneRecords{};
    if (plan.sceneGuids.empty()) {
        for (const AssetRecord* record : assetDatabase.CollectByType(AssetType::Scene)) {
            if (record != nullptr && plan.assetGuids.contains(record->guid.value)) {
                sceneRecords.push_back(record);
            }
        }
    } else {
        for (const std::string& sceneGuid : plan.sceneGuids) {
            const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{sceneGuid});
            if (record != nullptr) {
                sceneRecords.push_back(record);
            }
        }
    }
    std::sort(sceneRecords.begin(), sceneRecords.end(),
              [](const AssetRecord* lhs, const AssetRecord* rhs) { return lhs->guid.value < rhs->guid.value; });

    SceneSerializer serializer{};
    std::unordered_set<std::string> publishedPaths{};
    for (const AssetRecord* sceneRecord : sceneRecords) {
        SceneDocument document{};
        const std::filesystem::path scenePath = options.projectRoot / sceneRecord->sourcePath;
        if (!serializer.LoadFromFile(scenePath.lexically_normal().string(), document)) {
            outMessage = "Could not read scene while cooking procedural "
                         "geometry: " +
                         scenePath.generic_string();
            return false;
        }

        for (const SceneObjectData& object : document.objects) {
            for (const SceneComponentData& componentData : object.components) {
                if (componentData.type != "ProceduralMeshComponent") {
                    continue;
                }

                ProceduralMeshComponent component{};
                component.Deserialize(componentData.properties);
                const std::filesystem::path generatedPath =
                    PROCEDURAL::GetOrCreateClusteredGeometryPath(component.GetSettings(), options.projectRoot);
                if (generatedPath.empty()) {
                    outMessage = "Could not cook procedural clustered "
                                 "geometry for scene " +
                                 sceneRecord->guid.value;
                    return false;
                }

                std::error_code relativeError{};
                const std::filesystem::path relativePath =
                    std::filesystem::relative(generatedPath, options.projectRoot, relativeError);
                if (relativeError || !PATHS::IsSafeRelativePath(relativePath)) {
                    outMessage = "Procedural clustered geometry escaped "
                                 "the project root: " +
                                 generatedPath.generic_string();
                    return false;
                }
                if (!publishedPaths.insert(MakePathKey(relativePath)).second) {
                    continue;
                }

                if (!CopyCookPayloadFile(generatedPath, options.contentRoot / relativePath, fileCount, outMessage)) {
                    return false;
                }
            }
        }
    }
    return true;
}

} // namespace HIKARI::ASSETS::COOKING::INTERNAL
