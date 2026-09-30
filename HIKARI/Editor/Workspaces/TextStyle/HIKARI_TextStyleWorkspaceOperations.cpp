#include "Editor/Workspaces/TextStyle/HIKARI_TextStyleWorkspaceController.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Core/Text/HIKARI_Localization.h"
namespace HIKARI::EDITOR {
void RegisterTextStyleOperations(EditorOperationRegistry& registry, TextStyleWorkspaceController& controller,
                                 EditorWorkspaceHost& host, std::function<DocumentSceneBase*()> scene) {
    using P = EditorOperationParameterType;
    registry.Register({"text.style.describe",
                       "Describe the reusable text style draft, dirty state, undo state and "
                       "asset fields.",
                       "text",
                       {},
                       [&controller](const nlohmann::json&) {
                           const auto& d = controller.Document();
                           EditorOperationResult r{true, "OK", "Text style document"};
                           r.data = {{"open", d.IsOpen()},          {"dirty", d.IsDirty()},
                                     {"assetGuid", d.Guid().value}, {"asset", WriteTextStyleAsset(d.Profile())},
                                     {"canUndo", d.CanUndo()},      {"canRedo", d.CanRedo()}};
                           return r;
                       }},
                      EditorOperationKind::Query, false);
    registry.Register({"text.style.new",
                       "Create a reusable text style draft; save it as an asset and bind "
                       "textStyleAssetGuid on UI or WorldText components.",
                       "text",
                       {{"name", P::String, false, "Style name."},
                        {"discard", P::Bool, false, "Explicitly discard the current unsaved draft."}},
                       [&controller, scene](const nlohmann::json& p) {
                           auto* owner = scene();
                           if (!owner)
                               return EditorOperationResult{false, "NO_PROJECT", "Open a project first."};
                           std::string error;
                           auto& d = controller.Document();
                           bool ok = d.New(p.value("name", TEXT::Localize("textstyle.new")), error,
                                           p.value("discard", false));
                           if (ok) {
                               auto value = d.Profile();
                               value.font.value = FindUiFont(owner->GetAssetDatabase());
                               ok = d.Apply(value, error);
                           }
                           return EditorOperationResult{ok, ok ? "OK" : "DRAFT_CONFLICT", error};
                       }},
                      EditorOperationKind::Mutation, false);
    registry.Register({"text.style.open",
                       "Open a TextStyle asset for editing. Existing unsaved work is "
                       "protected.",
                       "text",
                       {{"assetGuid", P::String, true, "TextStyle asset GUID."},
                        {"discard", P::Bool, false, "Explicitly discard current draft."}},
                       [&controller, scene](const nlohmann::json& p) {
                           auto* owner = scene();
                           if (!owner)
                               return EditorOperationResult{false, "NO_PROJECT", "Open a project first."};
                           std::string error;
                           const bool ok = controller.Document().Open(owner->GetAssetDatabase(),
                                                                      {p.at("assetGuid").get<std::string>()}, error,
                                                                      p.value("discard", false));
                           return EditorOperationResult{ok, ok ? "OK" : "OPEN_FAILED", error};
                       }},
                      EditorOperationKind::Session, false);
    registry.Register({"text.style.edit",
                       "Replace the text style draft with validated asset data from "
                       "text.style.describe. Supports continuous-edit merging.",
                       "text",
                       {{"asset", P::Json, true,
                         "TextStyle asset object: version, name, fontAssetGuid and "
                         "appearance."},
                        {"mergeGroup", P::Int, false, "Optional continuous-edit group."}},
                       [&controller](const nlohmann::json& p) {
                           std::string error;
                           TextStyleAsset value;
                           const bool ok = ReadTextStyleAsset(p.at("asset"), value, error) &&
                                           controller.Document().Apply(value, error, p.value("mergeGroup", uint64_t{}));
                           return EditorOperationResult{ok, ok ? "OK" : "INVALID_STYLE", error};
                       }},
                      EditorOperationKind::Mutation, false);
    registry.Register({"text.style.save",
                       "Save the current text style asset and refresh its references.",
                       "text",
                       {{"saveAs", P::Bool, false, "Create a separate asset copy."}},
                       [&controller, scene](const nlohmann::json& p) {
                           auto* owner = scene();
                           if (!owner)
                               return EditorOperationResult{false, "NO_PROJECT", "Open a project first."};
                           std::string error;
                           const bool ok =
                               controller.Document().Save(owner->GetAssetDatabase(), error, p.value("saveAs", false));
                           EditorOperationResult r{ok, ok ? "OK" : "SAVE_FAILED", error};
                           r.data["assetGuid"] = controller.Document().Guid().value;
                           return r;
                       },
                       EditorOperationExecutionPolicy::MainThread,
                       EditorOperationKind::Mutation,
                       EditorOperationExposureMask::Default,
                       EditorOperationPermission::ModifyProjectFiles},
                      EditorOperationKind::Mutation, false);
    for (const bool redo : {false, true})
        registry.Register({redo ? "text.style.redo" : "text.style.undo",
                           redo ? "Redo a text style edit." : "Undo a text style edit.",
                           "text",
                           {},
                           [&controller, redo](const nlohmann::json&) {
                               const bool ok = redo ? controller.Document().Redo() : controller.Document().Undo();
                               return EditorOperationResult{ok, ok ? "OK" : "NO_HISTORY", {}};
                           }},
                          EditorOperationKind::Mutation, false);
    registry.Register({"text.reveal",
                       "Reveal the reusable text style workspace with optional scene UI text preview.",
                       "text",
                       {{"objectId", P::Int, false, "UI text object to preview in its canvas."}},
                       [&host, &controller](const nlohmann::json& p) {
                           controller.SetPreviewObject({p.value("objectId", uint64_t{})});
                           host.RequestOpen({EditorWorkspaceId::Text});
                           return EditorOperationResult{true, "OK", "Text style workspace requested."};
                       }},
                      EditorOperationKind::Session, false);
}
} // namespace HIKARI::EDITOR
