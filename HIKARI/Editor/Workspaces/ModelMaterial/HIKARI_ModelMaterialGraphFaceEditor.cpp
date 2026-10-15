#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <utility>

#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::DrawFaceRampNodeControl(DocumentSceneBase& scene, MaterialAssetData& source,
                                                               MaterialGraphData& graph, float width) {
    MaterialGraphResourceData* resource = FindMaterialGraphResource(graph, "resource.diffuseRamp");
    if (resource == nullptr) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.diffuse.ramp.missing.cf5301").c_str());
        return false;
    }
    bool capturesPointer = false;
    const AssetChoice* bound = FindMaterialChoice(resource->assetGuid);
    const char* preview = bound != nullptr                ? bound->label.c_str()
                          : resource->assetGuid.IsValid() ? "Missing Ramp"
                                                          : "Choose Ramp";
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo("##FaceRamp", preview)) {
        for (const AssetChoice& choice : materialChoices_) {
            if (choice.materialKind != MaterialAssetKind::Ramp)
                continue;
            if (ImGui::Selectable(choice.label.c_str(), choice.guid == resource->assetGuid)) {
                MaterialAssetData before = source;
                resource->assetGuid = choice.guid;
                resource->enabled = true;
                authoringDocument_.CommitMaterial(std::move(before), "Bind Face Ramp");
            }
        }
        ImGui::EndCombo();
    }
    capturesPointer = ImGui::IsItemHovered() || ImGui::IsItemActive();
    bound = FindMaterialChoice(resource->assetGuid);
    if (bound != nullptr && bound->rampPreview.has_value()) {
        MATERIAL_GRAPH_UI::DrawRampStrip(*bound->rampPreview, width, 20.0f);
        capturesPointer = capturesPointer || ImGui::IsItemHovered();
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
            (void)BeginInlineRampEdit(scene, bound->guid, statusMessage_);
        }
    }
    return capturesPointer;
}

} // namespace HIKARI::EDITOR
