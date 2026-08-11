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

    void RuntimeBindingWorkspaceController::DrawDockSpace(
        bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
            return;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiID dockspaceId =
            ImGui::GetID("HIKARI_RuntimeBindingDockSpace_v1");
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr ||
            resetDefaultDockLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(
                dockspaceId,
                ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(
                dockspaceId, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(
                dockspaceId, viewport->WorkSize);

            ImGuiID body = dockspaceId;
            ImGuiID toolbar = 0u;
            ImGuiID catalog = 0u;
            ImGuiID inspector = 0u;
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Up, 0.07f, &toolbar, &body);
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Left, 0.24f, &catalog, &body);
            ImGui::DockBuilderSplitNode(
                body, ImGuiDir_Right, 0.30f, &inspector, &body);

            ImGui::DockBuilderDockWindow(
                "Runtime Bindings###RuntimeBinding/Toolbar",
                toolbar);
            ImGui::DockBuilderDockWindow(
                "Catalog###RuntimeBinding/Catalog",
                catalog);
            ImGui::DockBuilderDockWindow(
                "Live & References###RuntimeBinding/LiveReferences",
                body);
            ImGui::DockBuilderDockWindow(
                "Inspector###RuntimeBinding/Inspector",
                inspector);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
        (void)resetDefaultDockLayout;
#endif
    }

    RuntimeBindingWorkspaceResult
        RuntimeBindingWorkspaceController::Draw(
            DocumentSceneBase& scene,
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

    void RuntimeBindingWorkspaceController::DrawToolbarWindow(
        DocumentSceneBase& scene,
        RuntimeBindingWorkspaceResult& result,
        EditorCommandRouter& commandRouter) {
#if defined(HIKARI_WITH_EDITOR)
        (void)commandRouter;
        const ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse;
        if (!ImGui::Begin(
                "Runtime Bindings###RuntimeBinding/Toolbar",
                nullptr,
                flags)) {
            ImGui::End();
            return;
        }

        ImGui::BeginDisabled(!IsDocumentOpen());
        if (IconTextButton(
                EditorGlyph::Save,
                "Save",
                "RuntimeBindingSave",
                EditorButtonTone::Primary,
                ImVec2(0.0f, 28.0f),
                "Save ProjectSettings/runtime_bindings.json")) {
            Save(scene, statusMessage_);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!CanUndo());
        if (IconButton(
                EditorGlyph::Undo,
                "RuntimeBindingUndo",
                EditorButtonTone::Quiet,
                ImVec2(28.0f, 28.0f),
                "Undo runtime binding edit")) {
            Undo(scene, statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!CanRedo());
        if (IconButton(
                EditorGlyph::Redo,
                "RuntimeBindingRedo",
                EditorButtonTone::Quiet,
                ImVec2(28.0f, 28.0f),
                "Redo runtime binding edit")) {
            Redo(scene, statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ToolbarDivider();
        ImGui::SameLine();

        if (IconTextButton(
                EditorGlyph::Add,
                "Add",
                "RuntimeBindingAdd",
                EditorButtonTone::Neutral,
                ImVec2(0.0f, 28.0f),
                "Create a variable or event")) {
            ImGui::OpenPopup("AddRuntimeBindingItem");
        }
        if (ImGui::BeginPopup("AddRuntimeBindingItem")) {
            if (ImGui::BeginMenu("Variable")) {
                for (const BINDING::ValueType type : {
                        BINDING::ValueType::Bool,
                        BINDING::ValueType::Int,
                        BINDING::ValueType::Float,
                        BINDING::ValueType::Float2,
                        BINDING::ValueType::Float3,
                        BINDING::ValueType::Float4,
                        BINDING::ValueType::Color,
                    }) {
                    if (ImGui::MenuItem(
                            std::string(BINDING::ToString(type)).c_str())) {
                        std::string id{};
                        CreateVariable(
                            scene, type, {}, {}, id, statusMessage_);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Event (no payload)")) {
                std::string id{};
                AddEventDefinition(
                    scene,
                    false,
                    BINDING::ValueType::Float,
                    {}, {}, id, statusMessage_);
            }
            if (ImGui::BeginMenu("Event with payload")) {
                for (const BINDING::ValueType type : {
                        BINDING::ValueType::Bool,
                        BINDING::ValueType::Int,
                        BINDING::ValueType::Float,
                        BINDING::ValueType::Float2,
                        BINDING::ValueType::Float3,
                        BINDING::ValueType::Float4,
                        BINDING::ValueType::Color,
                    }) {
                    const std::string label =
                        std::string(BINDING::ToString(type)) + " payload";
                    if (ImGui::MenuItem(label.c_str())) {
                        std::string id{};
                        AddEventDefinition(
                            scene, true, type,
                            {}, {}, id, statusMessage_);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }

        ImGui::SameLine();
        ImGui::BeginDisabled(selectedItemId_.empty());
        if (IconButton(
                EditorGlyph::Delete,
                "RuntimeBindingDelete",
                EditorButtonTone::Danger,
                ImVec2(28.0f, 28.0f),
                "Delete selected runtime binding")) {
            RuntimeBindingRemovalResult removal =
                RemoveItem(scene, selectedItemId_, false);
            statusMessage_ = removal.message;
            if (removal.confirmationRequired) {
                pendingDeleteItemId_ = selectedItemId_;
                pendingDeleteReferences_ =
                    std::move(removal.references);
                requestDeleteDialog_ = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine();
        ToolbarDivider();
        ImGui::SameLine();
        SearchField(
            "RuntimeBindingSearch",
            "Search name or stable ID",
            searchBuffer_.data(),
            searchBuffer_.size(),
            260.0f);

        ImGui::SameLine();
        const std::vector<BINDING::CatalogDiagnostic> diagnostics =
            GetDiagnostics();
        if (BINDING::HasCatalogErrors(diagnostics)) {
            StatusBadge("Catalog errors", EditorStatusTone::Error);
        } else if (IsDocumentDirty()) {
            StatusBadge("Unsaved", EditorStatusTone::Warning);
        } else {
            StatusBadge("Saved", EditorStatusTone::Ready);
        }

        const float exitWidth = 112.0f;
        ImGui::SameLine(std::max(
            ImGui::GetCursorPosX(),
            ImGui::GetWindowWidth() - exitWidth - 12.0f));
        if (IconTextButton(
                EditorGlyph::Exit,
                "Scene",
                "RuntimeBindingExit",
                EditorButtonTone::Quiet,
                ImVec2(exitWidth, 28.0f),
                "Return to Scene workspace")) {
            result.exitToSceneRequested = true;
        }

        if (requestDeleteDialog_) {
            ImGui::OpenPopup("Delete referenced binding?");
            requestDeleteDialog_ = false;
        }
        if (ImGui::BeginPopupModal(
                "Delete referenced binding?",
                nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            if (pendingDeleteReferences_.empty()) {
                if (!IsReferenceIndexAvailable()) {
                    ImGui::TextUnformatted(
                        "Reference indexing is not connected.");
                    ImGui::TextDisabled(
                        "Deletion may leave unresolved component or asset slots.");
                } else {
                    ImGui::TextUnformatted(
                        "No reference was found in the loaded authoring scope.");
                    ImGui::TextDisabled(
                        "Unloaded project scenes may still reference this stable ID.");
                }
            } else {
                ImGui::TextUnformatted(
                    "This binding is still used by editor-authored slots.");
                ImGui::TextDisabled(
                    "%zu reference(s) will become unresolved.",
                    pendingDeleteReferences_.size());
            }
            ImGui::Spacing();
            const size_t previewCount = std::min<size_t>(
                pendingDeleteReferences_.size(), 6u);
            for (size_t index = 0u; index < previewCount; ++index) {
                const RuntimeBindingReference& reference =
                    pendingDeleteReferences_[index];
                ImGui::BulletText(
                    "%s / %s",
                    reference.targetLabel.empty()
                        ? reference.targetId.c_str()
                        : reference.targetLabel.c_str(),
                    reference.slotLabel.empty()
                        ? reference.slotId.c_str()
                        : reference.slotLabel.c_str());
            }
            if (pendingDeleteReferences_.size() > previewCount) {
                ImGui::TextDisabled(
                    "...and %zu more",
                    pendingDeleteReferences_.size() - previewCount);
            }
            ImGui::Spacing();
            if (ActionButton(
                    "Cancel",
                    "RuntimeBindingDeleteCancel",
                    EditorButtonTone::Neutral)) {
                pendingDeleteItemId_.clear();
                pendingDeleteReferences_.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ActionButton(
                    "Delete anyway",
                    "RuntimeBindingDeleteConfirm",
                    EditorButtonTone::Danger)) {
                const RuntimeBindingRemovalResult removal =
                    RemoveItem(scene, pendingDeleteItemId_, true);
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
