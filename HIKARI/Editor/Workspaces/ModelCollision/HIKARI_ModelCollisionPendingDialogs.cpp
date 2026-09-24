#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionWorkspaceController.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelCollisionWorkspaceController::DrawPendingModelOpenModal(DocumentSceneBase& scene,
                                                                  ModelCollisionWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
    (void)result;
    AssetDatabase& assetDatabase = scene.GetAssetDatabase();
    if (!pendingModelGuid_.IsValid()) {
        return;
    }
    ImGui::OpenPopup(
        TEXT::LocalizeWindow("editor.ui.unsaved.model.collision.changes.72d305", "Unsaved Model Collision Changes")
            .c_str());
    if (!ImGui::BeginPopupModal(
            TEXT::LocalizeWindow("editor.ui.unsaved.model.collision.changes.72d305", "Unsaved Model Collision Changes")
                .c_str(),
            nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped(
        TEXT::Localize("editor.ui.the.current.model.collision.setup.has.unsaved.changes.9d8020").c_str());
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.save.and.open.16513d", "Save and Open").c_str(),
                      ImVec2(130.0f, 0.0f))) {
        std::string message{};
        if (SaveDocument(scene, message)) {
            const AssetGuid next = pendingModelGuid_;
            pendingModelGuid_ = {};
            (void)OpenModel(assetDatabase, next, statusMessage_);
            ImGui::CloseCurrentPopup();
        } else {
            statusMessage_ = std::move(message);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.discard.and.open.e49805", "Discard and Open").c_str(),
                      ImVec2(140.0f, 0.0f))) {
        const AssetGuid next = pendingModelGuid_;
        pendingModelGuid_ = {};
        (void)OpenModel(assetDatabase, next, statusMessage_);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str(), ImVec2(90.0f, 0.0f))) {
        pendingModelGuid_ = {};
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
#else
    (void)scene;
    (void)result;
#endif
}

void ModelCollisionWorkspaceController::DrawPendingCloseModal(DocumentSceneBase& scene,
                                                              ModelCollisionWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
    if (!closeRequested_) {
        return;
    }
    ImGui::OpenPopup(
        TEXT::LocalizeWindow("editor.ui.save.model.collision.changes.904eb2", "Save Model Collision Changes?").c_str());
    if (!ImGui::BeginPopupModal(
            TEXT::LocalizeWindow("editor.ui.save.model.collision.changes.904eb2", "Save Model Collision Changes?")
                .c_str(),
            nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped(
        TEXT::Localize("editor.ui.the.current.model.collision.setup.has.unsaved.changes.9d8020").c_str());
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.save.and.exit.5f4cf9", "Save and Exit").c_str(),
                      ImVec2(125.0f, 0.0f))) {
        if (SaveDocument(scene, statusMessage_)) {
            closeRequested_ = false;
            result.exitToSceneRequested = true;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.discard.and.exit.abe15d", "Discard and Exit").c_str(),
                      ImVec2(135.0f, 0.0f))) {
        closeRequested_ = false;
        result.exitToSceneRequested = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str(), ImVec2(90.0f, 0.0f))) {
        closeRequested_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
#else
    (void)scene;
    (void)result;
#endif
}

} // namespace HIKARI::EDITOR
