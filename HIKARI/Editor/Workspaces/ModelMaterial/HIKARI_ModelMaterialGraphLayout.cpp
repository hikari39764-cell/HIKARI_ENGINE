#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include <algorithm>

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

bool IsRuntimeDrivenInput(const MaterialNodeTypeId& typeId, std::string_view pin) noexcept {
    return IsMaterialGraphRuntimeGeneratedInput(typeId, pin);
}

bool IsInlineOwnedInput(const MaterialNodeTypeId& typeId, std::string_view pin) noexcept {
    MaterialNodeKind kind{};
    if (!TryGetLegacyMaterialNodeKind(typeId, kind)) {
        return false;
    }
    switch (kind) {
    case MaterialNodeKind::PbrSurface:
        return pin == "BaseColor" || pin == "Metallic" || pin == "Roughness" || pin == "Occlusion" ||
               pin == "Emissive" || pin == "Specular" || pin == "SpecularColor";
    case MaterialNodeKind::RampDiffuse:
    case MaterialNodeKind::MultiRampDiffuse: return pin != "Mask";
    case MaterialNodeKind::SpecularBand:
    case MaterialNodeKind::FresnelRim:
    case MaterialNodeKind::Outline: return true;
    case MaterialNodeKind::MatCap: return pin == "Mask";
    case MaterialNodeKind::AnisotropicHair:
    case MaterialNodeKind::FabricSheen: return pin == "Mask" || !IsRuntimeDrivenInput(kind, pin);
    case MaterialNodeKind::EyeLighting:
        // The authored RGBA control texture remains a visible graph socket.
        // Only ViewDirection is supplied by the runtime shading context.
        return false;
    case MaterialNodeKind::CorneaHighlight: return true;
    case MaterialNodeKind::FaceSdf: return pin == "Softness" || pin == "DirectGain";
    case MaterialNodeKind::SkinLighting:
    case MaterialNodeKind::HybridLighting: return pin != "DiffuseResponse";
    case MaterialNodeKind::HairShadow: return pin != "DiffuseResponse";
    default: return false;
    }
}

NodeCardPresentation ResolveNodeCardPresentation(const MaterialGraphData& graph, const MaterialGraphNodeData& node,
                                                 float zoom, bool collapsed) {
    NodeCardPresentation result{};
    result.inputs = InputPins(node);
    result.outputs = GetMaterialGraphNodeOutputPins(node);
    result.collapsed = collapsed;
    result.detail = collapsed || zoom < 0.48f            ? NodeCardDetail::Overview
                    : zoom >= kInlineControlsMinimumZoom ? NodeCardDetail::Authoring
                                                         : NodeCardDetail::Compact;
    if (result.detail == NodeCardDetail::Overview) {
        std::erase_if(result.inputs,
                      [&](std::string_view pin) { return !PinHasConnection(graph, node.id, pin, false); });
        std::erase_if(result.outputs,
                      [&](std::string_view pin) { return !PinHasConnection(graph, node.id, pin, true); });
    } else if (result.detail == NodeCardDetail::Compact) {
        std::erase_if(result.inputs, [&](std::string_view pin) {
            return IsInlineOwnedInput(node.kind, pin) && !PinHasConnection(graph, node.id, pin, false);
        });
        result.compactSummaryHeight = 32.0f;
    } else {
        result.inlineHeight = NodeInlineHeight(node.kind);
    }
    return result;
}

size_t CountNodeInputRows(const MaterialNodeTypeId& typeId, const std::vector<std::string_view>& inputs,
                          bool inlineControls) noexcept {
    return inlineControls ? static_cast<size_t>(std::count_if(
                                inputs.begin(), inputs.end(),
                                [&typeId](std::string_view pin) { return !IsInlineOwnedInput(typeId, pin); }))
                          : inputs.size();
}

std::vector<float> BuildNodeInputVerticalOffsets(const MaterialNodeTypeId& typeId,
                                                 const std::vector<std::string_view>& inputs, float contentHeight,
                                                 bool inlineControls, float zoom) {
    std::vector<float> offsets{};
    offsets.reserve(inputs.size());
    const size_t inlineCount = inputs.size() - CountNodeInputRows(typeId, inputs, inlineControls);
    size_t inlineIndex = 0u;
    size_t rowIndex = 0u;
    for (const std::string_view pin : inputs) {
        const bool inlineOwned = inlineControls && IsInlineOwnedInput(typeId, pin);
        offsets.push_back(
            inlineOwned
                ? kHeaderHeight * zoom + 8.0f +
                      (static_cast<float>(inlineIndex++) + 0.5f) *
                          ((std::max)(contentHeight - 12.0f, 1.0f) /
                           static_cast<float>((std::max)(inlineCount, size_t{1u})))
                : (kHeaderHeight + kNodePadding + (static_cast<float>(rowIndex++) + 0.5f) * kPinRowHeight) * zoom +
                      contentHeight);
    }
    return offsets;
}

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
