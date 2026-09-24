#include "Editor/Workspaces/Ui/HIKARI_UiAnimationEditor.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI::EDITOR {
void RegisterUiAnimationOperations(EditorOperationRegistry& registry, UiAnimationEditor& editor,
                                   std::function<DocumentSceneBase*()> scene) {
    using P = EditorOperationParameterType;
    auto result = [](bool ok, const std::string& error) {
        return EditorOperationResult{ok, ok ? "OK" : "UI_ANIMATION_FAILED", error};
    };
    registry.Register(
        {"ui.animation.describe",
         "Describe the UI animation draft, property catalogue and named clips. Tracks address self or a UiAnimationTarget slot under the animator; no source code is needed.",
         "ui",
         {},
         [&editor](const nlohmann::json&) {
             const auto& doc = editor.Document();
             EditorOperationResult r{true, "OK", "UI animation"};
             r.data = {{"open", doc.IsOpen()},          {"dirty", doc.IsDirty()},
                       {"assetGuid", doc.Guid().value}, {"asset", WriteUiAnimationAsset(doc.Profile())},
                       {"canUndo", doc.CanUndo()},      {"canRedo", doc.CanRedo()}};
             r.data["properties"] = nlohmann::json::array();
             for (const auto& p : UiMotionProperties())
                 r.data["properties"].push_back(
                     {{"id", p.id}, {"default", p.initial}, {"minimum", p.minimum}, {"maximum", p.maximum}});
             r.data["triggers"] = nlohmann::json::array();
             for (const auto* trigger : UiMotionTriggerNames())
                 r.data["triggers"].push_back(trigger);
             r.data["binding"] = {{"componentType", "UiAnimatorComponent"},
                                  {"propertyId", "animationAssetGuid"},
                                  {"targetComponent", "UiAnimationTargetComponent"},
                                  {"targetProperty", "slot"}};
             return r;
         }},
        EditorOperationKind::Query, false);
    registry.Register(
        {"ui.animation.new",
         "Create a reusable UI animation draft from a panel/button preset or empty library. Save and bind its asset GUID on UiAnimatorComponent.",
         "ui",
         {{"preset", P::String, false, "panel, button or empty."},
          {"name", P::String, false, "Asset name."},
          {"discard", P::Bool, false, "Explicitly discard unsaved draft."}},
         [&editor, result](const nlohmann::json& p) {
             const auto preset = p.value("preset", std::string("panel"));
             if (preset != "panel" && preset != "button" && preset != "empty")
                 return result(false, "Unknown UI animation preset.");
             auto asset = preset == "empty" ? UiAnimationAsset{} : MakeUiAnimationPreset(preset);
             asset.name = p.value("name", asset.name);
             std::string error;
             const bool ok = editor.Document().New(std::move(asset), error, p.value("discard", false));
             if (ok)
                 editor.StopPreview();
             return result(ok, error);
         }},
        EditorOperationKind::Mutation, false);
    registry.Register({"ui.animation.open",
                       "Open a UiAnimation asset, preserving unsaved work unless discard is explicit.",
                       "ui",
                       {{"assetGuid", P::String, true, "UI animation GUID."},
                        {"discard", P::Bool, false, "Explicitly discard unsaved draft."}},
                       [&editor, scene, result](const nlohmann::json& p) {
                           auto* owner = scene();
                           if (!owner)
                               return result(false, "Open a project first.");
                           std::string error;
                           const bool ok =
                               editor.Document().Open(owner->GetAssetDatabase(), {p.at("assetGuid").get<std::string>()},
                                                      error, p.value("discard", false));
                           if (ok)
                               editor.StopPreview();
                           return result(ok, error);
                       }},
                      EditorOperationKind::Session, false);
    registry.Register(
        {"ui.animation.edit",
         "Apply validated clip/track/key data from ui.animation.describe as one undoable edit. Key arrays are [seconds,value,inSlope,outSlope]; channels and automatic triggers must be unique.",
         "ui",
         {{"asset", P::Json, true, "Versioned UI animation asset data."},
          {"mergeGroup", P::Int, false, "Continuous editing transaction group."}},
         [&editor, result](const nlohmann::json& p) {
             UiAnimationAsset asset;
             std::string error;
             const bool ok = ReadUiAnimationAsset(p.at("asset"), asset, error) &&
                             editor.Document().Apply(std::move(asset), error, p.value("mergeGroup", uint64_t{}));
             return result(ok, error);
         }},
        EditorOperationKind::Mutation, false);
    registry.Register(
        {"ui.animation.save",
         "Save/import the UI animation and refresh runtime references. The saved scene stores only its GUID.",
         "ui",
         {{"saveAs", P::Bool, false, "Create a new reusable asset copy."}},
         [&editor, scene, result](const nlohmann::json& p) {
             auto* owner = scene();
             if (!owner)
                 return result(false, "Open a project first.");
             std::string error;
             const bool ok = editor.Document().Save(owner->GetAssetDatabase(), error, p.value("saveAs", false));
             auto r = result(ok, error);
             r.data["assetGuid"] = editor.Document().Guid().value;
             return r;
         }},
        EditorOperationKind::Mutation, false);
    for (bool redo : {false, true})
        registry.Register(
            {redo ? "ui.animation.redo" : "ui.animation.undo",
             redo ? "Redo UI animation edit." : "Undo UI animation edit.",
             "ui",
             {},
             [&editor, result, redo](const nlohmann::json&) { return result(editor.TravelHistory(redo), {}); }},
            EditorOperationKind::Mutation, false);
    registry.Register(
        {"ui.animation.preview",
         "Preview an animation clip on a scene UI root without changing authored layout. Stop game Play first. Missing/duplicate target slots fail explicitly.",
         "ui",
         {{"objectId", P::Int, true, "UI root SceneObjectId."},
          {"clip", P::String, true, "Named clip in draft."},
          {"seconds", P::Float, false, "Seek position."},
          {"play", P::Bool, false, "Continue playing."}},
         [&editor, scene, result](const nlohmann::json& p) {
             auto* owner = scene();
             if (!owner)
                 return result(false, "Open a scene first.");
             const bool ok = editor.Preview(*owner, {p.at("objectId").get<uint64_t>()}, p.at("clip").get<std::string>(),
                                            p.value("seconds", 0.f), p.value("play", false));
             return result(ok, ok ? "" : "Preview target, clip or target slots are invalid, or game Play is active.");
         }},
        EditorOperationKind::Session, false);
    registry.Register({"ui.animation.preview.stop",
                       "Restore the authored UI pose after animation preview.",
                       "ui",
                       {},
                       [&editor](const nlohmann::json&) {
                           editor.StopPreview();
                           return EditorOperationResult{true, "OK", {}};
                       }},
                      EditorOperationKind::Session, false);
}
} // namespace HIKARI::EDITOR
