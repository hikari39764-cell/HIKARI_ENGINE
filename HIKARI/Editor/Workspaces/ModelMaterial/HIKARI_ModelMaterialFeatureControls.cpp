#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawOutlineThicknessPresets(MaterialAssetData& source,
                                                                   MaterialGraphData& graph) {
#if defined(HIKARI_WITH_EDITOR)
    MaterialGraphParameterData* outlineThickness = FindMaterialGraphParameter(graph, "parameter.outlineThickness");
    if (outlineThickness == nullptr) {
        return;
    }
    ImGui::TextDisabled(TEXT::Localize("editor.ui.outline.thickness.presets.a424d6").c_str());
    const auto applyThickness = [&](float thickness, const char* label) {
        if (ImGui::Button(label)) {
            MaterialAssetData before = source;
            outlineThickness->defaultValue.numeric.x = thickness;
            authoringDocument_.CommitMaterial(std::move(before), "Apply outline thickness preset");
        }
    };
    applyThickness(1.0f, "Thin");
    ImGui::SameLine();
    applyThickness(2.0f, "Medium");
    ImGui::SameLine();
    applyThickness(4.0f, "Thick");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            TEXT::Localize("editor.ui.outline.thickness.is.measured.in.screen.pixels.and.clamped.to.0..2901c8")
                .c_str());
    }
#else
    (void)source;
    (void)graph;
#endif
}

} // namespace HIKARI::EDITOR
