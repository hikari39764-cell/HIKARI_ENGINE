#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include <algorithm>

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

bool IsRuntimeDrivenInput(MaterialNodeKind kind,
                          std::string_view pin) noexcept {
  switch (kind) {
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
    return pin == "NdotL" || pin == "Shadow";
  case MaterialNodeKind::SpecularBand: return pin == "NdotH";
  case MaterialNodeKind::FresnelRim: return pin == "NdotV";
  case MaterialNodeKind::MatCap:
    return pin == "Normal" || pin == "ViewDirection";
  case MaterialNodeKind::AnisotropicHair: return pin == "Tangent";
  case MaterialNodeKind::FaceSdf:
    return pin == "LightDirection" || pin == "SDF";
  case MaterialNodeKind::EyeLighting:
    return pin == "BaseColor" || pin == "ViewDirection";
  default: return false;
  }
}

bool IsInlineOwnedInput(MaterialNodeKind kind,
                        std::string_view pin) noexcept {
  switch (kind) {
  case MaterialNodeKind::PbrSurface: return true;
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
    return pin != "Mask";
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::Outline:
    return true;
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::AnisotropicHair:
  case MaterialNodeKind::EyeLighting:
  case MaterialNodeKind::FabricSheen:
    return pin == "Mask" || !IsRuntimeDrivenInput(kind, pin);
  case MaterialNodeKind::FaceSdf:
    return pin == "Softness" || pin == "DirectGain";
  case MaterialNodeKind::SkinLighting:
  case MaterialNodeKind::HybridLighting:
    return pin != "DiffuseResponse";
  default: return false;
  }
}

size_t CountNodeInputRows(MaterialNodeKind kind,
                          const std::vector<std::string_view>& inputs,
                          bool inlineControls) noexcept {
  return inlineControls
      ? static_cast<size_t>(std::count_if(
            inputs.begin(), inputs.end(),
            [kind](std::string_view pin) {
              return !IsInlineOwnedInput(kind, pin);
            }))
      : inputs.size();
}

std::vector<float> BuildNodeInputVerticalOffsets(
    MaterialNodeKind kind,
    const std::vector<std::string_view>& inputs,
    float inlineHeight,
    float zoom) {
  std::vector<float> offsets{};
  offsets.reserve(inputs.size());
  const bool inlineControls = inlineHeight > 0.0f;
  const size_t inlineCount = inputs.size() -
      CountNodeInputRows(kind, inputs, inlineControls);
  size_t inlineIndex = 0u;
  size_t rowIndex = 0u;
  for (const std::string_view pin : inputs) {
    const bool inlineOwned = inlineControls && IsInlineOwnedInput(kind, pin);
    offsets.push_back(inlineOwned
        ? kHeaderHeight * zoom + 8.0f +
            (static_cast<float>(inlineIndex++) + 0.5f) *
            ((std::max)(inlineHeight - 12.0f, 1.0f) /
             static_cast<float>((std::max)(inlineCount, size_t{1u})))
        : (kHeaderHeight + kNodePadding +
           (static_cast<float>(rowIndex++) + 0.5f) * kPinRowHeight) *
              zoom + inlineHeight);
  }
  return offsets;
}

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
