#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include <algorithm>

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

std::vector<std::string_view> OutputPins(MaterialNodeKind kind) {
  switch (kind) {
  case MaterialNodeKind::Constant:
  case MaterialNodeKind::Parameter:
    return {"Value"};
  case MaterialNodeKind::TextureSample:
    return {"Color", "RGB", "Alpha"};
  case MaterialNodeKind::PbrSurface:
    return {"Surface"};
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::AnisotropicHair:
  case MaterialNodeKind::FaceSdf:
    return {"Response"};
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::EyeLighting:
    return {"Color"};
  case MaterialNodeKind::SurfaceOutput:
    return {};
  default:
    return {"Result"};
  }
}

std::vector<std::string_view> InputPins(MaterialNodeKind kind) {
  switch (kind) {
  case MaterialNodeKind::Add:
  case MaterialNodeKind::Multiply:
  case MaterialNodeKind::Dot:
    return {"A", "B"};
  case MaterialNodeKind::Lerp:
    return {"A", "B", "Alpha"};
  case MaterialNodeKind::Clamp:
    return {"Value", "Min", "Max"};
  case MaterialNodeKind::Saturate:
  case MaterialNodeKind::OneMinus:
    return {"Value"};
  case MaterialNodeKind::Power:
    return {"Base", "Exponent"};
  case MaterialNodeKind::NormalUnpack:
    return {"PackedNormal"};
  case MaterialNodeKind::PbrSurface:
    return {"BaseColor", "Normal", "Metallic", "Roughness", "Occlusion",
            "Emissive", "Specular", "SpecularColor", "DiffuseResponse",
            "SpecularResponse", "Additive"};
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
    return {"NdotL", "Shadow", "Mask"};
  case MaterialNodeKind::SpecularBand:
    return {"NdotH", "Width", "Softness"};
  case MaterialNodeKind::FresnelRim:
    return {"NdotV", "Power", "Width", "Color"};
  case MaterialNodeKind::MatCap:
    return {"Normal", "ViewDirection", "Mask"};
  case MaterialNodeKind::AnisotropicHair:
    return {"Tangent", "Shift", "Width"};
  case MaterialNodeKind::FaceSdf:
    return {"LightDirection", "SDF", "Softness"};
  case MaterialNodeKind::EyeLighting:
    return {"BaseColor", "Mask", "ViewDirection"};
  case MaterialNodeKind::SurfaceOutput:
    return {"Surface"};
  case MaterialNodeKind::FunctionCall:
    return {"Input"};
  default:
    return {};
  }
}

const MaterialGraphNodeData *FindNode(const MaterialGraphData &graph,
                                      std::string_view id) noexcept {
  const auto found = std::find_if(
      graph.nodes.begin(), graph.nodes.end(),
      [id](const auto &node) { return node.id == id; });
  return found != graph.nodes.end() ? &*found : nullptr;
}

MaterialGraphNodeData *FindNode(MaterialGraphData &graph,
                                std::string_view id) noexcept {
  const auto found = std::find_if(
      graph.nodes.begin(), graph.nodes.end(),
      [id](const auto &node) { return node.id == id; });
  return found != graph.nodes.end() ? &*found : nullptr;
}

MaterialValueType DefaultNodeValueType(MaterialNodeKind kind) noexcept {
  switch (kind) {
  case MaterialNodeKind::PbrSurface:
  case MaterialNodeKind::SurfaceOutput:
    return MaterialValueType::Surface;
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
  case MaterialNodeKind::FaceSdf:
    return MaterialValueType::DiffuseResponse;
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::AnisotropicHair:
    return MaterialValueType::SpecularResponse;
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::EyeLighting:
  case MaterialNodeKind::NormalUnpack:
    return MaterialValueType::Float3;
  case MaterialNodeKind::TextureSample:
    return MaterialValueType::Float4;
  default:
    return MaterialValueType::Float1;
  }
}

std::string_view NodeCategory(MaterialNodeKind kind) noexcept {
  switch (kind) {
  case MaterialNodeKind::Constant:
  case MaterialNodeKind::Parameter:
  case MaterialNodeKind::TextureSample:
    return "Inputs";
  case MaterialNodeKind::Add:
  case MaterialNodeKind::Multiply:
  case MaterialNodeKind::Lerp:
  case MaterialNodeKind::Clamp:
  case MaterialNodeKind::Saturate:
  case MaterialNodeKind::OneMinus:
  case MaterialNodeKind::Power:
  case MaterialNodeKind::Dot:
  case MaterialNodeKind::NormalUnpack:
    return "Math & Masks";
  case MaterialNodeKind::PbrSurface:
  case MaterialNodeKind::SurfaceOutput:
    return "Surface";
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::AnisotropicHair:
  case MaterialNodeKind::FaceSdf:
  case MaterialNodeKind::EyeLighting:
    return "Lighting Responses";
  case MaterialNodeKind::FunctionCall:
    return "Functions";
  }
  return "Other";
}

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
