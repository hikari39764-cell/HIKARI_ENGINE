#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/RuntimeBinding/HIKARI_RuntimeBindingWorkspaceController.h"

#include <algorithm>
#include <string>

#include "Editor/Commands/HIKARI_EditorCommandRouter.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

void RuntimeBindingWorkspaceController::DrawDockSpace(bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImGui::GetID("HIKARI_RuntimeBindingDockSpace_v1");
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr || resetDefaultDockLayout) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID body = dockspaceId;
        ImGuiID toolbar = 0u;
        ImGuiID catalog = 0u;
        ImGuiID inspector = 0u;
        ImGui::DockBuilderSplitNode(body, ImGuiDir_Up, 0.07f, &toolbar, &body);
        ImGui::DockBuilderSplitNode(body, ImGuiDir_Left, 0.24f, &catalog, &body);
        ImGui::DockBuilderSplitNode(body, ImGuiDir_Right, 0.30f, &inspector, &body);

        ImGui::DockBuilderDockWindow("Runtime Bindings###RuntimeBinding/Toolbar", toolbar);
        ImGui::DockBuilderDockWindow("Catalog###RuntimeBinding/Catalog", catalog);
        ImGui::DockBuilderDockWindow("Live & References###RuntimeBinding/LiveReferences", body);
        ImGui::DockBuilderDockWindow("Inspector###RuntimeBinding/Inspector", inspector);
        ImGui::DockBuilderFinish(dockspaceId);
    }
    HIKARI::EDITOR::DrawWorkspaceDockSpace(dockspaceId, viewport);
#else
    (void)resetDefaultDockLayout;
#endif
}

RuntimeBindingWorkspaceResult RuntimeBindingWorkspaceController::Draw(DocumentSceneBase& scene,
                                                                      EditorCommandRouter& commandRouter) {
    RuntimeBindingWorkspaceResult result{};
#if defined(HIKARI_WITH_EDITOR)
    std::string openMessage{};
    if (!EnsureDocumentOpen(scene, openMessage)) {
        statusMessage_ = openMessage;
    }
    scene.GetRuntimeBindingStore().PublishSnapshot();
    DrawToolbarWindow(scene, result, commandRouter);
    DrawCatalogWindow(scene);
    DrawLiveReferencesWindow(scene);
    DrawInspectorWindow(scene);
#else
    (void)scene;
    (void)commandRouter;
#endif
    result.statusMessage = statusMessage_;
    return result;
}

void RuntimeBindingWorkspaceController::DrawToolbarWindow(DocumentSceneBase& scene,
                                                          RuntimeBindingWorkspaceResult& result,
                                                          EditorCommandRouter& commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
    (void)commandRouter;
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!ImGui::Begin(TEXT::LocalizeWindow("menu.workspace.runtimeBinding", "RuntimeBinding/Toolbar").c_str(), nullptr,
                      flags)) {
        ImGui::End();
        return;
    }

    ImGui::BeginDisabled(!IsDocumentOpen());
    if (IconTextButton(EditorGlyph::Save, TEXT::Localize("ui.common.save").c_str(), "RuntimeBindingSave",
                       EditorButtonTone::Primary, ImVec2(0.0f, 28.0f),
                       TEXT::Localize("editor.ui.save.projectsettings.runtime.bindings.json.11464d").c_str())) {
        Save(scene, statusMessage_);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!CanUndo());
    if (IconButton(EditorGlyph::Undo, "RuntimeBindingUndo", EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("editor.ui.undo.runtime.binding.edit.f07260").c_str())) {
        Undo(scene, statusMessage_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!CanRedo());
    if (IconButton(EditorGlyph::Redo, "RuntimeBindingRedo", EditorButtonTone::Quiet, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("editor.ui.redo.runtime.binding.edit.ac6274").c_str())) {
        Redo(scene, statusMessage_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ToolbarDivider();
    ImGui::SameLine();

    if (IconTextButton(EditorGlyph::Add, TEXT::Localize("ui.terrain.proceduralBlendAdd").c_str(), "RuntimeBindingAdd",
                       EditorButtonTone::Neutral, ImVec2(0.0f, 28.0f),
                       TEXT::Localize("editor.ui.create.a.variable.or.event.29a9e7").c_str())) {
        ImGui::OpenPopup("AddRuntimeBindingItem");
    }
    if (ImGui::BeginPopup("AddRuntimeBindingItem")) {
        if (ImGui::BeginMenu(TEXT::LocalizeWindow("editor.ui.variable.19de69", "Variable").c_str())) {
            for (const BINDING::ValueType type : {
                     BINDING::ValueType::Bool,
                     BINDING::ValueType::Int,
                     BINDING::ValueType::Float,
                     BINDING::ValueType::Float2,
                     BINDING::ValueType::Float3,
                     BINDING::ValueType::Float4,
                     BINDING::ValueType::Color,
                 }) {
                if (ImGui::MenuItem(std::string(BINDING::ToString(type)).c_str())) {
                    std::string id{};
                    CreateVariable(scene, type, {}, {}, id, statusMessage_);
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem(TEXT::LocalizeWindow("editor.ui.event.no.payload.1d71d8", "Event (no payload)").c_str())) {
            std::string id{};
            AddEventDefinition(scene, false, BINDING::ValueType::Float, {}, {}, id, statusMessage_);
        }
        if (ImGui::BeginMenu(
                TEXT::LocalizeWindow("editor.ui.event.with.payload.7a9778", "Event with payload").c_str())) {
            for (const BINDING::ValueType type : {
                     BINDING::ValueType::Bool,
                     BINDING::ValueType::Int,
                     BINDING::ValueType::Float,
                     BINDING::ValueType::Float2,
                     BINDING::ValueType::Float3,
                     BINDING::ValueType::Float4,
                     BINDING::ValueType::Color,
                 }) {
                const std::string label = std::string(BINDING::ToString(type)) + " payload";
                if (ImGui::MenuItem(label.c_str())) {
                    std::string id{};
                    AddEventDefinition(scene, true, type, {}, {}, id, statusMessage_);
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(selectedItemId_.empty());
    if (IconButton(EditorGlyph::Delete, "RuntimeBindingDelete", EditorButtonTone::Danger, ImVec2(28.0f, 28.0f),
                   TEXT::Localize("editor.ui.delete.selected.runtime.binding.ff5eec").c_str())) {
        RuntimeBindingRemovalResult removal = RemoveItem(scene, selectedItemId_, false);
        statusMessage_ = removal.message;
        if (removal.confirmationRequired) {
            pendingDeleteItemId_ = selectedItemId_;
            pendingDeleteReferences_ = std::move(removal.references);
            requestDeleteDialog_ = true;
        }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    NextToolbarItem(100);
    const std::vector<BINDING::CatalogDiagnostic> diagnostics = GetDiagnostics();
    if (BINDING::HasCatalogErrors(diagnostics)) {
        StatusBadge(TEXT::Localize("editor.ui.catalog.errors.41e99b").c_str(), EditorStatusTone::Error);
    } else if (IsDocumentDirty()) {
        StatusBadge(TEXT::Localize("ui.vfx.text.Unsaved").c_str(), EditorStatusTone::Warning);
    } else {
        StatusBadge(TEXT::Localize("ui.projectFeatures.saved").c_str(), EditorStatusTone::Ready);
    }

    const float exitWidth = 112.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - exitWidth - 12.0f));
    if (IconTextButton(EditorGlyph::Exit, TEXT::Localize("menu.scene").c_str(), "RuntimeBindingExit",
                       EditorButtonTone::Quiet, ImVec2(exitWidth, 28.0f),
                       TEXT::Localize("editor.ui.return.to.scene.workspace.364db6").c_str())) {
        result.exitToSceneRequested = true;
    }

    if (requestDeleteDialog_) {
        ImGui::OpenPopup(
            TEXT::LocalizeWindow("editor.ui.delete.referenced.binding.03da68", "Delete referenced binding?").c_str());
        requestDeleteDialog_ = false;
    }
    if (ImGui::BeginPopupModal(
            TEXT::LocalizeWindow("editor.ui.delete.referenced.binding.03da68", "Delete referenced binding?").c_str(),
            nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (pendingDeleteReferences_.empty()) {
            if (!IsReferenceIndexAvailable()) {
                ImGui::TextUnformatted(TEXT::Localize("editor.ui.reference.indexing.is.not.connected.f83d19").c_str());
                ImGui::TextDisabled(
                    TEXT::Localize("editor.ui.deletion.may.leave.unresolved.component.or.asset.slots.67898d").c_str());
            } else {
                ImGui::TextUnformatted(
                    TEXT::Localize("editor.ui.no.reference.was.found.in.the.loaded.authoring.scope.f82acc").c_str());
                ImGui::TextDisabled(
                    TEXT::Localize("editor.ui.unloaded.project.scenes.may.still.reference.this.stable.id.62db4e")
                        .c_str());
            }
        } else {
            ImGui::TextUnformatted(
                TEXT::Localize("editor.ui.this.binding.is.still.used.by.editor.authored.slots.29a469").c_str());
            ImGui::TextDisabled(TEXT::Localize("editor.ui.zu.reference.s.will.become.unresolved.f42d53").c_str(),
                                pendingDeleteReferences_.size());
        }
        ImGui::Spacing();
        const size_t previewCount = std::min<size_t>(pendingDeleteReferences_.size(), 6u);
        for (size_t index = 0u; index < previewCount; ++index) {
            const RuntimeBindingReference& reference = pendingDeleteReferences_[index];
            ImGui::BulletText(
                "%s / %s", reference.targetLabel.empty() ? reference.targetId.c_str() : reference.targetLabel.c_str(),
                reference.slotLabel.empty() ? reference.slotId.c_str() : reference.slotLabel.c_str());
        }
        if (pendingDeleteReferences_.size() > previewCount) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.and.zu.more.540dc2").c_str(),
                                pendingDeleteReferences_.size() - previewCount);
        }
        ImGui::Spacing();
        if (ActionButton(TEXT::Localize("ui.common.cancel").c_str(), "RuntimeBindingDeleteCancel",
                         EditorButtonTone::Neutral)) {
            pendingDeleteItemId_.clear();
            pendingDeleteReferences_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ActionButton(TEXT::Localize("editor.ui.delete.anyway.3095ec").c_str(), "RuntimeBindingDeleteConfirm",
                         EditorButtonTone::Danger)) {
            const RuntimeBindingRemovalResult removal = RemoveItem(scene, pendingDeleteItemId_, true);
            statusMessage_ = removal.message;
            pendingDeleteItemId_.clear();
            pendingDeleteReferences_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::End();
#else
    (void)scene;
    (void)result;
    (void)commandRouter;
#endif
}

} // namespace HIKARI::EDITOR
