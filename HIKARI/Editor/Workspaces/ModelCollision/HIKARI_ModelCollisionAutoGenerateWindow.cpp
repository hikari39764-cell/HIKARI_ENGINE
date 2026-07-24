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

void ModelCollisionWorkspaceController::DrawAutoGenerateWindow() {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Auto Generate###ModelCollision/Generate")) {
    ImGui::End();
    return;
  }
  PollGenerationTask();
  if (generationPending_) {
    const std::optional<AssetTaskSnapshot> task =
        assetTaskService_ != nullptr
            ? assetTaskService_->FindSnapshot(generationTaskId_)
            : std::nullopt;
    ImGui::TextColored(ImVec4(0.40f, 0.82f, 0.92f, 1.0f), "%s",
                       task ? task->progress.stage.c_str()
                            : "Generating collision...");
    if (task && task->progress.determinate) {
      ImGui::ProgressBar(task->progress.normalized, ImVec2(-1.0f, 22.0f));
      ImGui::TextDisabled(
          "%llu / %llu source groups  |  %.2f s",
          static_cast<unsigned long long>(task->progress.completedUnits),
          static_cast<unsigned long long>(task->progress.totalUnits),
          task->elapsedSeconds);
    } else {
      ImGui::ProgressBar(0.0f, ImVec2(-1.0f, 22.0f), "Working...");
      if (task) {
        ImGui::TextDisabled("%.2f s elapsed", task->elapsedSeconds);
      }
    }
    ImGui::TextDisabled("The editor remains usable. CoACD cancellation takes "
                        "effect after its current native solve returns.");
    const bool cancelRequested = task && task->cancellationRequested;
    ImGui::BeginDisabled(cancelRequested);
    if (ImGui::Button(cancelRequested ? "Cancel Requested"
                                      : "Cancel Generation")) {
      if (assetTaskService_ != nullptr) {
        (void)assetTaskService_->RequestCancel(generationTaskId_);
      }
      statusMessage_ =
          "cancel requested; the current geometry batch will finish safely";
    }
    ImGui::EndDisabled();
    if (!statusMessage_.empty()) {
      ImGui::TextWrapped("%s", statusMessage_.c_str());
    }
    ImGui::End();
    return;
  }
  if (generationDraft_.has_value()) {
    const auto &result = generationDraft_->result;
    ImGui::TextColored(ImVec4(0.42f, 0.90f, 0.58f, 1.0f),
                       "Generation Draft Ready");
    ImGui::Text("%u generated | %u source groups | %u replaced",
                result.generatedCount, result.candidateCount,
                result.removedGeneratedCount);
    if (result.truncated) {
      ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.28f, 1.0f),
                         "The draft reached the configured shape budget.");
    }
    ImGui::TextWrapped("The viewport is previewing this draft. The current "
                       "document has not changed. Apply records one undoable "
                       "edit; Discard leaves it untouched.");
    if (ImGui::Button("Apply Draft", ImVec2(135.0f, 32.0f))) {
      ApplyGenerationDraft();
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard Draft", ImVec2(135.0f, 32.0f))) {
      DiscardGenerationDraft();
    }
    ImGui::End();
    return;
  }
  const char *targets[]{"Whole Model", "Selected Parts Combined",
                        "Nearby Selected Groups", "Each Selected Part"};
  int target = static_cast<int>(generationTarget_);
  if (ImGui::Combo("Target", &target, targets, 4)) {
    generationTarget_ =
        static_cast<ASSETS::COLLISION::ModelCollisionGenerationTarget>(target);
  }
  const char *methods[]{"Fitted Box",
                        "Fitted Sphere",
                        "Fitted Capsule",
                        "Single Convex Hull",
                        "Convex Decomposition (CoACD)",
                        "Static Triangle Mesh"};
  int method = static_cast<int>(generationMethod_);
  if (ImGui::Combo("Method", &method, methods, 6)) {
    generationMethod_ =
        static_cast<ASSETS::COLLISION::ModelCollisionGenerationMethod>(method);
  }
  ImGui::Checkbox("Replace previously generated shapes",
                  &replaceGeneratedShapes_);
  const bool requiresSourceSelection =
      generationTarget_ !=
      ASSETS::COLLISION::ModelCollisionGenerationTarget::WholeModel;
  if (generationTarget_ == ASSETS::COLLISION::ModelCollisionGenerationTarget::
                               SelectedNodesSpatialGroups) {
    ImGui::DragFloat("Merge Distance", &generationMergeDistance_, 0.01f, 0.0f,
                     100000.0f, "%.3f");
    generationMergeDistance_ = (std::max)(generationMergeDistance_, 0.0f);
  }
  const bool convexHull =
      generationMethod_ ==
      ASSETS::COLLISION::ModelCollisionGenerationMethod::ConvexHull;
  const bool decomposition =
      generationMethod_ ==
      ASSETS::COLLISION::ModelCollisionGenerationMethod::ConvexDecomposition;
  const bool triangleMesh =
      generationMethod_ ==
      ASSETS::COLLISION::ModelCollisionGenerationMethod::TriangleMesh;
  const bool primitiveMethod =
      generationMethod_ ==
          ASSETS::COLLISION::ModelCollisionGenerationMethod::Box ||
      generationMethod_ ==
          ASSETS::COLLISION::ModelCollisionGenerationMethod::Sphere ||
      generationMethod_ ==
          ASSETS::COLLISION::ModelCollisionGenerationMethod::Capsule;
  if (primitiveMethod && generationTarget_ ==
                             ASSETS::COLLISION::ModelCollisionGenerationTarget::
                                 SelectedNodesSpatialGroups) {
    ImGui::DragFloat("Max Merge Inflation", &generationAccuracy_, 0.01f, 0.0f,
                     4.0f, "%.2f x");
    generationAccuracy_ = (std::clamp)(generationAccuracy_, 0.0f, 4.0f);
  }
  if (convexHull || decomposition) {
    ImGui::InputInt("Max Hull Vertices", &generationHullVertexBudget_);
    generationHullVertexBudget_ =
        (std::clamp)(generationHullVertexBudget_, 16, 256);
  }
  if (decomposition) {
    ImGui::DragFloat("Concavity Threshold", &generationAccuracy_, 0.0025f,
                     0.001f, 1.0f, "%.4f");
    generationAccuracy_ = (std::clamp)(generationAccuracy_, 0.001f, 1.0f);
    ImGui::Checkbox("Preserve Separate Gaps", &generationPreserveGaps_);
  }
  if (triangleMesh) {
    ImGui::TextDisabled("Static bodies only");
    ImGui::InputInt("Triangle Budget", &generationTriangleBudget_);
    generationTriangleBudget_ =
        (std::clamp)(generationTriangleBudget_, 1, 16000000);
  } else if (decomposition ||
             generationTarget_ ==
                 ASSETS::COLLISION::ModelCollisionGenerationTarget::
                     SelectedNodesIndividually ||
             generationTarget_ ==
                 ASSETS::COLLISION::ModelCollisionGenerationTarget::
                     SelectedNodesSpatialGroups) {
    ImGui::InputInt("Shape Budget", &generationBudget_);
    generationBudget_ = (std::clamp)(generationBudget_, 1, 4096);
  }
  if (requiresSourceSelection) {
    const ImVec4 color = selectedSourceNodes_.empty()
                             ? ImVec4(1.0f, 0.62f, 0.30f, 1.0f)
                             : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImGui::TextColored(color, "%d source part(s) selected",
                       static_cast<int>(selectedSourceNodes_.size()));
  }
  ImGui::Separator();
  ImGui::BeginDisabled(
      !previewScene_.IsReady() ||
      (requiresSourceSelection && selectedSourceNodes_.empty()));
  if (ImGui::Button("Generate Collision", ImVec2(-1.0f, 34.0f))) {
    GenerateShapes();
  }
  ImGui::EndDisabled();
  if (!statusMessage_.empty()) {
    ImGui::TextWrapped("%s", statusMessage_.c_str());
  }
  ImGui::End();
#endif
}

} // namespace HIKARI::EDITOR
