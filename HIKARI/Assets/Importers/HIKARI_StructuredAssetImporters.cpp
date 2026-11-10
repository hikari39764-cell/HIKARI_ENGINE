#include "Assets/Environment/HIKARI_EnvironmentAsset.h"
#include "Assets/Importers/HIKARI_StructuredAssetImporters.h"
#include "Assets/Importers/HIKARI_AssetImporterRegistry.h"
#include "Assets/Animation/HIKARI_AnimationStateMachineAsset.h"
#include "Assets/Camera/HIKARI_GameplayCameraAsset.h"
#include "RenderContracts/Color/HIKARI_ColorLutAsset.h"
#include "Assets/Lighting/HIKARI_IesProfileAsset.h"
#include "Assets/Dialogue/HIKARI_DialogueAsset.h"
#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Assets/Post/HIKARI_PostAsset.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Scene/Serialization/HIKARI_WorldInstanceFile.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include <array>
#include <cstring>
#include <fstream>
#include <set>
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
            AssetImportResult result = import_(path, record.guid);
            // 取り込み器は絶対パスで依存を返してよい。記録は project 相対にそろえる。
            for (AssetDependencyDesc& dependency : result.dependencies) {
                const std::filesystem::path dependencyPath = TEXT::PathFromUtf8(dependency.path);
                if (dependencyPath.is_absolute())
                    dependency.path =
                        TEXT::PathToUtf8(PROJECT_PATHS::MakeProjectRelativePath(context.projectRoot, dependencyPath));
            }
            return result;
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
    SceneCellFileState cells;
    AssetImportResult result;
    const bool loaded = SceneSerializer{}.LoadFromFile(path.string(), document, &cells);
    result.success = loaded && cells.unreadableCells.empty();
    // 分区場景の格子ファイルも入力：一つの格子だけが書き直されても取り込みは古くなる。
    std::set<WorldCellCoord> cellFiles = cells.unreadableCells;
    for (const auto& [cell, checksum] : cells.cellChecksums)
        cellFiles.insert(cell);
    for (WorldCellCoord cell : cellFiles)
        result.dependencies.push_back(
            {AssetGuid{}, TEXT::PathToUtf8(MakeSceneCellObjectsPath(path, cell)), "SourceCompanion"});
    // モジュールが場面に添えた内容（地形の瓦片など）。物体の格子ファイル以外は全部入力にする。
    std::error_code cellError{};
    for (const auto& entry :
         std::filesystem::directory_iterator(ASSETS::SEMANTICS::MakeSceneCellDirectoryPath(path), cellError)) {
        const std::string name = TEXT::PathToUtf8(entry.path().filename());
        if (entry.is_regular_file(cellError) && !name.ends_with(kSceneCellObjectsSuffix)) {
            result.dependencies.push_back({AssetGuid{}, TEXT::PathToUtf8(entry.path()), "SourceCompanion"});
        }
    }
    // 実例集の模型は場面の物体から辿れない。依存に書いておけば、書き出しがここから模型を拾う。
    std::vector<GameProjectWorldInstanceSetDesc> instanceSets;
    std::string instanceError;
    std::unordered_set<std::string> instanceModels;
    const std::filesystem::path instanceSetsPath =
        ASSETS::SEMANTICS::MakeSceneCellDirectoryPath(path) / kWorldInstanceSetsFileName;
    std::error_code instanceSetsError;
    if (std::filesystem::is_regular_file(instanceSetsPath, instanceSetsError) &&
        ReadWorldInstanceSetsFile(instanceSetsPath, instanceSets, instanceError)) {
        for (const GameProjectWorldInstanceSetDesc& set : instanceSets) {
            for (const char* model : {set.modelAssetGuid, set.farModelAssetGuid}) {
                if (IsValidAssetGuid(model) && instanceModels.insert(model).second) {
                    result.dependencies.push_back({AssetGuid{model}, {}, "Scene.WorldInstanceModel"});
                }
            }
        }
    }
    result.message = result.success ? "Scene asset validated"
                     : loaded       ? "Scene has " + std::to_string(cells.unreadableCells.size()) +
                                    " unreadable cell file(s): " + path.generic_string()
                              : "Scene asset validation failed: " + path.generic_string();
    result.diagnosticsJson = nlohmann::json{
        {"kind", "Scene"},
        {"valid", result.success},
        {"objectCount", document.objects.size()},
        {"cellCount",
         cellFiles.size()}}.dump();
    return result;
}

// モジュールの資産：JSON として読めることを確かめ、値の中の GUID を依存として集める。
void CollectJsonGuids(const nlohmann::json& node, std::unordered_set<std::string>& outGuids) {
    if (node.is_string()) {
        if (const std::string& text = node.get_ref<const std::string&>(); IsValidAssetGuid(text)) {
            outGuids.insert(text);
        }
    } else if (node.is_structured()) {
        for (const nlohmann::json& child : node) {
            CollectJsonGuids(child, outGuids);
        }
    }
}

AssetImportResult ImportModuleAsset(const std::filesystem::path& path, const AssetGuid& guid) {
    AssetImportResult result;
    nlohmann::json document;
    std::string error;
    result.success = SERIALIZATION::JSON::ReadJsonFile(path, document, &error);
    result.message = result.success ? "Module asset validated" : "Module asset is not readable JSON: " + error;
    if (!result.success) {
        return result;
    }
    std::unordered_set<std::string> guids;
    CollectJsonGuids(document, guids);
    guids.erase(guid.value);
    for (const std::string& reference : guids) {
        result.dependencies.push_back({AssetGuid{reference}, {}, "ModuleAsset.Reference"});
    }
    const auto* type = ASSETS::SEMANTICS::FindModuleAssetTypeForPath(path);
    result.diagnosticsJson =
        nlohmann::json{{"kind", "ModuleAsset"}, {"typeId", type != nullptr ? type->typeId : std::string{}}}.dump();
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
    const auto addModelDependency = [&result, &dependencyGuids](const AssetGuid& modelAssetId, const char* role) {
        if (modelAssetId.value.empty() || !dependencyGuids.insert(modelAssetId.value).second) {
            return;
        }
        result.dependencies.push_back(AssetDependencyDesc{modelAssetId, {}, role});
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

AssetImportResult ImportGameplayCamera(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    CAMERA::GameplayCameraProfile profile;

    result.success = LoadGameplayCameraAsset(sourcePath, profile, result.message);
    if (result.success)
        result.message = "Gameplay camera profile validated";
    result.diagnosticsJson = nlohmann::json{{"valid", result.success}, {"modeCount", profile.modes.size()}}.dump();
    return result;
}

AssetImportResult ImportPostEffect(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    POST::PostEffectDefinition effect;
    result.success = POST::LoadPostEffectDefinition(sourcePath, effect, result.message);
    if (result.success) {
        result.message = "Post effect validated";
        result.dependencies = POST::CollectPostEffectDependencies(effect);
    }
    result.diagnosticsJson = nlohmann::json{
        {"valid", result.success},
        {"passCount", effect.passes.size()},
        {"parameterCount",
         effect.params.size()}}.dump();
    return result;
}

AssetImportResult ImportPostProfile(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    POST::PostProfileAsset profile;
    result.success = POST::LoadPostProfileAsset(sourcePath, profile, result.message);
    if (result.success) {
        result.message = "Post profile validated";
        result.dependencies = POST::CollectPostProfileDependencies(profile);
    }
    result.diagnosticsJson =
        nlohmann::json{{"valid", result.success}, {"effect", profile.effectId}, {"valueCount", profile.values.size()}}
            .dump();
    return result;
}

AssetImportResult ImportEnvironment(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    ENVIRONMENT::EnvironmentProfileAsset asset;
    result.success = ENVIRONMENT::LoadEnvironmentProfileAsset(sourcePath, asset, result.message);
    if (result.success) {
        result.message = "Environment profile validated";
        result.dependencies = ENVIRONMENT::CollectEnvironmentProfileDependencies(asset);
    }
    result.diagnosticsJson = nlohmann::json{{"valid", result.success}, {"name", asset.name}}.dump();
    return result;
}

AssetImportResult ImportRenderStyle(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    RENDERSTYLE::RenderStyleAsset style;
    result.success = RENDERSTYLE::LoadRenderStyleAsset(sourcePath, style, result.message);
    if (result.success) {
        result.message = "Render style validated";
        result.dependencies = RENDERSTYLE::CollectRenderStyleDependencies(style);
    }
    result.diagnosticsJson =
        nlohmann::json{{"valid", result.success}, {"name", style.name}, {"postLayerCount", style.post.size()}}.dump();
    return result;
}

AssetImportResult ImportColorLut(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    ASSETS::COLOR::ColorLutAsset lut;
    result.success = ASSETS::COLOR::LoadColorLutAsset(sourcePath, lut, result.message);
    if (result.success) {
        result.message = "Color LUT validated";
    }
    result.diagnosticsJson = nlohmann::json{{"valid", result.success}, {"title", lut.title}, {"size", lut.size}}.dump();
    return result;
}

AssetImportResult ImportLightProfile(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    ASSETS::LIGHTING::IesProfileAsset profile;
    result.success = ASSETS::LIGHTING::LoadIesProfileAsset(sourcePath, profile, result.message);
    if (result.success) {
        result.message = "IES light profile validated";
    }
    result.diagnosticsJson = nlohmann::json{
        {"valid", result.success},
        {"verticalAngles", profile.verticalAngles.size()},
        {"horizontalAngles", profile.horizontalAngles.size()},
        {"maximumCandela",
         profile.maximumCandela}}.dump();
    return result;
}

AssetImportResult ImportWeather(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    ENVIRONMENT::WeatherPresetAsset asset;
    result.success = ENVIRONMENT::LoadWeatherPresetAsset(sourcePath, asset, result.message);
    if (result.success) {
        result.message = "Weather preset validated";
    }
    result.diagnosticsJson = nlohmann::json{{"valid", result.success}, {"name", asset.name}}.dump();
    return result;
}
// 音声：元の文件をそのまま使い、取り込みでは中身が読める形式かだけを頭の数バイトで確かめる。
AssetImportResult ImportAudio(const std::filesystem::path& sourcePath, const AssetGuid&) {
    AssetImportResult result;
    std::array<unsigned char, 12> head{};
    std::ifstream input(sourcePath, std::ios::binary);
    input.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    const auto read = static_cast<size_t>(input.gcount());
    const auto starts = [&](const char* magic, size_t offset) {
        const size_t length = std::strlen(magic);
        return read >= offset + length && std::memcmp(head.data() + offset, magic, length) == 0;
    };
    std::string format;
    if (starts("RIFF", 0) && starts("WAVE", 8))
        format = "wav";
    else if (starts("ID3", 0) || (read >= 2 && head[0] == 0xFF && (head[1] & 0xE0) == 0xE0))
        format = "mp3";
    else if (starts("OggS", 0))
        format = "ogg";
    else if (starts("fLaC", 0))
        format = "flac";
    std::error_code error;
    const auto bytes = std::filesystem::file_size(sourcePath, error);
    result.success = !format.empty() && !error && bytes > 0;
    result.message = result.success
                         ? "Audio file validated"
                         : "Audio file is not a readable wav, mp3, ogg or flac: " + sourcePath.generic_string();
    result.diagnosticsJson = nlohmann::json{{"valid", result.success},
                                            {"format", format},
                                            {"bytes", error ? 0 : static_cast<uint64_t>(bytes)}}
                                 .dump();
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
    add(ImporterKind::PostEffect, nullptr, ImportPostEffect);
    add(ImporterKind::PostProfile, nullptr, ImportPostProfile);
    add(ImporterKind::Environment, nullptr, ImportEnvironment);
    add(ImporterKind::Weather, nullptr, ImportWeather);
    add(ImporterKind::RenderStyle, nullptr, ImportRenderStyle);
    add(ImporterKind::ColorLut, nullptr, ImportColorLut);
    add(ImporterKind::LightProfile, nullptr, ImportLightProfile);
    add(ImporterKind::Audio, nullptr, ImportAudio);
    add(ImporterKind::ModuleAsset, nullptr, ImportModuleAsset);
}
} // namespace HIKARI
