#include "Assets/Importers/HIKARI_AnimationStateMachineAssetImporter.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include <json.hpp>

#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"
namespace HIKARI {

    ASSETS::SEMANTICS::AssetImporterKind
        AnimationStateMachineAssetImporter::GetImporterKind()
        const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::AnimationStateMachine;
    }

    AssetMeta AnimationStateMachineAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {
        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson = nlohmann::json{
            { "runtimeLoader", "AnimationStateMachineAssetStore" },
            { "sourceAuthoritative", true }
        }.dump(2);
        return meta;
    }

    AssetImportResult AnimationStateMachineAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {
        AssetImportResult result{};
        AnimationStateMachineAsset asset{};
        std::string error{};
        const std::filesystem::path sourcePath = record.sourcePath.is_absolute()
            ? record.sourcePath
            : (context.projectRoot / record.sourcePath).lexically_normal();
        result.success = LoadAnimationStateMachineAsset(
            sourcePath,
            record.guid,
            asset,
            &error);
        const auto issues = result.success
            ? ANIMATION::ValidateAnimationStateMachine(asset.definition)
            : std::vector<ANIMATION::AnimationStateMachineValidationIssue>{};
        size_t errorCount = 0u;
        for (const auto& issue : issues) {
            if (!issue.error) continue;
            ++errorCount;
            if (error.empty()) error = issue.message;
        }

        std::unordered_set<std::string> dependencyGuids{};
        const auto addModelDependency =
            [&result, &dependencyGuids](
                const AssetId& modelAssetId,
                const char* role) {
                if (modelAssetId.value.empty() ||
                    !dependencyGuids.insert(modelAssetId.value).second) {
                    return;
                }
                result.dependencies.push_back(AssetDependencyDesc{
                    AssetGuid{ modelAssetId.value },
                    {},
                    role
                });
            };
        addModelDependency(
            asset.definition.previewModelAssetId,
            "Animation Preview Model");
        for (const ANIMATION::AnimationState& state :
                asset.definition.states) {
            if (const auto* clip = std::get_if<
                    ANIMATION::AnimationClipMotion>(&state.motion)) {
                addModelDependency(
                    clip->clip.modelAssetId,
                    "Animation Clip Model");
                continue;
            }
            const auto& blendTree = std::get<
                ANIMATION::AnimationBlendTree1DMotion>(state.motion);
            for (const auto& sample : blendTree.samples) {
                addModelDependency(
                    sample.clip.modelAssetId,
                    "Animation Blend Sample Model");
            }
        }
        result.success = result.success && errorCount == 0u;
        result.message = result.success
            ? "Animation state machine asset validated"
            : "Animation state machine validation failed: " + error;
        result.diagnosticsJson = nlohmann::json{
            { "kind", "AnimationStateMachine" },
            { "stateCount", asset.definition.states.size() },
            { "parameterCount", asset.definition.parameters.size() },
            { "transitionCount", asset.definition.transitions.size() },
            { "validationIssueCount", issues.size() },
            { "validationErrorCount", errorCount },
            { "valid", result.success }
        }.dump(2);
        return result;
    }

} // namespace HIKARI
