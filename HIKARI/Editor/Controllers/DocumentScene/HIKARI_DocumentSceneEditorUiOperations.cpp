#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI {
void DocumentSceneEditorController::RegisterUiOperations() {
    using namespace EDITOR;
    RegisterUiAnimationOperations(operationRegistry_, uiWorkspaceController_.Animation(),
                                  [this] { return aiOperationScene_; });
    RegisterTextStyleOperations(operationRegistry_, textWorkspaceController_, workspaceHost_,
                                [this] { return aiOperationScene_; });
    operationRegistry_.Register(
        {"ui.parent.set",
         "Move an element into a Canvas or UI container; anchors and offsets "
         "become relative to the new parent.",
         "ui",
         {{"objectId", EditorOperationParameterType::Int, true, "UI element id."},
          {"parentId", EditorOperationParameterType::Int, true, "Canvas or UI container id."}},
         [this](const nlohmann::json& p) {
             auto* scene = aiOperationScene_;
             if (!scene || scene->IsRuntimePlayActive())
                 return EditorOperationResult{false, "NOT_EDITABLE", "Open a scene and stop Play."};
             auto& document = scene->GetSceneDocument();
             const auto before = document.objects;
             const bool dirty = context_.sceneDirty || scene->HasUnsavedSceneChanges();
             std::string error;
             if (!ReparentUiElement(document, {p.at("objectId").get<uint64_t>()}, {p.at("parentId").get<uint64_t>()},
                                    error))
                 return EditorOperationResult{false, "INVALID_PARENT", error};
             CommitSceneObjectAuthoringHistory(*scene, {"Move UI element", before, document.objects, document.camera,
                                                        document.camera, dirty, true, true});
             return EditorOperationResult{true, "OK", "UI parent changed."};
         }},
        EditorOperationKind::Mutation, false);
    operationRegistry_.Register({"ui.reveal",
                                 "Open the scene UI/text editing workspace without replacing the "
                                 "scene.",
                                 "ui",
                                 {},
                                 [this](const nlohmann::json&) {
                                     workspaceHost_.RequestOpen({EditorWorkspaceId::Ui});
                                     return EditorOperationResult{true, "OK", "Workspace requested."};
                                 }},
                                EditorOperationKind::Session, false);
    operationRegistry_.Register({"ui.templates.query",
                                 "List reusable scene UI/text templates. Edit created components "
                                 "through component.describe and authoring.transaction.",
                                 "ui",
                                 {},
                                 [](const nlohmann::json&) {
                                     EditorOperationResult result{true, "OK", "Templates available."};
                                     result.data["templates"] = nlohmann::json::array();
                                     for (const auto& item : UiTemplates()) {
                                         result.data["templates"].push_back(
                                             {{"id", item.id}, {"name", TEXT::Localize(item.labelKey)}});
                                     }
                                     return result;
                                 }},
                                EditorOperationKind::Query, false);
    operationRegistry_.Register(
        {"ui.create",
         "Create UI or world text in the current scene as one undoable edit. A "
         "Canvas is supplied when needed. Save with scene.save.",
         "ui",
         {{"template", EditorOperationParameterType::String, true, "Template id from templates.query."},
          {"parentId", EditorOperationParameterType::Int, false,
           "Canvas or UI container object id; omitted selects the first "
           "Canvas."},
          {"name", EditorOperationParameterType::String, false, "Optional object name."}},
         [this](const nlohmann::json& p) {
             auto* scene = aiOperationScene_;
             if (!scene)
                 return EditorOperationResult{false, "NO_SCENE", "Open a scene first."};
             if (scene->IsRuntimePlayActive())
                 return EditorOperationResult{false, "PLAY_ACTIVE", "Stop Play before editing."};
             const auto preset = p.at("template").get<std::string>();
             const SceneObjectId parent{p.value("parentId", uint64_t{0})};
             for (const auto& object : scene->GetSceneDocument().objects)
                 if (object.id == parent && object.editorLocked)
                     return EditorOperationResult{false, "LOCKED", "Unlock the parent before adding children."};
             const auto before = scene->GetSceneDocument().objects;
             const bool dirty = context_.sceneDirty || scene->HasUnsavedSceneChanges();
             const auto created =
                 CreateUiTemplate(scene->GetSceneDocument(), scene->GetComponentRegistry(), preset, parent,
                                  FindUiFont(scene->GetAssetDatabase()), p.value("name", std::string{}));
             if (!created.ok)
                 return EditorOperationResult{false, "CREATE_FAILED", created.error};
             CommitSceneObjectAuthoringHistory(*scene, {"Create UI/Text", before, scene->GetSceneDocument().objects,
                                                        scene->GetSceneDocument().camera,
                                                        scene->GetSceneDocument().camera, dirty, true, true});
             context_.selection.SelectObjectIds(scene->GetWorld(), {created.id});
             EditorOperationResult result{true, "OK", "Created scene object."};
             result.data = {{"objectId", created.id.value}, {"componentQuery", "component.instances.query"}};
             return result;
         }},
        EditorOperationKind::Mutation, false);
}
} // namespace HIKARI
