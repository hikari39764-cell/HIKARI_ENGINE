#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <exception>
#include <iterator>
#include <string>
#include <utility>

#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Environment/HIKARI_EnvironmentPanelSections.h"
#include "Editor/History/HIKARI_EnvironmentHistoryCommand.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Serialization/Environment/HIKARI_SceneEnvironmentJson.h"
#include "Tools/Baking/HIKARI_LightingBakeReport.h"

namespace HIKARI {
namespace {
using EDITOR::EditorOperationKind;
using EDITOR::EditorOperationParameterType;

nlohmann::json SerializeEnvironment(const SceneEnvironment& environment) {
    nlohmann::json out = nlohmann::json::object();
    SCENE::SERIALIZATION::SerializeSceneEnvironmentJson(environment, out);
    return out;
}

// パッチのキーは現在の環境 JSON に存在するものに限る。配列と値は丸ごと置き換える。
bool FindUnknownKey(const nlohmann::json& patch, const nlohmann::json& current, const std::string& path,
                    std::string& outPath) {
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        const std::string key = path.empty() ? it.key() : path + "." + it.key();
        const auto found = current.find(it.key());
        if (found == current.end()) {
            outPath = key;
            return true;
        }
        if (it->is_object() && found->is_object() && FindUnknownKey(*it, *found, key, outPath)) {
            return true;
        }
    }
    return false;
}

// 要求した値と、正規化後に保存された値が違う項目を返す。
void CollectAdjusted(const nlohmann::json& requested, const nlohmann::json& stored, const std::string& path,
                     nlohmann::json& outAdjusted) {
    for (auto it = requested.begin(); it != requested.end(); ++it) {
        const std::string key = path.empty() ? it.key() : path + "." + it.key();
        const auto found = stored.find(it.key());
        if (found == stored.end()) {
            continue;
        }
        if (it->is_object() && found->is_object()) {
            CollectAdjusted(*it, *found, key, outAdjusted);
        } else if (*it != *found) {
            outAdjusted[key] = *found;
        }
    }
}
} // namespace

void DocumentSceneEditorController::RegisterEnvironmentOperations() {
    const auto commitEnvironment = [this](DocumentSceneBase& scene, SceneEnvironment edited, std::string label) {
        SceneEnvironment before = scene.GetSceneDocument().environment;
        const bool externalDirtyBefore = IsDirtyOutsideHistory(scene);
        scene.GetSceneDocument().environment = edited;
        CommitAppliedDocumentCommand(
            scene, EDITOR::MakeEnvironmentHistoryCommand(std::move(label), std::move(before), std::move(edited)), 0u,
            externalDirtyBefore, EDITOR::EditorDocumentImpact::Environment);
    };

    EDITOR::EditorOperationDefinition describe{
        "environment.describe",
        "Return the scene environment (sky, sun, fog, bloom, tone mapping, SSR, AO, global post) in its saved JSON form. Edit it with environment.edit.",
        "scene",
        {{"section", EditorOperationParameterType::String, false,
          "Optional top-level section such as sky, fog, toneMapping or post"}},
        [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (!aiOperationScene_)
                return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
            const nlohmann::json environment = SerializeEnvironment(aiOperationScene_->GetSceneDocument().environment);
            nlohmann::json sections = nlohmann::json::array();
            for (auto it = environment.begin(); it != environment.end(); ++it)
                sections.push_back(it.key());
            nlohmann::json presets = nlohmann::json::array();
            for (const auto& preset : EDITOR::ENVIRONMENT_PANEL::kEnvironmentPresets)
                presets.push_back(preset.id);
            const std::string section = parameters.value("section", std::string{});
            if (section.empty())
                return EDITOR::OperationOk(
                    "Scene environment.", {{"environment", environment}, {"sections", sections}, {"presets", presets}});
            const auto found = environment.find(section);
            if (found == environment.end())
                return EDITOR::OperationFailure("UNKNOWN_SECTION", "Unknown environment section: " + section);
            return EDITOR::OperationOk("Scene environment section.",
                                       {{"section", section}, {"value", *found}, {"sections", sections}});
        }};
    describe.entryPoint = true;
    describe.searchTerms = {"environment", "sky", "fog", "lighting", "post", "环境", "天空", "雾", "後処理"};
    operationRegistry_.Register(std::move(describe), EditorOperationKind::Query);

    operationRegistry_.Register(
        {"environment.edit",
         "Merge a partial JSON object into the scene environment (same keys as environment.describe; arrays replace). One undo step; reports values the engine normalized.",
         "scene",
         {{"patch", EditorOperationParameterType::Json, true,
           "Partial environment object, for example {\"fog\":{\"density\":0.02}}"}},
         [this, commitEnvironment](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (!aiOperationScene_)
                 return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
             const nlohmann::json& patch = parameters.at("patch");
             if (!patch.is_object() || patch.empty())
                 return EDITOR::OperationFailure("INVALID_PATCH", "patch must be a non-empty object.");
             auto& scene = *aiOperationScene_;
             const nlohmann::json current = SerializeEnvironment(scene.GetSceneDocument().environment);
             std::string unknown;
             if (FindUnknownKey(patch, current, {}, unknown))
                 return EDITOR::OperationFailure("UNKNOWN_FIELD", "Unknown environment field: " + unknown);
             nlohmann::json merged = current;
             merged.merge_patch(patch);
             SceneEnvironment edited = scene.GetSceneDocument().environment;
             try {
                 SCENE::SERIALIZATION::DeserializeSceneEnvironmentJson(merged, edited);
             } catch (const std::exception& error) {
                 return EDITOR::OperationFailure("INVALID_VALUE", error.what());
             }
             const nlohmann::json stored = SerializeEnvironment(edited);
             if (stored == current)
                 return EDITOR::OperationOk("Environment unchanged.", {{"changed", false}});
             nlohmann::json adjusted = nlohmann::json::object();
             CollectAdjusted(patch, stored, {}, adjusted);
             commitEnvironment(scene, std::move(edited), "AI Edit Environment");
             return EDITOR::OperationOk("Environment updated.", {{"changed", true}, {"adjusted", std::move(adjusted)}});
         }},
        EditorOperationKind::Mutation);

    operationRegistry_.Register(
        {"environment.preset.apply",
         "Apply a built-in environment preset listed by environment.describe. One undo step.",
         "scene",
         {{"preset", EditorOperationParameterType::String, true, "Preset id"}},
         [this, commitEnvironment](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (!aiOperationScene_)
                 return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
             const std::string id = parameters.value("preset", std::string{});
             const auto& presets = EDITOR::ENVIRONMENT_PANEL::kEnvironmentPresets;
             for (int index = 0; index < static_cast<int>(std::size(presets)); ++index) {
                 if (id != presets[index].id)
                     continue;
                 SceneEnvironment edited = aiOperationScene_->GetSceneDocument().environment;
                 EDITOR::ENVIRONMENT_PANEL::ApplyEnvironmentPreset(edited, index);
                 commitEnvironment(*aiOperationScene_, std::move(edited), "AI Apply Environment Preset");
                 return EDITOR::OperationOk("Environment preset applied: " + id);
             }
             return EDITOR::OperationFailure("UNKNOWN_PRESET", "Unknown environment preset: " + id);
         }},
        EditorOperationKind::Mutation);

    EDITOR::EditorOperationDefinition bake{
        "lighting.bake.start",
        "Bake reflection probes, irradiance volumes and lightmaps of the current scene on the GPU and write the results into the project. Poll lighting.bake.status.",
        "scene",
        {{"force", EditorOperationParameterType::Bool, false, "Rebake items whose inputs did not change"},
         {"targetObjectId", EditorOperationParameterType::Int, false,
          "Bake only this probe, volume or lightmapped object"}},
        [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
            if (!aiOperationScene_)
                return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
            if (aiOperationScene_->IsRuntimePlayActive())
                return EDITOR::OperationFailure("PLAY_ACTIVE", "Stop Play before baking lighting.");
            if (TOOLS::BAKING::IsLightingBakeJobRunning(aiOperationScene_->GetLightingBakeJobState()))
                return EDITOR::OperationFailure("BAKE_RUNNING", "A lighting bake is already running.");
            if (!aiOperationScene_->RequestLightingBake(parameters.value("force", false),
                                                        SceneObjectId{parameters.value("targetObjectId", uint64_t{})}))
                return EDITOR::OperationFailure("BAKE_REJECTED",
                                                "The scene has nothing to bake or rejected the request.");
            return EDITOR::OperationOk("Lighting bake started; query lighting.bake.status for progress.",
                                       {{"statusAction", "lighting.bake.status"}});
        }};
    bake.permission = EDITOR::EditorOperationPermission::ModifyProjectFiles;
    operationRegistry_.Register(std::move(bake), EditorOperationKind::Session);

    operationRegistry_.Register({"lighting.bake.status",
                                 "Report the lighting bake state and the latest bake report.",
                                 "scene",
                                 {},
                                 [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
                                     if (!aiOperationScene_)
                                         return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
                                     const auto state = aiOperationScene_->GetLightingBakeJobState();
                                     nlohmann::json data{{"state", TOOLS::BAKING::ToString(state)},
                                                         {"running", TOOLS::BAKING::IsLightingBakeJobRunning(state)}};
                                     if (aiOperationScene_->HasLastLightingBakeReport()) {
                                         const auto& report = aiOperationScene_->GetLastLightingBakeReport();
                                         data["report"] = {{"success", report.success},
                                                           {"items", report.bakeItemCount},
                                                           {"completed", report.completedItemCount},
                                                           {"reused", report.reusedItemCount},
                                                           {"current", report.currentItemName},
                                                           {"warnings", report.warnings},
                                                           {"errors", report.errors}};
                                     }
                                     return EDITOR::OperationOk("Lighting bake status.", std::move(data));
                                 }},
                                EditorOperationKind::Query);
}

} // namespace HIKARI
