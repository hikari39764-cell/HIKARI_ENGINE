#include "Assets/Importers/HIKARI_StructuredAssetImporters.h"
#include "Assets/Importers/HIKARI_AssetImporterRegistry.h"
#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include <unordered_set>

namespace HIKARI {
namespace {
using ImporterKind = ASSETS::SEMANTICS::AssetImporterKind;
using ImportSource = AssetImportResult (*)(const std::filesystem::path&, const AssetGuid&);

class StructuredAssetImporter final : public IAssetImporter {
public:
    StructuredAssetImporter(ImporterKind kind, const char* loader, ImportSource import)
        : kind_(kind), loader_(loader), import_(import) {}
    ImporterKind GetImporterKind() const noexcept override { return kind_; }
    AssetMeta CreateDefaultMeta(const std::filesystem::path& path, const AssetGuid& guid) const override {
        auto meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(path, guid, kind_);
        if (loader_)
            meta.importSettingsJson = nlohmann::json{{"runtimeLoader", loader_}, {"sourceAuthoritative", true}}.dump(2);
        return meta;
    }
    AssetImportResult Import(const AssetRecord& record, const AssetImportContext& context) override {
        const auto path = record.sourcePath.is_absolute()
                              ? record.sourcePath
                              : (context.projectRoot / record.sourcePath).lexically_normal();
        try {
            return import_(path, record.guid);
        } catch (const nlohmann::json::exception& error) {
            AssetImportResult result;
            result.message = std::string("Malformed asset: ") + error.what();
            return result;
        }
    }

private:
    ImporterKind kind_;
    const char* loader_;
    ImportSource import_;
};

AssetImportResult ImportScene(const std::filesystem::path& path, const AssetGuid&) {
    SceneDocument document;
    AssetImportResult result;
    result.success = SceneSerializer{}.LoadFromFile(path.string(), document);
    result.message =
        result.success ? "Scene asset validated" : "Scene asset validation failed: " + path.generic_string();
    result.diagnosticsJson =
        nlohmann::json{{"kind", "Scene"}, {"valid", result.success}, {"objectCount", document.objects.size()}}.dump();
    return result;
}

AssetImportResult ImportSequence(const std::filesystem::path& sourcePath, const AssetGuid& guid) {
    AssetImportResult result{};
    SequenceAsset asset{};
    std::string error{};

    result.success = LoadSequenceAsset(sourcePath, guid, asset, &error);
    if (result.success)
        result.dependencies = CollectSequenceDependencies(asset.sequence);
    result.message = result.success ? "Sequence asset validated" : "Sequence asset validation failed: " + error;
    result.diagnosticsJson = nlohmann::json{
        {"kind", "Sequence"},
        {"durationSeconds", asset.sequence.durationSeconds},
        {"bindingCount", asset.sequence.bindings.size()},
        {"valid",
         result.success}}.dump(2);
    return result;
}

AssetImportResult ImportAnimationStateMachine(const std::filesystem::path& sourcePath, const AssetGuid& guid) {
    AssetImportResult result{};
    AnimationStateMachineAsset asset{};
    std::string error{};

    result.success = LoadAnimationStateMachineAsset(sourcePath, guid, asset, &error);
    const auto issues = result.success ? ANIMATION::ValidateAnimationStateMachine(asset.definition)
                                       : std::vector<ANIMATION::AnimationStateMachineValidationIssue>{};
    size_t errorCount = 0u;
    for (const auto& issue : issues) {
        if (!issue.error)
            continue;
        ++errorCount;
        if (error.empty())
            error = issue.message;
    }

    std::unordered_set<std::string> dependencyGuids{};
    const auto addModelDependency = [&result, &dependencyGuids](const AssetId& modelAssetId, const char* role) {
        if (modelAssetId.value.empty() || !dependencyGuids.insert(modelAssetId.value).second) {
            return;
        }
        result.dependencies.push_back(AssetDependencyDesc{AssetGuid{modelAssetId.value}, {}, role});
    };
    addModelDependency(asset.definition.previewModelAssetId, "Animation Preview Model");
    for (const ANIMATION::AnimationState& state : asset.definition.states) {
        if (const auto* clip = std::get_if<ANIMATION::AnimationClipMotion>(&state.motion)) {
            addModelDependency(clip->clip.modelAssetId, "Animation Clip Model");
            continue;
        }
        const auto& blendTree = std::get<ANIMATION::AnimationBlendTree1DMotion>(state.motion);
        for (const auto& sample : blendTree.samples) {
            addModelDependency(sample.clip.modelAssetId, "Animation Blend Sample Model");
        }
    }
    result.success = result.success && errorCount == 0u;
    result.message = result.success ? "Animation state machine asset validated"
                                    : "Animation state machine validation failed: " + error;
    result.diagnosticsJson = nlohmann::json{
        {"kind", "AnimationStateMachine"},
        {"stateCount", asset.definition.states.size()},
        {"parameterCount", asset.definition.parameters.size()},
        {"transitionCount", asset.definition.transitions.size()},
        {"validationIssueCount", issues.size()},
        {"validationErrorCount", errorCount},
        {"valid",
         result.success}}.dump(2);
    return result;
}

AssetImportResult ImportDialogue(const std::filesystem::path& sourcePath, const AssetGuid& guid) {
    AssetImportResult result{};
    DialogueAsset asset{};
    std::string error{};

    result.success = LoadDialogueAsset(sourcePath, guid, asset, &error);
    const auto issues = result.success ? DIALOGUE::ValidateDialogue(asset.definition)
                                       : std::vector<DIALOGUE::DialogueValidationIssue>{};
    size_t errorCount = 0u;
    for (const auto& issue : issues) {
        if (issue.error) {
            ++errorCount;
            if (error.empty())
                error = issue.message;
        }
    }
    result.success = result.success && errorCount == 0u;
    result.message = result.success ? "Dialogue asset validated" : "Dialogue asset validation failed: " + error;
    result.diagnosticsJson =
        nlohmann::json{
            {"kind", "Dialogue"},
            {"nodeCount", asset.definition.nodes.size()},
            {"validationIssueCount", issues.size()},
            {"validationErrorCount", errorCount},
            {"valid", result.success},
        }
            .dump(2);
    return result;
}

AssetImportResult ImportGameplayCamera(const std::filesystem::path& sourcePath, const AssetGuid& guid) {
    AssetImportResult result;
    CAMERA::GameplayCameraProfile profile;

    result.success = LoadGameplayCameraAsset(sourcePath, profile, result.message);
    if (result.success)
        result.message = "Gameplay camera profile validated";
    result.diagnosticsJson = nlohmann::json{{"valid", result.success}, {"modeCount", profile.modes.size()}}.dump();
    return result;
}

AssetImportResult ImportUiAnimation(const std::filesystem::path& sourcePath, const AssetGuid& guid) {
    AssetImportResult result;
    UiAnimationAsset asset;
    result.success = LoadUiAnimationAsset(sourcePath, asset, result.message);
    if (result.success)
        result.message = "UI animation validated";
    return result;
}
} // namespace

void RegisterStructuredAssetImporters(AssetImporterRegistry& registry) {
    const auto add = [&](ImporterKind kind, const char* loader, ImportSource import) {
        registry.Register(std::make_unique<StructuredAssetImporter>(kind, loader, import));
    };
    add(ImporterKind::Scene, "SceneSerializer", ImportScene);
    add(ImporterKind::Sequence, "SequenceAssetStore", ImportSequence);
    add(ImporterKind::AnimationStateMachine, "AnimationStateMachineAssetStore", ImportAnimationStateMachine);
    add(ImporterKind::Dialogue, "DialogueAssetStore", ImportDialogue);
    add(ImporterKind::GameplayCamera, nullptr, ImportGameplayCamera);
    add(ImporterKind::UiAnimation, nullptr, ImportUiAnimation);
}
} // namespace HIKARI
