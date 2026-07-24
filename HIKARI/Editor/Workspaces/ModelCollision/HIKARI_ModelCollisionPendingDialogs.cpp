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

void ModelCollisionWorkspaceController::DrawPendingModelOpenModal(
    DocumentSceneBase &scene, ModelCollisionWorkspaceResult &result) {
#if defined(HIKARI_WITH_EDITOR)
  (void)result;
  AssetDatabase &assetDatabase = scene.GetAssetDatabase();
  if (!pendingModelGuid_.IsValid()) {
    return;
  }
  ImGui::OpenPopup("Unsaved Model Collision Changes");
  if (!ImGui::BeginPopupModal("Unsaved Model Collision Changes", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::TextWrapped("The current model collision setup has unsaved changes.");
  if (ImGui::Button("Save and Open", ImVec2(130.0f, 0.0f))) {
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
  if (ImGui::Button("Discard and Open", ImVec2(140.0f, 0.0f))) {
    const AssetGuid next = pendingModelGuid_;
    pendingModelGuid_ = {};
    (void)OpenModel(assetDatabase, next, statusMessage_);
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
    pendingModelGuid_ = {};
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
#else
  (void)scene;
  (void)result;
#endif
}

void ModelCollisionWorkspaceController::DrawPendingCloseModal(
    DocumentSceneBase &scene, ModelCollisionWorkspaceResult &result) {
#if defined(HIKARI_WITH_EDITOR)
  if (!closeRequested_) {
    return;
  }
  ImGui::OpenPopup("Save Model Collision Changes?");
  if (!ImGui::BeginPopupModal("Save Model Collision Changes?", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    return;
  }
  ImGui::TextWrapped("The current model collision setup has unsaved changes.");
  if (ImGui::Button("Save and Exit", ImVec2(125.0f, 0.0f))) {
    if (SaveDocument(scene, statusMessage_)) {
      closeRequested_ = false;
      result.exitToSceneRequested = true;
      ImGui::CloseCurrentPopup();
    }
  }
  ImGui::SameLine();
  if (ImGui::Button("Discard and Exit", ImVec2(135.0f, 0.0f))) {
    closeRequested_ = false;
    result.exitToSceneRequested = true;
    ImGui::CloseCurrentPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(90.0f, 0.0f))) {
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
