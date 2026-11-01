#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include <filesystem>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/RenderStyle/HIKARI_RenderStyleAssetStore.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/History/HIKARI_EnvironmentHistoryCommand.h"
#include "Project/HIKARI_ProjectContext.h"
#include "Render3D/Pipeline/HIKARI_RenderFramePipeline.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Tools/Baking/HIKARI_LightingBakeReport.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

namespace HIKARI {
namespace {
using EDITOR::EditorOperationKind;
using EDITOR::EditorOperationParameterType;

nlohmann::json ListRenderStyles(const AssetDatabase& database) {
    nlohmann::json list = nlohmann::json::array();
    for (const RENDERSTYLE::BuiltinRenderStyleContent& content : RENDERSTYLE::GetBuiltinRenderStyles()) {
        list.push_back({{"id", content.id}, {"name", content.displayName}, {"builtin", true}});
    }
    for (const AssetRecord* record : database.CollectByType(AssetType::RenderStyle)) {
        list.push_back({{"id", record->guid.value}, {"name", GetAssetRecordDisplayName(*record)}, {"builtin", false}});
    }
    return list;
}

nlohmann::json ListEnvironmentAssets(AssetType type, const AssetDatabase& database) {
    nlohmann::json list = nlohmann::json::array();
    const auto builtin = type == AssetType::Weather       ? ENVIRONMENT::GetBuiltinWeatherPresets()
                         : type == AssetType::Environment ? ENVIRONMENT::GetBuiltinEnvironmentProfiles()
                                                          : std::span<const ENVIRONMENT::BuiltinEnvironmentContent>{};
    for (const ENVIRONMENT::BuiltinEnvironmentContent& content : builtin) {
        list.push_back({{"id", content.id}, {"name", content.displayName}, {"builtin", true}});
    }
    for (const AssetRecord* record : database.CollectByType(type)) {
        list.push_back({{"id", record->guid.value}, {"name", GetAssetRecordDisplayName(*record)}, {"builtin", false}});
    }
    return list;
}

// 実行時の状態（試し再生や Play で進んだ時刻と天気）。シーン文書には保存されない。
nlohmann::json DescribeEnvironmentRuntime(DocumentSceneBase& scene) {
    ENVIRONMENT::EnvironmentRuntime& runtime = scene.GetEnvironmentRuntime();
    const ENVIRONMENT::EnvironmentFrameState state = runtime.Simulation().GetFrameState();
    return {{"time",
             {{"year", state.time.year},
              {"month", state.time.month},
              {"day", state.time.day},
              {"localHours", state.time.localHours}}},
            {"sunAltitudeDegrees", state.celestial.sunAltitudeDegrees},
            {"moonIlluminatedFraction", state.celestial.moonIlluminatedFraction},
            {"primaryLight", scene.GetSceneEnvironment().frame.moonIsPrimary ? "moon" : "sun"},
            {"weather", runtime.GetWeatherId()},
            {"weatherTransition", state.weatherTransitionProgress},
            {"surface",
             {{"wetness", state.surface.wetness},
              {"puddles", state.surface.puddles},
              {"snowCover", state.surface.snowCover}}},
            {"error", runtime.GetError()}};
}
} // namespace

void DocumentSceneEditorController::RegisterEnvironmentOperations() {
    EDITOR::EditorOperationDefinition describe{
        "environment.describe",
        "Return the scene environment binding (profile, weather, render style, start time, location, time scale), the "
        "runtime time/weather state and the available profiles, weathers, render styles and color LUTs (.cube, set "
        "through the profile colorGrading.lut). Edit the binding with environment.edit, the profile or weather contents "
        "with environment.asset.* and render styles with renderstyle.*.",
        "scene",
        {},
        [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
            if (!aiOperationScene_)
                return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
            DocumentSceneBase& scene = *aiOperationScene_;
            const AssetDatabase& database = scene.GetAssetDatabase();
            return EDITOR::OperationOk(
                "Scene environment.",
                {{"binding", ENVIRONMENT::WriteSceneEnvironmentBinding(scene.GetSceneDocument().environment)},
                 {"profile", scene.GetEnvironmentRuntime().GetBinding().profileId},
                 {"runtime", DescribeEnvironmentRuntime(scene)},
                 {"profiles", ListEnvironmentAssets(AssetType::Environment, database)},
                 {"weathers", ListEnvironmentAssets(AssetType::Weather, database)},
                 {"renderStyles", ListRenderStyles(database)},
                 {"colorLuts", ListEnvironmentAssets(AssetType::ColorLut, database)}});
        }};
    describe.entryPoint = true;
    describe.searchTerms = {"environment", "sky",  "weather", "time of day", "fog", "lighting",
                            "环境",        "天气", "时间",    "天空",        "雾",  "天気"};
    operationRegistry_.Register(std::move(describe), EditorOperationKind::Query);

    operationRegistry_.Register(
        {"environment.edit",
         "Merge a partial JSON object into the scene environment binding (keys from environment.describe binding: "
         "profile, weather, renderStyle, timeScale, time, location). One undo step; reports values the engine "
         "normalized.",
         "scene",
         {{"patch", EditorOperationParameterType::Json, true,
           "Partial binding, for example {\"time\":{\"localHours\":18.5},\"weather\":\"engine.weather.rain\"}"}},
         [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (!aiOperationScene_)
                 return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
             const nlohmann::json& patch = parameters.at("patch");
             if (!patch.is_object() || patch.empty())
                 return EDITOR::OperationFailure("INVALID_PATCH", "patch must be a non-empty object.");
             DocumentSceneBase& scene = *aiOperationScene_;
             const ENVIRONMENT::SceneEnvironmentBinding before = scene.GetSceneDocument().environment;
             const nlohmann::json current = ENVIRONMENT::WriteSceneEnvironmentBinding(before);
             if (const std::string unknown = EDITOR::FindUnknownPatchKey(patch, current); !unknown.empty())
                 return EDITOR::OperationFailure("UNKNOWN_FIELD", "Unknown environment binding field: " + unknown);
             nlohmann::json merged = current;
             merged.merge_patch(patch);
             ENVIRONMENT::SceneEnvironmentBinding edited{};
             ENVIRONMENT::ReadSceneEnvironmentBinding(merged, edited);
             std::string error;
             if (!scene.GetEnvironmentAssets().LoadProfile(edited.profileId, error))
                 return EDITOR::OperationFailure("UNKNOWN_PROFILE", error);
             if (!edited.weatherId.empty() && !scene.GetEnvironmentAssets().LoadWeather(edited.weatherId, error))
                 return EDITOR::OperationFailure("UNKNOWN_WEATHER", error);
             if (!scene.GetRenderStyleAssets().Load(edited.renderStyleId, error))
                 return EDITOR::OperationFailure("UNKNOWN_RENDER_STYLE", error);
             const nlohmann::json stored = ENVIRONMENT::WriteSceneEnvironmentBinding(edited);
             // 環境の編集は場面文档の修订号を進めないので、変わったかは束縛そのものを比べる。
             const bool changed = edited != before;
             if (changed) {
                 const bool externalDirtyBefore = IsDirtyOutsideHistory(scene);
                 scene.GetSceneDocument().environment = edited;
                 CommitAppliedDocumentCommand(
                     scene, EDITOR::MakeEnvironmentHistoryCommand("AI Edit Environment", before, std::move(edited)), 0u,
                     externalDirtyBefore, EDITOR::EditorDocumentImpact::Environment);
             }
             nlohmann::json data{{"appliedPatch", EDITOR::ProjectAppliedPatch(patch, stored)},
                                 {"adjusted", EDITOR::CollectAdjustedPatchValues(patch, stored)},
                                 {"documentChanged", changed},
                                 {"dirty", context_.sceneDirty || scene.HasUnsavedSceneChanges()},
                                 {"revision", scene.GetSceneDocumentRevision()}};
             return EDITOR::OperationOk(changed ? "Environment updated." : "Environment unchanged.", std::move(data));
         }},
        EditorOperationKind::Mutation);

    operationRegistry_.Register(
        {"environment.preview",
         "Preview a time of day or weather in the editor without changing the scene document (Play and reload start "
         "again from the binding). Weather changes use the weather's transition time unless seconds is given.",
         "scene",
         {{"localHours", EditorOperationParameterType::Float, false, "Local time in hours (0-24)"},
          {"weather", EditorOperationParameterType::String, false, "Weather id from environment.describe"},
          {"seconds", EditorOperationParameterType::Float, false, "Weather transition seconds; 0 switches at once"},
          {"reset", EditorOperationParameterType::Bool, false, "Restart from the scene binding first"}},
         [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (!aiOperationScene_)
                 return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
             DocumentSceneBase& scene = *aiOperationScene_;
             if (scene.IsRuntimePlayActive())
                 return EDITOR::OperationFailure("PLAY_ACTIVE", "The game drives the environment during Play.");
             if (parameters.value("reset", false))
                 scene.ResetEnvironmentRuntime();
             ENVIRONMENT::EnvironmentRuntime& runtime = scene.GetEnvironmentRuntime();
             if (parameters.contains("localHours")) {
                 const double hours = parameters.value("localHours", 0.0);
                 if (!(hours >= 0.0 && hours < 24.0))
                     return EDITOR::OperationFailure("INVALID_TIME", "localHours must be in [0, 24).");
                 ENVIRONMENT::CalendarTime time = runtime.Simulation().GetFrameState().time;
                 time.localHours = hours;
                 runtime.Simulation().SetTime(time);
             }
             if (const std::string weather = parameters.value("weather", std::string{}); !weather.empty()) {
                 std::string error;
                 if (!runtime.RequestWeather(weather, parameters.value("seconds", -1.0f), error))
                     return EDITOR::OperationFailure("UNKNOWN_WEATHER", error);
             }
             return EDITOR::OperationOk("Environment preview updated.",
                                        {{"runtime", DescribeEnvironmentRuntime(scene)}});
         }},
        EditorOperationKind::Session);

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
            if (const auto reason = TOOLS::BAKING::LightingBakeService{}.GetBlockReason(*aiOperationScene_);
                !reason.empty())
                return EDITOR::OperationFailure("BAKE_BLOCKED", reason);
            if (parameters.contains("targetObjectId") && parameters.value("targetObjectId", uint64_t{}) == 0u)
                return EDITOR::OperationFailure("TARGET_REQUIRED",
                                                "Select a bake object or omit targetObjectId for the scene.");
            if (!aiOperationScene_->RequestLightingBake(parameters.value("force", false),
                                                        SceneObjectId{parameters.value("targetObjectId", uint64_t{})}))
                return EDITOR::OperationFailure("BAKE_REJECTED",
                                                "The scene has nothing to bake or rejected the request.");
            return EDITOR::OperationOk("Lighting bake started; query lighting.bake.status for progress.",
                                       {{"statusAction", "lighting.bake.status"}, {"pending", true}});
        }};
    bake.permission = EDITOR::EditorOperationPermission::ModifyProjectFiles;
    operationRegistry_.Register(std::move(bake), EditorOperationKind::Session, false);
    EDITOR::EditorOperationDefinition clear{
        "lighting.bake.clear",
        "Clear the current scene's generated lighting outputs when no bake, import or Play is active.",
        "scene",
        {},
        [this](const nlohmann::json&) {
            if (!aiOperationScene_)
                return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
            if (const auto reason = TOOLS::BAKING::LightingBakeService{}.GetBlockReason(*aiOperationScene_);
                !reason.empty())
                return EDITOR::OperationFailure("BAKE_BLOCKED", reason);
            if (!aiOperationScene_->ClearLightingBake())
                return EDITOR::OperationFailure("CLEAR_FAILED",
                                                "Could not clear lighting outputs; inspect the bake report.");
            return EDITOR::OperationOk("Lighting outputs cleared.");
        }};
    clear.permission = EDITOR::EditorOperationPermission::ModifyProjectFiles;
    operationRegistry_.Register(std::move(clear), EditorOperationKind::Mutation, false);

    operationRegistry_.Register({"lighting.bake.status",
                                 "Report the lighting bake state and the latest bake report.",
                                 "scene",
                                 {},
                                 [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
                                     if (!aiOperationScene_)
                                         return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
                                     const auto state = aiOperationScene_->GetLightingBakeJobState();
                                     nlohmann::json data{{"state", TOOLS::BAKING::ToString(state)},
                                                         {"running", TOOLS::BAKING::IsLightingBakeJobRunning(state)},
                                                         {"pending", TOOLS::BAKING::IsLightingBakeJobRunning(state)}};
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

    EDITOR::EditorOperationDefinition giProbes{
        "lighting.gi.probes.dump",
        "Read back the dynamic GI probes once and write them to Library/Diagnostics/<name>.json a few frames later: "
        "for every field (region, near, far) the grid origin, spacing and counts, and for every probe its world "
        "position (grid point plus relocation), active flag, update count, backface ratio, fixed-ray brightness and "
        "the irradiance E/pi and sky visibility toward +x,-x,+y,-y,+z,-z. Use it to check probe placement and values "
        "against a reference; the file appears only while dynamic GI is running.",
        "general",
        {{"name", EditorOperationParameterType::String, false,
          "File name in Library/Diagnostics without extension; default gi_probes."}},
        [](const nlohmann::json& parameters) {
            const std::string name = parameters.value("name", std::string("gi_probes"));
            if (name.empty() || name.find_first_of("/\\:*?\"<>|") != std::string::npos)
                return EDITOR::OperationFailure("INVALID_NAME", "name must be a plain file name.");
            const std::filesystem::path path = GetProjectContext().GetLibraryRoot() / "Diagnostics" / (name + ".json");
            std::error_code error;
            std::filesystem::create_directories(path.parent_path(), error);
            RENDER3D::PIPELINE::RequestDynamicDiffuseGiProbeDump(path);
            return EDITOR::OperationOk("GI probe dump requested: " + TEXT::PathToUtf8(path),
                                       {{"path", TEXT::PathToUtf8(path)}});
        }};
    giProbes.searchTerms = {"global illumination", "probes", "GI", "探针", "全局光照"};
    operationRegistry_.Register(std::move(giProbes), EditorOperationKind::Session, false);
}

} // namespace HIKARI
