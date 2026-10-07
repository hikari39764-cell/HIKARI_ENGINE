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

void ModelCollisionWorkspaceController::DrawAutoGenerateWindow() {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.auto.generate.0f86c2", "ModelCollision/Generate").c_str())) {
        ImGui::End();
        return;
    }
    PollGenerationTask();
    if (generationPending_) {
        const std::optional<AssetTaskSnapshot> task =
            assetTaskService_ != nullptr ? assetTaskService_->FindSnapshot(generationTaskId_) : std::nullopt;
        ImGui::TextColored(ImVec4(0.40f, 0.82f, 0.92f, 1.0f), "%s",
                           task ? task->progress.stage.c_str() : "Generating collision...");
        if (task && task->progress.determinate) {
            ImGui::ProgressBar(task->progress.normalized, ImVec2(-1.0f, 22.0f));
            ImGui::TextDisabled(TEXT::Localize("editor.ui.llu.llu.source.groups.2f.s.463562").c_str(),
                                static_cast<unsigned long long>(task->progress.completedUnits),
                                static_cast<unsigned long long>(task->progress.totalUnits), task->elapsedSeconds);
        } else {
            ImGui::ProgressBar(0.0f, ImVec2(-1.0f, 22.0f), "Working...");
            if (task) {
                ImGui::TextDisabled(TEXT::Localize("editor.ui.2f.s.elapsed.67ba42").c_str(), task->elapsedSeconds);
            }
        }
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.the.editor.remains.usable.coacd.cancellation.takes.effect.after..23e7a8")
                .c_str());
        const bool cancelRequested = task && task->cancellationRequested;
        ImGui::BeginDisabled(cancelRequested);
        if (ImGui::Button(cancelRequested ? TEXT::Localize("ui.resource.cancelRequested").c_str()
                                          : "Cancel Generation")) {
            if (assetTaskService_ != nullptr) {
                (void)assetTaskService_->RequestCancel(generationTaskId_);
            }
            statusMessage_ = "cancel requested; the current geometry batch will finish safely";
        }
        ImGui::EndDisabled();
        if (!statusMessage_.empty()) {
            ImGui::TextWrapped("%s", statusMessage_.c_str());
        }
        ImGui::End();
        return;
    }
    if (generationDraft_.has_value()) {
        const auto& result = generationDraft_->result;
        ImGui::TextColored(ImVec4(0.42f, 0.90f, 0.58f, 1.0f),
                           TEXT::Localize("editor.ui.generation.draft.ready.269f1c").c_str());
        ImGui::Text(TEXT::Localize("editor.ui.u.generated.u.source.groups.u.replaced.6f95aa").c_str(),
                    result.generatedCount, result.candidateCount, result.removedGeneratedCount);
        if (result.truncated) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.68f, 0.28f, 1.0f),
                TEXT::Localize("editor.ui.the.draft.reached.the.configured.shape.budget.d595a3").c_str());
        }
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.the.viewport.is.previewing.this.draft.the.current.document.has.n.a89d04")
                .c_str());
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.apply.draft.b63f82", "Apply Draft").c_str(),
                          ImVec2(135.0f, 32.0f))) {
            ApplyGenerationDraft();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.discard.draft.965902", "Discard Draft").c_str(),
                          ImVec2(135.0f, 32.0f))) {
            DiscardGenerationDraft();
        }
        ImGui::End();
        return;
    }
    const char* targets[]{TEXT::Localize("editor.ui.whole.model.217305").c_str(),
                          TEXT::Localize("editor.ui.selected.parts.combined.686da0").c_str(),
                          TEXT::Localize("editor.ui.nearby.selected.groups.4ed8e8").c_str(),
                          TEXT::Localize("editor.ui.each.selected.part.c0d7a5").c_str()};
    int target = static_cast<int>(generationTarget_);
    if (ImGui::Combo(TEXT::LocalizeWindow("editor.ui.target.61ad50", "Target").c_str(), &target, targets, 4)) {
        generationTarget_ = static_cast<ASSETS::COLLISION::ModelCollisionGenerationTarget>(target);
    }
    const char* methods[]{TEXT::Localize("editor.ui.fitted.box.25b34d").c_str(),
                          TEXT::Localize("editor.ui.fitted.sphere.f0c049").c_str(),
                          TEXT::Localize("editor.ui.fitted.capsule.01153f").c_str(),
                          TEXT::Localize("editor.ui.single.convex.hull.bb4a3d").c_str(),
                          TEXT::Localize("editor.ui.convex.decomposition.coacd.7ae694").c_str(),
                          TEXT::Localize("editor.ui.static.triangle.mesh.0df8f6").c_str()};
    int method = static_cast<int>(generationMethod_);
    if (ImGui::Combo(TEXT::LocalizeWindow("editor.ui.method.883069", "Method").c_str(), &method, methods, 6)) {
        generationMethod_ = static_cast<ASSETS::COLLISION::ModelCollisionGenerationMethod>(method);
    }
    ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.replace.previously.generated.shapes.8c79b1",
                                         "Replace previously generated shapes")
                        .c_str(),
                    &replaceGeneratedShapes_);
    const bool requiresSourceSelection =
        generationTarget_ != ASSETS::COLLISION::ModelCollisionGenerationTarget::WholeModel;
    if (generationTarget_ == ASSETS::COLLISION::ModelCollisionGenerationTarget::SelectedNodesSpatialGroups) {
        ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.merge.distance.fd314c", "Merge Distance").c_str(),
                         &generationMergeDistance_, 0.01f, 0.0f, 100000.0f, "%.3f");
        generationMergeDistance_ = (std::max)(generationMergeDistance_, 0.0f);
    }
    const bool convexHull = generationMethod_ == ASSETS::COLLISION::ModelCollisionGenerationMethod::ConvexHull;
    const bool decomposition =
        generationMethod_ == ASSETS::COLLISION::ModelCollisionGenerationMethod::ConvexDecomposition;
    const bool triangleMesh = generationMethod_ == ASSETS::COLLISION::ModelCollisionGenerationMethod::TriangleMesh;
    const bool primitiveMethod = generationMethod_ == ASSETS::COLLISION::ModelCollisionGenerationMethod::Box ||
                                 generationMethod_ == ASSETS::COLLISION::ModelCollisionGenerationMethod::Sphere ||
                                 generationMethod_ == ASSETS::COLLISION::ModelCollisionGenerationMethod::Capsule;
    if (primitiveMethod &&
        generationTarget_ == ASSETS::COLLISION::ModelCollisionGenerationTarget::SelectedNodesSpatialGroups) {
        ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.max.merge.inflation.aa2a5a", "Max Merge Inflation").c_str(),
                         &generationAccuracy_, 0.01f, 0.0f, 4.0f, "%.2f x");
        generationAccuracy_ = (std::clamp)(generationAccuracy_, 0.0f, 4.0f);
    }
    if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.workflow.qualityLimits", "generationLimits").c_str())) {
        if (convexHull || decomposition) {
            ImGui::InputInt(TEXT::LocalizeWindow("editor.ui.max.hull.vertices.7b50c2", "Max Hull Vertices").c_str(),
                            &generationHullVertexBudget_);
            generationHullVertexBudget_ = (std::clamp)(generationHullVertexBudget_, 16, 256);
        }
        if (decomposition) {
            ImGui::DragFloat(
                TEXT::LocalizeWindow("editor.ui.concavity.threshold.6420ce", "Concavity Threshold").c_str(),
                &generationAccuracy_, 0.0025f, 0.001f, 1.0f, "%.4f");
            generationAccuracy_ = (std::clamp)(generationAccuracy_, 0.001f, 1.0f);
            ImGui::Checkbox(
                TEXT::LocalizeWindow("editor.ui.preserve.separate.gaps.979a4e", "Preserve Separate Gaps").c_str(),
                &generationPreserveGaps_);
        }
        if (triangleMesh) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.static.bodies.only.4f1004").c_str());
            ImGui::InputInt(TEXT::LocalizeWindow("editor.ui.triangle.budget.418792", "Triangle Budget").c_str(),
                            &generationTriangleBudget_);
            generationTriangleBudget_ = (std::clamp)(generationTriangleBudget_, 1, 16000000);
        } else if (decomposition ||
                   generationTarget_ == ASSETS::COLLISION::ModelCollisionGenerationTarget::SelectedNodesIndividually ||
                   generationTarget_ == ASSETS::COLLISION::ModelCollisionGenerationTarget::SelectedNodesSpatialGroups) {
            ImGui::InputInt(TEXT::LocalizeWindow("editor.ui.shape.budget.531dc6", "Shape Budget").c_str(),
                            &generationBudget_);
            generationBudget_ = (std::clamp)(generationBudget_, 1, 4096);
        }
    }
    if (requiresSourceSelection) {
        const ImVec4 color =
            selectedSourceNodes_.empty() ? ImVec4(1.0f, 0.62f, 0.30f, 1.0f) : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::TextColored(color, TEXT::Localize("editor.ui.d.source.part.s.selected.3c5910").c_str(),
                           static_cast<int>(selectedSourceNodes_.size()));
    }
    ImGui::Separator();
    ImGui::BeginDisabled(!previewScene_.IsReady() || (requiresSourceSelection && selectedSourceNodes_.empty()));
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.generate.collision.36c878", "Generate Collision").c_str(),
                      ImVec2(-1.0f, 34.0f))) {
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
