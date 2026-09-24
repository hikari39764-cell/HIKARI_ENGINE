#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::DrawMaterialGraphRampNodeInlineControls(DocumentSceneBase& scene,
                                                                               MaterialAssetData& source,
                                                                               MaterialGraphData& graph,
                                                                               MaterialGraphNodeData& node,
                                                                               float width) {
#if defined(HIKARI_WITH_EDITOR)
    bool capturesPointer = false;
    const auto trackItem = [&capturesPointer]() {
        capturesPointer = capturesPointer || ImGui::IsItemHovered() || ImGui::IsItemActive();
    };
    const auto drawParameter = [&](std::string_view id, const char* label, std::string_view pin = {}) {
        capturesPointer |= DrawGraphParameter(source, graph, &node, id, label, width, pin);
    };

    ImGui::SeparatorText(node.kind == MaterialNodeKind::MultiRampDiffuse ? "Ramp Stack / Layer 1"
                                                                         : TEXT::LocalizeCStr("ui.terrain.toolRamp"));
    MaterialGraphResourceData* resource = MATERIAL_GRAPH_UI::FindRampResource(graph, node);
    if (resource == nullptr) {
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.ramp.resource.3cfd9d", "+ Ramp Resource").c_str(),
                          {width, 0.0f})) {
            MaterialAssetData before = source;
            MaterialGraphResourceData created{};
            created.id = "Ramp_" + std::to_string(authoringIdCounter_++);
            created.displayName = "Diffuse Ramp";
            created.type = MaterialResourceType::Ramp1D;
            created.semantic = MaterialResourceSemantic::Ramp;
            graph.resources.push_back(std::move(created));
            node.referenceId = graph.resources.back().id;
            authoringDocument_.CommitMaterial(std::move(before), "Create Ramp resource");
        }
        trackItem();
    } else {
        const AssetChoice* bound = FindMaterialChoice(resource->assetGuid);
        const char* preview = bound != nullptr                ? bound->label.c_str()
                              : resource->assetGuid.IsValid() ? "Missing Ramp"
                                                              : "Choose Ramp";
        ImGui::SetNextItemWidth(width);
        if (ImGui::BeginCombo("##RampAsset", preview)) {
            for (const AssetChoice& choice : materialChoices_) {
                if (choice.materialKind != MaterialAssetKind::Ramp) {
                    continue;
                }
                if (ImGui::Selectable(choice.label.c_str(), choice.guid == resource->assetGuid)) {
                    MaterialAssetData before = source;
                    node.referenceId = resource->id;
                    resource->assetGuid = choice.guid;
                    resource->enabled = true;
                    authoringDocument_.CommitMaterial(std::move(before), "Bind Ramp asset");
                }
            }
            ImGui::EndCombo();
        }
        trackItem();
        bound = FindMaterialChoice(resource->assetGuid);
        if (bound != nullptr && bound->rampPreview.has_value()) {
            MATERIAL_GRAPH_UI::DrawRampStrip(*bound->rampPreview, width, 20.0f);
            trackItem();
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                (void)BeginInlineRampEdit(scene, bound->guid, statusMessage_);
            }
        }
        if (ImGui::SmallButton(
                TEXT::LocalizeWindow("editor.ui.new.from.preset.658e68", "New from Preset...").c_str())) {
            ImGui::OpenPopup("##NewRampPreset");
        }
        trackItem();
        if (width >= 200.0f) {
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(bound == nullptr);
        if (ImGui::SmallButton(TEXT::LocalizeWindow("editor.ui.edit.ramp.6de413", "Edit Ramp").c_str())) {
            (void)BeginInlineRampEdit(scene, bound->guid, statusMessage_);
        }
        trackItem();
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("##NewRampPreset")) {
            for (const ASSETS::MATERIAL::MaterialRampPresetInfo& preset :
                 ASSETS::MATERIAL::GetBuiltInMaterialRampPresets()) {
                if (ImGui::MenuItem(preset.displayName.data())) {
                    (void)CreateRampPresetAndBind(scene, source, graph, node, preset.id, statusMessage_);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s", preset.description.data());
                }
            }
            ImGui::EndPopup();
        }
    }

    if (node.kind == MaterialNodeKind::MultiRampDiffuse) {
        constexpr std::array<std::string_view, 3u> kRampIds{"resource.secondaryRamp", "resource.tertiaryRamp",
                                                            "resource.quaternaryRamp"};
        constexpr std::array<const char*, 3u> kRampLabels{"Layer 2", "Layer 3", "Layer 4"};
        for (size_t layer = 0u; layer < kRampIds.size(); ++layer) {
            MaterialGraphResourceData* layerResource = FindMaterialGraphResource(graph, kRampIds[layer]);
            ImGui::PushID(kRampLabels[layer]);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(kRampLabels[layer]);
            ImGui::SameLine();
            if (layerResource == nullptr) {
                ImGui::TextDisabled(TEXT::Localize("editor.ui.missing.resource.e0ca40").c_str());
                ImGui::PopID();
                continue;
            }
            const AssetChoice* layerChoice = FindMaterialChoice(layerResource->assetGuid);
            const char* layerPreview = layerChoice != nullptr               ? layerChoice->label.c_str()
                                       : layerResource->assetGuid.IsValid() ? "Missing Ramp"
                                                                            : "None (disabled)";
            ImGui::SetNextItemWidth((std::max)(96.0f, width - 62.0f));
            if (ImGui::BeginCombo("##RampAsset", layerPreview)) {
                if (ImGui::Selectable(TEXT::LocalizeWindow("ui.input.none", "None").c_str(),
                                      !layerResource->assetGuid.IsValid())) {
                    MaterialAssetData before = source;
                    layerResource->assetGuid = {};
                    layerResource->enabled = false;
                    authoringDocument_.CommitMaterial(std::move(before), "Clear Ramp stack layer");
                }
                for (const AssetChoice& choice : materialChoices_) {
                    if (choice.materialKind != MaterialAssetKind::Ramp) {
                        continue;
                    }
                    if (ImGui::Selectable(choice.label.c_str(), choice.guid == layerResource->assetGuid)) {
                        MaterialAssetData before = source;
                        layerResource->assetGuid = choice.guid;
                        layerResource->enabled = true;
                        authoringDocument_.CommitMaterial(std::move(before), "Bind Ramp stack layer");
                    }
                }
                ImGui::EndCombo();
            }
            trackItem();
            layerChoice = FindMaterialChoice(layerResource->assetGuid);
            if (layerChoice != nullptr && layerChoice->rampPreview.has_value()) {
                MATERIAL_GRAPH_UI::DrawRampStrip(*layerChoice->rampPreview, width, 12.0f);
                trackItem();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
                    (void)BeginInlineRampEdit(scene, layerChoice->guid, statusMessage_);
                }
            }
            ImGui::PopID();
        }
    }

    ImGui::SeparatorText(TEXT::Localize("editor.ui.lighting.response.560bba").c_str());
    drawParameter("parameter.shadowThreshold", "Shadow", "ShadowThreshold");
    drawParameter("parameter.shadowSoftness", "Softness", "ShadowSoftness");
    drawParameter("parameter.midThreshold", "Midtone", "MidThreshold");
    drawParameter("parameter.shadowTint", "Shadow Tint", "ShadowTint");
    drawParameter("parameter.midTint", "Mid Tint", "MidTint");
    drawParameter("parameter.litTint", "Lit Tint", "LitTint");
    drawParameter("parameter.rampMaskInfluence", "Mask", "MaskInfluence");
    drawParameter("parameter.rampIntensity", "Ramp Influence", "Intensity");
    drawParameter("parameter.toonDirectGain", "Direct Gain", "DirectGain");
    if (node.kind == MaterialNodeKind::MultiRampDiffuse) {
        drawParameter("parameter.rampRow", "Ramp Row", "RampRow");
        drawParameter("parameter.rampLayerWeights", "Layer Weights", "LayerWeights");
        ImGui::TextDisabled(TEXT::Localize("editor.ui.weights.are.clamped.0.and.normalized.at.runtime.0c27c0").c_str());
    }
    return capturesPointer;
#else
    (void)scene;
    (void)source;
    (void)graph;
    (void)node;
    (void)width;
    return false;
#endif
}

} // namespace HIKARI::EDITOR
