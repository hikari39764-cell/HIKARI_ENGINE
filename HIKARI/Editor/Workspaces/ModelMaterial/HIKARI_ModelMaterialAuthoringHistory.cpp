#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <utility>
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "imgui.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::FlushMaterialPreview(DocumentSceneBase& scene, std::string& outMessage) {
    const bool materialChanged = authoringDocument_.ConsumeMaterialPreviewChange();
    const bool nodeChanged = std::exchange(nodePreviewRefreshPending_, false);
    if (materialChanged)
        return RefreshMaterialPreviews(scene, outMessage);
    return !nodeChanged || RebuildAuthoredPreview(scene.GetAssetDatabase(), scene.GetAssetRegistry(), outMessage);
}

void ModelMaterialWorkspaceController::TrackMaterialItem(MaterialAssetData before, std::string label, bool changed) {
    const uint32_t item = ImGui::GetItemID();
    const bool active = ImGui::IsItemActive();
    if (active)
        materialActiveItem_ = item;
    authoringDocument_.UpdateMaterialItem(std::move(before), std::move(label), item, active, changed);
}

bool ModelMaterialWorkspaceController::ApplyHistoryProjection(ModelMaterialWorkspaceDocument::ChangedDomain domain,
                                                              DocumentSceneBase& scene, std::string& outMessage) {
    if (domain == ModelMaterialWorkspaceDocument::ChangedDomain::Binding)
        RebuildSlotState(scene.GetAssetDatabase(), scene.GetAssetRegistry());
    return FlushMaterialPreview(scene, outMessage);
}

void ModelMaterialWorkspaceController::AppendAiCompileDiagnostics(std::vector<std::string>& outDetails) const {
    if (!authoringDocument_.HasMaterial()) {
        return;
    }
    const MaterialCompileResult& compile = authoringDocument_.GetCompileResult();
    if (compile.success && !compile.usedFallback) {
        outDetails.push_back("compile: ok");
        return;
    }
    size_t count = 0u;
    for (const MaterialCompileDiagnostic& diagnostic : compile.diagnostics) {
        if (count >= 8u) {
            break;
        }
        std::string line = diagnostic.code + ": " + diagnostic.message;
        if (!diagnostic.subjectId.empty()) {
            line += " (" + diagnostic.subjectId + ")";
        }
        outDetails.push_back(line);
        ++count;
    }
    if (count == 0u) {
        outDetails.push_back("compile: " + std::string(compile.success ? "ok" : "failed"));
    }
}

} // namespace HIKARI::EDITOR
