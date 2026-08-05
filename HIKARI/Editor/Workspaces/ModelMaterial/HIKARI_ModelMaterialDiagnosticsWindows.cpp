#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>

#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Render3D/Features/HIKARI_RenderFeatureSceneAnalysis.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawDiagnosticsWindow(
    const DocumentSceneBase &scene) {
#if defined(HIKARI_WITH_EDITOR)
  (void)scene;
  if (!ImGui::Begin("Diagnostics###ModelMaterial/Diagnostics")) {
    ImGui::End();
    return;
  }
  StatusBadge(IsEditingModel() ? "Model ready" : "No model",
              IsEditingModel() ? EditorStatusTone::Ready
                               : EditorStatusTone::Normal);
  ImGui::SameLine();
  StatusBadge(previewOutputReady_ ? "Preview ready" : "Preview pending",
              previewOutputReady_ ? EditorStatusTone::Ready
                                  : EditorStatusTone::Warning);
  ImGui::SameLine();
  const bool dirty = authoringDocument_.IsBindingDirty() ||
                     authoringDocument_.IsMaterialDirty();
  StatusBadge(dirty ? "Unsaved edits" : "Saved",
              dirty ? EditorStatusTone::Warning : EditorStatusTone::Ready);
  if (authoringDocument_.IsAbPreviewActive()) {
    ImGui::SameLine();
    StatusBadge("B preview", EditorStatusTone::Warning);
  }

  if (!statusMessage_.empty()) {
    ImGui::TextWrapped("%s", statusMessage_.c_str());
  }
  if (stats_.invalidSlotIdCount != 0u) {
    ImGui::BulletText("%u material slot(s) have no stable slotId",
                      stats_.invalidSlotIdCount);
  }
  if (stats_.invalidSurfaceMaterialCount != 0u) {
    ImGui::BulletText("%u surface(s) reference an invalid material index",
                      stats_.invalidSurfaceMaterialCount);
  }
  for (const SlotState &slot : slots_) {
    if (!slot.view.diagnostic.empty()) {
      ImGui::BulletText("Slot %u: %s", slot.view.materialIndex,
                        slot.view.diagnostic.c_str());
    }
  }

  if (authoringDocument_.HasMaterial()) {
    ImGui::SeparatorText("Material Compile");
    const MaterialCompileResult &compile =
        authoringDocument_.GetCompileResult();
    StatusBadge(compile.success ? "Compiled" : "Fallback",
                compile.success ? EditorStatusTone::Ready
                                : EditorStatusTone::Warning);
    ImGui::SameLine();
    ImGui::TextDisabled("source r%llu / compile r%llu",
                        static_cast<unsigned long long>(
                            authoringDocument_.GetMaterialRevision()),
                        static_cast<unsigned long long>(
                            authoringDocument_.GetCompileRevision()));
    for (const MaterialCompileDiagnostic &diagnostic : compile.diagnostics) {
      const ImVec4 color =
          diagnostic.severity == MaterialCompileDiagnosticSeverity::Error
              ? ImVec4(1.0f, 0.42f, 0.35f, 1.0f)
          : diagnostic.severity ==
                    MaterialCompileDiagnosticSeverity::Warning
              ? ImVec4(1.0f, 0.72f, 0.28f, 1.0f)
              : ImVec4(0.62f, 0.76f, 0.9f, 1.0f);
      ImGui::TextColored(color, "%s: %s", diagnostic.code.c_str(),
                         diagnostic.message.c_str());
    }
    if (compile.success) {
      RENDER3D::MATERIAL::MaterialExpressionBudget budget{};
      std::string budgetError{};
      if (RENDER3D::MATERIAL::ResolveMaterialExpressionBudget(
              compile.program, budget, budgetError) &&
          budget.active) {
        ImGui::Text(
            "Expression budget: %u/%u instr | %u/%u reg | %u/%u values | "
            "%u/%u params | %u/%u resources",
            budget.instructionCount, budget.maxInstructionCount,
            budget.registerCount, budget.maxRegisterCount,
            budget.valueCount, budget.maxValueCount,
            budget.parameterSlotCount, budget.maxParameterSlotCount,
            budget.resourceCount, budget.maxResourceSlotCount);
      }
    }
  }

  ImGui::SeparatorText("Runtime-backed Preview");
  ImGui::Text("Request: %s | Output: %ux%u | revision %llu",
              previewRequestSubmitted_ ? "submitted" : "idle",
              previewOutputWidth_, previewOutputHeight_,
              static_cast<unsigned long long>(previewOutputRevision_));
  const ModelPreviewRenderPolicy policy = previewScene_.GetRenderPolicy();
  ImGui::Text("Runtime policy: shadow %s/%s | cached-forward %s",
              policy.castShadow ? "cast" : "off",
              policy.receiveShadow ? "receive" : "off",
              policy.allowStaticCachedForward ? "on" : "off");
  ImGui::Text("Environment: %s",
              previewEnvironmentMode_ == PreviewEnvironmentMode::RuntimeMatch
                  ? "Runtime Asset Lighting (active scene, no spatial bake)"
                  : "Studio Inspect (workspace overrides)");
  const RENDER3D::EDITORVIEW::EditorInteractiveSchedulerStats scheduler =
      RENDER3D::EDITORVIEW::GetSchedulerStats();
  ImGui::Text("Scheduler: serial %llu | rendered view %llu | candidates %u",
              static_cast<unsigned long long>(scheduler.scheduleSerial),
              static_cast<unsigned long long>(scheduler.renderedViewId),
              scheduler.renderableViewCount);
  if (const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource *sceneSource =
          previewScene_.GetSceneSource()) {
    const RENDER3D::FEATURES::RenderFeatureRequestSet requests =
        RENDER3D::FEATURES::AnalyzeRenderFeatureRequests(*sceneSource);
    ImGui::SeparatorText("Render Features");
    for (uint32_t index = 0u;
         index < static_cast<uint32_t>(
                     RENDER3D::FEATURES::RenderFeatureKind::Count);
         ++index) {
      const auto kind =
          static_cast<RENDER3D::FEATURES::RenderFeatureKind>(index);
      if (!requests.Requests(kind)) {
        continue;
      }
      ImGui::BulletText("%s: %u surface(s)",
                        RENDER3D::FEATURES::ToString(kind),
                        requests.surfaceCounts[index]);
    }
    ImGui::TextDisabled("post group mask 0x%08X | isolated surfaces %u",
                        requests.postGroupMask,
                        requests.isolatedSurfaceCount);
  }
  ImGui::TextWrapped(
      "The preview request uses ToolPreview purpose, the shared runtime scene "
      "source, runtime material projection, shadows, cached forward, LOD and "
      "GPU-scene culling. Runtime Asset Lighting inherits the active scene "
      "global environment and lights, but intentionally excludes placement-"
      "specific Lightmaps and probes. Studio Inspect changes only per-view "
      "lighting overrides.");
  ImGui::End();
#else
  (void)scene;
#endif
}

void ModelMaterialWorkspaceController::DrawStatsWindow() const {
#if defined(HIKARI_WITH_EDITOR)
  if (!ImGui::Begin("Stats###ModelMaterial/Stats")) {
    ImGui::End();
    return;
  }
  if (ImGui::BeginTable("##ModelMaterialStats", 2,
                        ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_SizingStretchProp)) {
    const auto row = [](const char *label, uint64_t value) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(label);
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%llu", static_cast<unsigned long long>(value));
    };
    row("Nodes", stats_.nodeCount);
    row("Meshes", stats_.meshCount);
    row("Runtime surfaces", stats_.surfaceCount);
    row("Material slots", stats_.materialSlotCount);
    row("Vertices", stats_.vertexCount);
    row("Triangles", stats_.triangleCount);
    row("Preview scene revision", previewScene_.GetRevision());
    row("Cached model cameras", cameraStateStore_.Size());
    row("Material programs", materialProgramRegistry_.GetStats().programCount);
    ImGui::EndTable();
  }
  ImGui::End();
#endif
}

} // namespace HIKARI::EDITOR
