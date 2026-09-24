#include "Editor/Workspaces/Ui/HIKARI_UiWorkspaceController.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
bool Has(const SceneObjectData& o, std::string_view type) {
    return std::any_of(o.components.begin(), o.components.end(), [&](const auto& c) { return c.type == type; });
}
} // namespace
std::string UiWorkspaceController::Window(const char* title, const char* panel) const {
    return TEXT::LocalizeWindow(title, std::string(Prefix()) + "/" + panel);
}
void UiWorkspaceController::DrawDockSpace(bool reset) {
    const auto* viewport = ImGui::GetMainViewport();
    const auto id = ImGui::GetID("HIKARI_UiWorkspace_v2");
    if (reset || !ImGui::DockBuilderGetNode(id)) {
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(id, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(id, viewport->WorkSize);
        ImGuiID center = id, left = 0, right = 0, bottom = 0;
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .2f, &left, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .32f, &right, &center);
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .38f, &bottom, &center);
        ImGui::DockBuilderDockWindow("###UiWorkspace/Animation", bottom);
        ImGui::DockBuilderDockWindow((std::string("###") + Prefix() + "/Library").c_str(), left);
        ImGui::DockBuilderDockWindow((std::string("###") + Prefix() + "/Canvas").c_str(), center);
        ImGui::DockBuilderDockWindow((std::string("###") + Prefix() + "/Inspector").c_str(), right);
        ImGui::DockBuilderFinish(id);
    }
    DrawWorkspaceDockSpace(id, viewport);
}
UiWorkspaceResult UiWorkspaceController::Draw(DocumentSceneBase& scene, EditorContext& context,
                                              SelectionSyncService& selection, SceneObjectCommandService& objects,
                                              EditorCommandRouter& commands, EditorOperationRegistry& operations) {
    UiWorkspaceResult result;
    commands.Bind(EditorCommandId::FocusSelection, L("uix.fit"), true, [this] {
        zoom_ = 1;
        pan_ = {};
    });
    SERVICES::SetEditorGameViewportSize(0, 0, false);
    DrawLibrary(scene, context, objects, selection, commands, operations);
    animation_.Draw(scene, context, operations);
    animation_.Tick(scene, ImGui::GetIO().DeltaTime);
    projector_.SetPreviewAnimations(&animation_.Player(), animation_.PreviewRoot());
    DrawCanvas(scene, context, commands, operations, result);
    DrawInspector(scene, context, selection, objects, operations, result);
    return result;
}
void UiWorkspaceController::EditProperties(EditorOperationRegistry& operations, SceneObjectId object,
                                           const SceneComponentData& component, const nlohmann::json& values) {
    auto edits = nlohmann::json::array();
    for (const auto& [id, value] : values.items())
        edits.push_back(
            {{"objectId", object.value}, {"componentId", component.id.value}, {"propertyId", id}, {"value", value}});
    const auto result = operations.Execute("authoring.transaction", {{"commands", edits}});
    status_ = result.ok ? std::string{} : result.message;
}
void UiWorkspaceController::FinishDrag(DocumentSceneBase& scene, EditorOperationRegistry* operations) {
    if (!drag_)
        return;
    auto edit = std::move(*drag_);
    drag_.reset();
    const bool sameRevision = edit.revision == scene.GetSceneDocumentRevision();
    if (auto* object = scene.GetWorld().FindObject(edit.object)) {
        if (auto* rect = object->GetComponent<UiRectComponent>())
            for (const auto& authored : scene.GetSceneDocument().objects)
                if (authored.id == edit.object)
                    for (const auto& c : authored.components)
                        if (c.id == edit.before.id)
                            rect->Deserialize(c.properties);
    }
    if (sameRevision && operations && edit.changed && edit.after != edit.before.properties)
        EditProperties(
            *operations, edit.object, edit.before,
            {{"offsetMinimum", edit.after.at("offsetMinimum")}, {"offsetMaximum", edit.after.at("offsetMaximum")}});
}
void UiWorkspaceController::Deactivate(DocumentSceneBase& scene) {
    animation_.StopPreview();
    target_.Reset();
    content_.Reset();
    hierarchyScene_ = nullptr;
    FinishDrag(scene, nullptr);
}
void UiWorkspaceController::DrawLibrary(DocumentSceneBase& scene, EditorContext& context,
                                        SceneObjectCommandService& objects, SelectionSyncService& selection,
                                        EditorCommandRouter& commands, EditorOperationRegistry& operations) {
    if (ImGui::Begin(Window("uix.library", "Library").c_str())) {
        const bool editing = !scene.IsRuntimePlayActive();
        ImGui::BeginDisabled(!editing);
        if (IconTextButton(EditorGlyph::Add, L("uix.add"), "AddElement", EditorButtonTone::Primary))
            ImGui::OpenPopup("UiCreate");
        if (ImGui::BeginPopup("UiCreate")) {
            std::string chosen;
            for (const auto& item : UiTemplates()) {
                if (item.id == "world_text")
                    continue;
                if (ImGui::Selectable(TEXT::Localize(item.labelKey).c_str()))
                    chosen = item.id;
            }
            std::string prefab;
            if (ImGui::BeginMenu(L("uix.savedPrefabs"))) {
                for (const auto& id : objects.ListPrefabIds())
                    if (ImGui::MenuItem(id.c_str()))
                        prefab = id;
                ImGui::EndMenu();
            }
            if (!prefab.empty())
                objects.Execute(SceneObjectCommandId::InstantiatePrefab, scene, context, selection, prefab);
            if (!chosen.empty()) {
                SceneObjectId parent{};
                if (auto* selected = context.selection.selectedObject;
                    selected &&
                    (selected->GetComponent<UiCanvasComponent>() || selected->GetComponent<UiGroupComponent>() ||
                     selected->GetComponent<UiLayoutGroupComponent>()))
                    parent = selected->GetDocumentId();
                else if (selected && selected->GetParent())
                    parent = selected->GetParent()->GetDocumentId();
                const auto r = operations.Execute("ui.create", {{"template", chosen}, {"parentId", parent.value}});
                status_ = r.ok ? std::string{} : r.message;
            }
            ImGui::EndPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!editing || !objects.CanExecute(SceneObjectCommandId::SaveAsPrefab, scene, context));
        if (IconButton(EditorGlyph::SaveAs, "Prefab", EditorButtonTone::Quiet, {28, 28}, L("uix.reuse")) && editing)
            objects.Execute(SceneObjectCommandId::SaveAsPrefab, scene, context, selection);
        ImGui::EndDisabled();
        if (!objects.GetStatusMessage().empty())
            ImGui::TextWrapped("%s", objects.GetStatusMessage().c_str());
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##Search", L("uix.search"), search_, sizeof(search_));
        ImGui::Separator();
        const auto needle = TEXT::ToLowerAsciiCopy(search_);
        bool any = false;
        const auto& sceneObjects = scene.GetSceneDocument().objects;
        if (hierarchyScene_ != &scene || hierarchyRevision_ != scene.GetSceneDocumentRevision()) {
            hierarchyScene_ = &scene;
            hierarchyRevision_ = scene.GetSceneDocumentRevision();
            std::unordered_map<uint64_t, std::vector<size_t>> children;
            std::unordered_set<uint64_t> ids, visited;
            for (const auto& object : sceneObjects)
                ids.insert(object.id.value);
            for (size_t i = 0; i < sceneObjects.size(); ++i)
                children[sceneObjects[i].parent.value_or(SceneObjectId{}).value].push_back(i);
            hierarchy_.clear();
            const auto visit = [&](auto&& self, size_t index, int depth) -> void {
                const auto id = sceneObjects[index].id.value;
                if (depth >= 64 || !visited.insert(id).second)
                    return;
                if (Has(sceneObjects[index], "UiCanvasComponent") || Has(sceneObjects[index], "UiRectComponent"))
                    hierarchy_.emplace_back(index, depth);
                if (const auto found = children.find(id); found != children.end())
                    for (const auto child : found->second)
                        self(self, child, depth + 1);
            };
            for (size_t i = 0; i < sceneObjects.size(); ++i)
                if (!sceneObjects[i].parent || !ids.contains(sceneObjects[i].parent->value))
                    visit(visit, i, 0);
            for (size_t i = 0; i < sceneObjects.size(); ++i)
                visit(visit, i, 0);
        }
        for (const auto [index, depth] : hierarchy_) {
            const auto& object = sceneObjects[index];
            const bool eligible = Has(object, "UiCanvasComponent") || Has(object, "UiRectComponent");
            if (!eligible || (!needle.empty() && TEXT::ToLowerAsciiCopy(object.name).find(needle) == std::string::npos))
                continue;
            any = true;
            ImGui::PushID(std::to_string(object.id.value).c_str());
            const float indentation = std::min(depth, 12) * 12.f;
            if (indentation > 0)
                ImGui::Indent(indentation);
            if (ImGui::Selectable(object.name.c_str(), context.selection.IsObjectSelected(object.id)))
                context.selection.SelectObjectIds(scene.GetWorld(), {object.id});
            if (indentation > 0)
                ImGui::Unindent(indentation);
            if (ImGui::BeginPopupContextItem()) {
                for (auto command : {SceneObjectCommandId::Duplicate, SceneObjectCommandId::Delete}) {
                    if (ImGui::MenuItem(L(command == SceneObjectCommandId::Duplicate ? "uix.duplicate" : "uix.delete"),
                                        nullptr, false, editing)) {
                        context.selection.SelectObjectIds(scene.GetWorld(), {object.id});
                        objects.Execute(command, scene, context, selection);
                        ImGui::EndPopup();
                        ImGui::PopID();
                        ImGui::End();
                        return;
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (editing)
            commands.ProcessViewportShortcuts(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows));
        if (!any)
            ImGui::TextWrapped("%s", L("uix.uiStart"));
        if (!status_.empty())
            ImGui::TextWrapped("%s", status_.c_str());
    }
    ImGui::End();
}
} // namespace HIKARI::EDITOR
