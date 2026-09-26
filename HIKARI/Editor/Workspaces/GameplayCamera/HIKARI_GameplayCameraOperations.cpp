#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {
void RegisterGameplayCameraOperations(EditorOperationRegistry& registry, GameplayCameraWorkspaceController& controller,
                                      EditorWorkspaceHost& host, std::function<DocumentSceneBase*()> scene) {
    using P = EditorOperationParameter;
    using T = EditorOperationParameterType;
    using K = EditorOperationKind;
    EditorOperationDefinition schema{
        "gameplay_camera.schema",
        "Discover reusable gameplay camera presets and editable mode fields.",
        "gameplay_camera",
        {P{"preset", T::String, false, "third_person, first_person, top_down, side_scroller, fixed"}},
        [](const nlohmann::json& p) {
            auto motion = nlohmann::json::array();
            for (const auto& d : SEQUENCER::kCameraMotionDescriptors)
                motion.push_back({{"kind", static_cast<int>(d.kind)},
                                  {"labelKey", d.key},
                                  {"defaultAmount", d.defaultAmount},
                                  {"speedDriven", d.speedDriven},
                                  {"oscillating", d.oscillating}});
            return EditorOperationResult{
                true,
                "OK",
                "Gameplay camera profile schema",
                {{"profile", CAMERA::WriteGameplayCameraProfile(
                                 CAMERA::MakeGameplayCameraPreset(p.value("preset", "third_person")))},
                 {"motionEffects", motion},
                 {"binding",
                  {{"component", "CameraFollowComponent"},
                   {"profile", "cameraProfile"},
                   {"followTarget", "targetObjectId"},
                   {"lookTarget", "cameraLookTarget"},
                   {"initialMode", "cameraMode"}}},
                 {"units", {{"angles", "degrees"}, {"time", "seconds"}, {"distance", "world units"}}}}};
        }};
    schema.kind = K::Query;
    schema.requiresDocumentSession = false;
    schema.entryPoint = true;
    schema.searchTerms = {"game camera", "follow", "orbit", "游戏摄像机", "カメラ"};
    registry.Register(std::move(schema));
    registry.Register(
        {"gameplay_camera.effect_preview",
         "Preview a reusable camera effect at a time or play it. During Play this triggers the saved effect on the selected scene camera.",
         "gameplay_camera",
         {P{"id", T::String, true, "Effect call ID"}, P{"time", T::Float, false, "Preview time in seconds"},
          P{"strength", T::Float, false, "Strength multiplier"},
          P{"play", T::Bool, false, "Play rather than pause at the chosen time"}},
         [&controller, scene](const auto& p) {
             auto* current = scene();
             if (!current)
                 return EditorOperationResult{false, "NO_SCENE", "No scene."};
             std::string message;
             const bool ok =
                 controller.PreviewEffect(*current, p.at("id").template get<std::string>(), p.value("time", 0.0f),
                                          p.value("strength", 1.0f), p.value("play", false), message);
             return EditorOperationResult{ok, ok ? "OK" : "PREVIEW_FAILED", message};
         }},
        K::Session);
    EditorOperationDefinition flowSchema{
        "camera_flow.describe",
        "Discover scene camera flows, region settings, bindings, and live playback stages. Empty sequence changes mode directly; empty afterMode preserves mode. Editing and save belong to the scene.",
        "scene",
        {},
        [&controller, scene](const auto&) {
            auto* current = scene();
            return current ? EditorOperationResult{true, "OK", "Camera flows", controller.DescribeFlows(*current)}
                           : EditorOperationResult{false, "NO_SCENE", "No scene."};
        }};
    flowSchema.kind = K::Query;
    flowSchema.entryPoint = true;
    flowSchema.requiresDocumentSession = false;
    flowSchema.searchTerms = {"camera flow", "trigger", "region", "cutscene", "摄像机流程", "カメラフロー"};
    registry.Register(std::move(flowSchema));
    registry.Register(
        {"camera_flow.control",
         "Trigger or interrupt an authored camera flow during Play. Uses the same runtime queue as the game SDK.",
         "scene",
         {P{"objectId", T::Int, true, "Camera flow scene object ID"},
          P{"action", T::String, true, "trigger or interrupt"}},
         [scene](const auto& p) {
             auto* current = scene();
             if (!current || !current->IsRuntimePlayActive())
                 return EditorOperationResult{false, "PLAY_REQUIRED", "Start Play first."};
             auto* object = current->GetWorld().FindObject(SceneObjectId{p.at("objectId").template get<uint64_t>()});
             auto* flow = object ? object->GetComponent<CameraFlowComponent>() : nullptr;
             const auto action = p.at("action").template get<std::string>();
             const bool ok = flow && (action == "trigger"     ? flow->RequestTrigger()
                                      : action == "interrupt" ? flow->RequestInterrupt()
                                                              : false);
             return EditorOperationResult{ok, ok ? "OK" : "REJECTED",
                                          ok ? "Camera flow command queued."
                                             : "Flow unavailable or command not valid in its current stage."};
         }},
        K::Session, false);
    registry.Register({"gameplay_camera.document",
                       "Read the current camera profile and unsaved state.",
                       "gameplay_camera",
                       {},
                       [&controller](const auto&) {
                           return EditorOperationResult{true, "OK", "Gameplay camera document",
                                                        controller.DescribeDocumentState()};
                       }},
                      K::Query);
    registry.Register(
        {"gameplay_camera.new",
         "Create a reusable camera draft from a preset. A dirty draft is preserved unless discard is explicit.",
         "gameplay_camera",
         {P{"preset", T::String, false, "Preset ID"},
          P{"discard", T::Bool, false, "Explicitly discard current unsaved draft"}},
         [&controller](const auto& p) {
             std::string message;
             const bool ok =
                 controller.Document().New(p.value("preset", "third_person"), message, p.value("discard", false));
             return EditorOperationResult{ok, ok ? "OK" : "DIRTY_DOCUMENT", message,
                                          controller.DescribeDocumentState()};
         }},
        K::Mutation);
    registry.Register(
        {"gameplay_camera.open",
         "Open an existing .hcamera profile by asset GUID.",
         "gameplay_camera",
         {P{"assetGuid", T::String, true, "Camera profile asset GUID"},
          P{"discard", T::Bool, false, "Discard current draft"}},
         [&controller, scene](const auto& p) {
             auto* current = scene();
             if (!current)
                 return EditorOperationResult{false, "NO_SCENE", "No project."};
             std::string message;
             const bool ok = controller.Document().Open(current->GetAssetDatabase(),
                                                        AssetGuid{p.at("assetGuid").template get<std::string>()},
                                                        message, p.value("discard", false));
             return EditorOperationResult{ok, ok ? "OK" : "OPEN_FAILED", message, controller.DescribeDocumentState()};
         }},
        K::Mutation);
    const nlohmann::json editSchema = {
        {"type", "object"},
        {"required", {"action"}},
        {"properties",
         {{"action",
           {{"type", "string"},
            {"enum",
             {"rename", "set_default", "add_mode", "edit_mode", "duplicate_mode", "remove_mode", "add_effect",
              "edit_effect", "remove_effect", "replace"}}}}}}};
    registry.Register(
        {"gameplay_camera.edit",
         "Atomically edit the camera profile: rename(name), set_default(id), add_mode(mode), edit_mode(id,patch), duplicate_mode(id,newId), remove_mode(id), add_effect(effect), edit_effect(id,patch), remove_effect(id), replace(profile). Read schema for defaults. Omitted patch fields remain unchanged.",
         "gameplay_camera",
         {P{"command", T::Json, true, "Camera edit command", editSchema}},
         [&controller](const auto& p) {
             std::string message;
             const bool ok = controller.Edit(p.at("command"), message);
             return EditorOperationResult{
                 ok,
                 ok ? "OK" : "INVALID_CAMERA_EDIT",
                 message,
                 {{"revision", controller.Document().Revision()}, {"dirty", controller.Document().IsDirty()}}};
         }},
        K::Mutation);
    registry.Register(
        {"gameplay_camera.save",
         "Save the current camera profile to the project's Cameras assets; optional saveAs creates a reusable copy.",
         "gameplay_camera",
         {P{"saveAs", T::Bool, false, "Save a separate asset copy"}},
         [&controller, scene](const auto& p) {
             auto* current = scene();
             if (!current)
                 return EditorOperationResult{false, "NO_SCENE", "No project."};
             std::string message;
             const bool ok = controller.Document().Save(current->GetAssetDatabase(), message, p.value("saveAs", false));
             return EditorOperationResult{
                 ok, ok ? "OK" : "SAVE_FAILED", message, {{"assetGuid", controller.Document().Guid().value}}};
         }},
        K::Mutation);
    registry.Register({"gameplay_camera.undo",
                       "Undo one camera edit.",
                       "gameplay_camera",
                       {},
                       [&controller](const auto&) {
                           const bool ok = controller.Document().Undo();
                           return EditorOperationResult{ok, ok ? "OK" : "NO_UNDO", "",
                                                        controller.DescribeDocumentState()};
                       }},
                      K::Mutation);
    registry.Register({"gameplay_camera.redo",
                       "Redo one camera edit.",
                       "gameplay_camera",
                       {},
                       [&controller](const auto&) {
                           const bool ok = controller.Document().Redo();
                           return EditorOperationResult{ok, ok ? "OK" : "NO_REDO", "",
                                                        controller.DescribeDocumentState()};
                       }},
                      K::Mutation);
    registry.Register({"gameplay_camera.reveal",
                       "Show the camera workspace and current profile.",
                       "gameplay_camera",
                       {},
                       [&controller, &host](const auto&) {
                           EditorWorkspaceOpenRequest request;
                           request.workspaceId = EditorWorkspaceId::GameplayCamera;
                           if (controller.Document().Guid().IsValid())
                               request.cameraProfileAssetGuid = controller.Document().Guid();
                           host.RequestOpen(request);
                           return EditorOperationResult{true, "OK", "Camera workspace requested."};
                       }},
                      K::Session);
}
} // namespace HIKARI::EDITOR
