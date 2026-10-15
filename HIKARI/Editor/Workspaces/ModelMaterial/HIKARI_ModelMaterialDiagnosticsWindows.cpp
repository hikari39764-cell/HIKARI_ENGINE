#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>

#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Render3D/Features/HIKARI_RenderFeatureSceneAnalysis.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawDiagnosticsWindow(const DocumentSceneBase& scene) {
    (void)scene;
    if (!ImGui::Begin(TEXT::LocalizeWindow("shell.diagnostics", "ModelMaterial/Diagnostics").c_str())) {
        ImGui::End();
        return;
    }
    StatusBadge(IsEditingModel() ? TEXT::Localize("editor.ui.model.ready.4b7cbb").c_str()
                                 : TEXT::Localize("editor.ui.no.model.8cae61").c_str(),
                IsEditingModel() ? EditorStatusTone::Ready : EditorStatusTone::Normal);
    ImGui::SameLine();
    StatusBadge(previewOutputReady_ ? TEXT::Localize("editor.ui.preview.ready.8a0295").c_str()
                                    : TEXT::Localize("editor.ui.preview.pending.e4ceea").c_str(),
                previewOutputReady_ ? EditorStatusTone::Ready : EditorStatusTone::Warning);
    ImGui::SameLine();
    const bool dirty = authoringDocument_.IsBindingDirty() || authoringDocument_.IsMaterialDirty();
    StatusBadge(dirty ? "Unsaved edits" : TEXT::Localize("ui.projectFeatures.saved").c_str(),
                dirty ? EditorStatusTone::Warning : EditorStatusTone::Ready);
    if (authoringDocument_.IsAbPreviewActive()) {
        ImGui::SameLine();
        StatusBadge(TEXT::Localize("editor.ui.b.preview.d48f3a").c_str(), EditorStatusTone::Warning);
    }

    if (!statusMessage_.empty()) {
        ImGui::TextWrapped("%s", statusMessage_.c_str());
    }
    if (stats_.invalidSlotIdCount != 0u) {
        ImGui::BulletText("%u material slot(s) have no stable slotId", stats_.invalidSlotIdCount);
    }
    if (stats_.invalidSurfaceMaterialCount != 0u) {
        ImGui::BulletText("%u surface(s) reference an invalid material index", stats_.invalidSurfaceMaterialCount);
    }
    for (const SlotState& slot : slots_) {
        if (!slot.view.diagnostic.empty()) {
            ImGui::BulletText("Slot %u: %s", slot.view.materialIndex, slot.view.diagnostic.c_str());
        }
    }

    if (authoringDocument_.HasMaterial()) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.material.compile.fc6f27").c_str());
        const MaterialCompileResult& compile = authoringDocument_.GetCompileResult();
        StatusBadge(compile.success ? TEXT::Localize("editor.ui.compiled.14878f").c_str()
                                    : TEXT::Localize("editor.ui.fallback.b4a100").c_str(),
                    compile.success ? EditorStatusTone::Ready : EditorStatusTone::Warning);
        ImGui::SameLine();
        ImGui::TextDisabled(TEXT::Localize("editor.ui.source.r.llu.compile.r.llu.877878").c_str(),
                            static_cast<unsigned long long>(authoringDocument_.GetMaterialRevision()),
                            static_cast<unsigned long long>(authoringDocument_.GetCompileRevision()));
        for (const MaterialCompileDiagnostic& diagnostic : compile.diagnostics) {
            const ImVec4 color =
                diagnostic.severity == MaterialCompileDiagnosticSeverity::Error     ? ImVec4(1.0f, 0.42f, 0.35f, 1.0f)
                : diagnostic.severity == MaterialCompileDiagnosticSeverity::Warning ? ImVec4(1.0f, 0.72f, 0.28f, 1.0f)
                                                                                    : ImVec4(0.62f, 0.76f, 0.9f, 1.0f);
            ImGui::TextColored(color, "%s: %s", diagnostic.code.c_str(), diagnostic.message.c_str());
        }
        if (compile.success) {
            RENDER3D::MATERIAL::MaterialExpressionBudget budget{};
            std::string budgetError{};
            if (RENDER3D::MATERIAL::ResolveMaterialExpressionBudget(compile.program, budget, budgetError) &&
                budget.active) {
                ImGui::Text(
                    TEXT::Localize("editor.ui.expression.budget.u.u.instr.u.u.reg.u.u.values.u.u.params.u.u.re.c635cd")
                        .c_str(),
                    budget.instructionCount, budget.maxInstructionCount, budget.registerCount, budget.maxRegisterCount,
                    budget.valueCount, budget.maxValueCount, budget.parameterSlotCount, budget.maxParameterSlotCount,
                    budget.resourceCount, budget.maxResourceSlotCount);
            }
        }
    }

    ImGui::SeparatorText(TEXT::Localize("editor.ui.runtime.backed.preview.0d0e73").c_str());
    ImGui::Text(TEXT::Localize("editor.ui.request.s.output.ux.u.revision.llu.4ea33c").c_str(),
                previewRequestSubmitted_ ? "submitted" : "idle", previewOutputWidth_, previewOutputHeight_,
                static_cast<unsigned long long>(previewOutputRevision_));
    const ModelPreviewRenderPolicy policy = previewScene_.GetRenderPolicy();
    ImGui::Text(TEXT::Localize("editor.ui.runtime.policy.shadow.s.s.cached.forward.s.3062e3").c_str(),
                policy.castShadow ? "cast" : "off", policy.receiveShadow ? "receive" : "off",
                policy.allowStaticCachedForward ? "on" : "off");
    ImGui::Text(TEXT::Localize("editor.ui.environment.s.e9b433").c_str(),
                previewEnvironmentMode_ == PreviewEnvironmentMode::RuntimeMatch
                    ? "Runtime Asset Lighting (active scene, no spatial bake)"
                    : "Studio Inspect (workspace overrides)");
    const RENDER3D::EDITORVIEW::EditorInteractiveSchedulerStats scheduler = RENDER3D::EDITORVIEW::GetSchedulerStats();
    ImGui::Text(TEXT::Localize("editor.ui.scheduler.serial.llu.rendered.view.llu.candidates.u.95ce17").c_str(),
                static_cast<unsigned long long>(scheduler.scheduleSerial),
                static_cast<unsigned long long>(scheduler.renderedViewId), scheduler.renderableViewCount);
    if (const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* sceneSource = previewScene_.GetSceneSource()) {
        const RENDER3D::FEATURES::RenderFeatureRequestSet requests =
            RENDER3D::FEATURES::AnalyzeRenderFeatureRequests(*sceneSource);
        ImGui::SeparatorText(TEXT::Localize("editor.ui.render.features.a321bd").c_str());
        for (uint32_t index = 0u; index < static_cast<uint32_t>(RENDER3D::FEATURES::RenderFeatureKind::Count);
             ++index) {
            const auto kind = static_cast<RENDER3D::FEATURES::RenderFeatureKind>(index);
            if (!requests.Requests(kind)) {
                continue;
            }
            ImGui::BulletText("%s: %u surface(s)", RENDER3D::FEATURES::ToString(kind), requests.surfaceCounts[index]);
        }
        ImGui::TextDisabled(TEXT::Localize("editor.ui.post.group.mask.0x.08x.isolated.surfaces.u.83ca34").c_str(),
                            requests.postGroupMask, requests.isolatedSurfaceCount);
    }
    ImGui::TextWrapped(
        TEXT::Localize("editor.ui.the.preview.request.uses.toolpreview.purpose.the.shared.runtime..8967e0").c_str());
    ImGui::End();
}

} // namespace HIKARI::EDITOR
