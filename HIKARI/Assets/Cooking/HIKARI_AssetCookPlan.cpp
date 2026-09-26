#include "Assets/Cooking/HIKARI_AssetCookInternal.h"
#include "Assets/Cooking/HIKARI_CookedContentManifest.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Post/HIKARI_PostAsset.h"
#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Assets/Models/MaterialBindings/HIKARI_ModelMaterialBindingAsset.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Gameplay/Dialogue/HIKARI_DialogueProjectStyle.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include <algorithm>
#include <deque>
#include <utility>

namespace HIKARI::ASSETS::COOKING::INTERNAL {
std::string MakePathKey(const std::filesystem::path& path) {
    return TEXT::ToLowerAsciiCopy(path.lexically_normal().generic_string());
}

bool IsPathWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root) {
    const std::string pathKey = MakePathKey(path);
    std::string rootKey = MakePathKey(root);
    while (!rootKey.empty() && rootKey.back() == '/') {
        rootKey.pop_back();
    }
    return !rootKey.empty() && (pathKey == rootKey ||
                                (pathKey.size() > rootKey.size() && pathKey.compare(0u, rootKey.size(), rootKey) == 0 &&
                                 pathKey[rootKey.size()] == '/'));
}

bool IsDeclaredProjectAssetRoot(const std::filesystem::path& root) {
    if (!IsSafeCookedRelativePath(root)) {
        return false;
    }
    const std::string key = MakePathKey(root);
    return key == "assets" || key.rfind("assets/", 0u) == 0u;
}

bool ResolveContainedFilePath(const std::filesystem::path& root, const std::filesystem::path& relativePath,
                              std::filesystem::path& outResolved, std::string& outMessage) {
    if (!IsSafeCookedRelativePath(relativePath)) {
        outMessage = "Cooked artifact path is unsafe: " + relativePath.generic_string();
        return false;
    }
    std::error_code ec{};
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) {
        outMessage = "Could not resolve cooked artifact root: " + ec.message();
        return false;
    }
    const std::filesystem::path canonicalFile = std::filesystem::weakly_canonical(root / relativePath, ec);
    if (ec) {
        outMessage = "Could not resolve cooked artifact path: " + ec.message();
        return false;
    }

    std::string rootKey = TEXT::ToLowerAsciiCopy(canonicalRoot.generic_string());
    const std::string fileKey = TEXT::ToLowerAsciiCopy(canonicalFile.generic_string());
    while (!rootKey.empty() && rootKey.back() == '/') {
        rootKey.pop_back();
    }
    if (rootKey.empty() || fileKey.size() <= rootKey.size() || fileKey.compare(0u, rootKey.size(), rootKey) != 0 ||
        fileKey[rootKey.size()] != '/') {
        outMessage = "Cooked artifact resolves outside its owned root: " + relativePath.generic_string();
        return false;
    }
    outResolved = canonicalFile;
    return true;
}

bool CollectSourceCookDependencies(const AssetRecord& record, const std::filesystem::path& projectRoot,
                                   AssetDatabase& assetDatabase, CookPlan& plan, std::string& outMessage) {
    if (record.type == AssetType::UiAnimation) {
        std::filesystem::path path;
        if (!ResolveContainedFilePath(projectRoot, record.sourcePath, path, outMessage))
            return false;
        UiAnimationAsset animation;
        return LoadUiAnimationAsset(path, animation, outMessage);
    }
    if (record.type == AssetType::PostEffect) {
        std::filesystem::path path;
        if (!ResolveContainedFilePath(projectRoot, record.sourcePath, path, outMessage))
            return false;
        POST::PostEffectDefinition effect;
        return POST::LoadPostEffectDefinition(path, effect, outMessage);
    }
    if (record.type == AssetType::PostProfile) {
        std::filesystem::path path;
        if (!ResolveContainedFilePath(projectRoot, record.sourcePath, path, outMessage))
            return false;
        POST::PostProfileAsset profile;
        if (!POST::LoadPostProfileAsset(path, profile, outMessage))
            return false;
        plan.sourceDependencies.insert_or_assign(record.guid.value, POST::CollectPostProfileDependencies(profile));
        return true;
    }
    if (record.type == AssetType::TextStyle) {
        std::filesystem::path path;
        if (!ResolveContainedFilePath(projectRoot, record.sourcePath, path, outMessage))
            return false;
        TextStyleAsset style;
        if (!LoadTextStyleAsset(path, style, outMessage))
            return false;
        plan.sourceDependencies.insert_or_assign(record.guid.value, CollectTextStyleDependencies(style));
        return true;
    }
    if (record.type == AssetType::Sequence) {
        std::filesystem::path path;
        if (!ResolveContainedFilePath(projectRoot, record.sourcePath, path, outMessage))
            return false;
        SequenceAsset asset;
        if (!LoadSequenceAsset(path, record.guid, asset, &outMessage))
            return false;
        plan.sourceDependencies.insert_or_assign(record.guid.value, CollectSequenceDependencies(asset.sequence));
        return true;
    }
    if (record.type != AssetType::ModelMaterialBinding) {
        return true;
    }
    std::filesystem::path sourcePath{};
    if (!ResolveContainedFilePath(projectRoot, record.sourcePath, sourcePath, outMessage)) {
        return false;
    }
    ASSETS::MODELS::ModelMaterialBindingAssetData data{};
    std::string error{};
    if (!ASSETS::MODELS::LoadModelMaterialBindingAsset(sourcePath, data, error)) {
        outMessage = "Could not read model material bindings: " + TEXT::PathToUtf8(record.sourcePath) + ": " + error;
        return false;
    }
    std::vector<AssetDependencyDesc> dependencies{};
    for (const auto& dependency : ASSETS::MODELS::CollectModelMaterialBindingDependencies(data)) {
        const AssetRecord* dependencyRecord = assetDatabase.FindByGuid(dependency.assetGuid);
        const AssetType expectedType = dependency.kind == ASSETS::MODELS::ModelMaterialBindingDependencyKind::Model
                                           ? AssetType::Model
                                           : AssetType::Material;
        if (dependencyRecord == nullptr || dependencyRecord->type != expectedType) {
            outMessage = "Missing or invalid " + dependency.role + " dependency of " +
                         TEXT::PathToUtf8(record.sourcePath) + ": " + dependency.assetGuid.value;
            return false;
        }
        dependencies.push_back(AssetDependencyDesc{dependency.assetGuid, {}, dependency.role});
    }
    plan.sourceDependencies.insert_or_assign(record.guid.value, std::move(dependencies));
    return true;
}

const std::vector<AssetDependencyDesc>& CookDependencies(const AssetRecord& record, const CookPlan& plan) {
    const auto it = plan.sourceDependencies.find(record.guid.value);
    return it != plan.sourceDependencies.end() ? it->second : record.artifactManifest.dependencies;
}

bool ValidateCueAudioPayload(const AssetCookOptions& options, const AssetDependencyDesc& dependency,
                             std::string& message) {
    const auto path = std::filesystem::path(dependency.path);
    const bool declared =
        std::any_of(options.runtimePayloadRoots.begin(), options.runtimePayloadRoots.end(),
                    [&](const auto& root) { return IsDeclaredProjectAssetRoot(root) && IsPathWithinRoot(path, root); });
    if (!declared) {
        message = "Sequence audio requires a runtimePayloadRoots entry covering: " + dependency.path;
        return false;
    }
    std::filesystem::path resolved;
    return ResolveContainedFilePath(options.projectRoot, path, resolved, message);
}

bool BuildCookPlan(const AssetCookOptions& options, AssetDatabase& assetDatabase, CookPlan& outPlan,
                   std::string& outMessage) {
    CookPlan plan{};
    if (!options.startupSceneGuid.empty() && !IsValidAssetGuid(options.startupSceneGuid)) {
        outMessage = "Startup Scene GUID must be 32 hexadecimal characters.";
        return false;
    }
    for (const std::string& sceneGuid : options.sceneGuids) {
        if (!IsValidAssetGuid(sceneGuid)) {
            outMessage = "Cooked Scene GUID must be 32 hexadecimal "
                         "characters: " +
                         sceneGuid;
            return false;
        }
        plan.sceneGuids.insert(sceneGuid);
    }

    if (plan.sceneGuids.empty()) {
        if (!options.startupSceneGuid.empty()) {
            outMessage = "Startup scene was set but no scenes were "
                         "selected for cooking.";
            return false;
        }
        for (const AssetRecord* record : assetDatabase.CollectAll()) {
            if (record != nullptr && IsValidAssetGuid(record->guid.value)) {
                plan.assetGuids.insert(record->guid.value);
                if (!CollectSourceCookDependencies(*record, options.projectRoot, assetDatabase, plan, outMessage)) {
                    return false;
                }
            }
        }
        outPlan = std::move(plan);
        return true;
    }

    if (options.startupSceneGuid.empty()) {
        outMessage = "Startup scene is required when cooking selected "
                     "scenes.";
        return false;
    }
    if (!plan.sceneGuids.contains(options.startupSceneGuid)) {
        outMessage = "Startup scene must be included in cooked scenes: " + options.startupSceneGuid;
        return false;
    }

    std::deque<std::string> pendingGuids{};
    auto queueRecord = [&](const AssetRecord* record) {
        if (record == nullptr || !IsValidAssetGuid(record->guid.value)) {
            if (outMessage.empty()) {
                outMessage = record ? "Asset has no valid metadata. Import it before exporting: " +
                                          TEXT::PathToUtf8(record->sourcePath)
                                    : "A referenced asset record is missing.";
            }
            ++plan.missingReferenceCount;
            return;
        }
        if (record->type == AssetType::Scene && !plan.sceneGuids.contains(record->guid.value)) {
            return;
        }
        if (plan.assetGuids.insert(record->guid.value).second) {
            pendingGuids.push_back(record->guid.value);
        }
    };

    SceneSerializer sceneSerializer{};
    for (const std::string& sceneGuid : plan.sceneGuids) {
        const AssetRecord* sceneRecord = assetDatabase.FindByGuid(AssetGuid{sceneGuid});
        if (sceneRecord == nullptr || sceneRecord->type != AssetType::Scene) {
            outMessage = "Cooked scene was not found in the asset "
                         "database: " +
                         sceneGuid;
            return false;
        }

        queueRecord(sceneRecord);
        SceneDocument document{};
        const std::filesystem::path scenePath = options.projectRoot / sceneRecord->sourcePath;
        if (!sceneSerializer.LoadFromFile(scenePath.lexically_normal().string(), document)) {
            outMessage = "Could not read scene while building cook plan: " + scenePath.generic_string();
            return false;
        }

        for (const auto& sequence : document.cinematics.sequences) {
            for (const auto& dependency : CollectSequenceDependencies(sequence))
                if (dependency.role == "Sequence.AudioCue" && !ValidateCueAudioPayload(options, dependency, outMessage))
                    return false;
        }
        AssetUsageSummary usage = AnalyzeAssetUsage(document, assetDatabase);
        for (const std::string& usedGuid : usage.usedGuids) {
            const AssetRecord* usedRecord = assetDatabase.FindByGuid(AssetGuid{usedGuid});
            if (usedRecord == nullptr) {
                outMessage = "Missing scene asset record: " + usedGuid;
                return false;
            }
            queueRecord(usedRecord);
        }
        plan.missingReferenceCount += usage.missingReferences.size();
        if (!usage.missingReferences.empty()) {
            const auto& missing = usage.missingReferences.front();
            outMessage = "Missing scene asset: " + missing.assetId + " (" + missing.owner + ", " + missing.role + ").";
            return false;
        }
    }

    const std::vector<const AssetRecord*> allRecords = assetDatabase.CollectAll();
    for (const std::filesystem::path& root : options.assetRoots) {
        if (!IsDeclaredProjectAssetRoot(root)) {
            outMessage = "Cook asset root must be a safe path below Assets: " + root.generic_string();
            return false;
        }
        size_t matched = 0u;
        for (const AssetRecord* record : allRecords) {
            if (record != nullptr && IsPathWithinRoot(record->sourcePath, root)) {
                queueRecord(record);
                ++matched;
            }
        }
        if (matched == 0u) {
            outMessage = "Cook asset root did not resolve any registered assets: " + root.generic_string();
            return false;
        }
    }

    const std::filesystem::path dialogueStylePath =
        options.projectRoot / std::string(DIALOGUE::kDialogueProjectStylePath);
    std::error_code dialogueStyleError{};
    if (std::filesystem::is_regular_file(dialogueStylePath, dialogueStyleError) && !dialogueStyleError) {
        DIALOGUE::DialogueProjectStyle dialogueStyle{};
        std::string dialogueStyleMessage{};
        if (!DIALOGUE::LoadDialogueProjectStyle(options.projectRoot, dialogueStyle, &dialogueStyleMessage)) {
            outMessage = "Could not read dialogue UI style while building "
                         "the cook plan: " +
                         dialogueStyleMessage;
            return false;
        }
        for (const auto& guid : {dialogueStyle.theme.speakerTextStyle, dialogueStyle.theme.bodyTextStyle,
                                 dialogueStyle.theme.choiceTextStyle}) {
            const auto* record = assetDatabase.FindByGuid(guid);
            if (!record || record->type != AssetType::TextStyle) {
                outMessage = "Dialogue UI references a missing TextStyle asset: " + guid.value;
                return false;
            }
            queueRecord(record);
        }
        for (const DIALOGUE::DialogueSpeakerProfile& speaker : dialogueStyle.speakers) {
            if (speaker.portraitAssetGuid.empty()) {
                continue;
            }
            const AssetRecord* portraitRecord = assetDatabase.FindByGuid(AssetGuid{speaker.portraitAssetGuid});
            if (portraitRecord == nullptr || portraitRecord->type != AssetType::Texture) {
                outMessage = "Dialogue speaker '" + speaker.id +
                             "' references a missing or non-texture portrait "
                             "asset: " +
                             speaker.portraitAssetGuid;
                return false;
            }
            queueRecord(portraitRecord);
        }
    } else if (dialogueStyleError && dialogueStyleError != std::errc::no_such_file_or_directory) {
        outMessage = "Could not inspect dialogue UI style while building "
                     "the cook plan: " +
                     dialogueStyleError.message();
        return false;
    }

    while (!pendingGuids.empty()) {
        const std::string guid = std::move(pendingGuids.front());
        pendingGuids.pop_front();

        const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{guid});
        if (record == nullptr) {
            outMessage = "Missing dependency record: " + guid;
            return false;
        }

        if (!CollectSourceCookDependencies(*record, options.projectRoot, assetDatabase, plan, outMessage)) {
            return false;
        }
        for (const AssetDependencyDesc& dependency : CookDependencies(*record, plan)) {
            if (ASSETS::SEMANTICS::IsSourceOnlyAssetDependencyRole(dependency.role)) {
                continue;
            }

            if (dependency.role == "Sequence.AudioCue") {
                if (!ValidateCueAudioPayload(options, dependency, outMessage))
                    return false;
                continue;
            }
            const AssetRecord* dependencyRecord = nullptr;
            if (dependency.guid.IsValid()) {
                dependencyRecord = assetDatabase.FindByGuid(dependency.guid);
            } else if (!dependency.path.empty()) {
                dependencyRecord = assetDatabase.FindByPath(dependency.path);
            }
            if (dependencyRecord == nullptr) {
                outMessage = "Missing dependency of " + TEXT::PathToUtf8(record->sourcePath) + ": " +
                             dependency.guid.value + " " + dependency.path + " (" + dependency.role + ").";
                return false;
            }
            queueRecord(dependencyRecord);
        }
    }

    if (plan.missingReferenceCount != 0u)
        return false;
    outPlan = std::move(plan);
    return true;
}

} // namespace HIKARI::ASSETS::COOKING::INTERNAL
