#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include <algorithm>
#include <cctype>

#include "Assets/Material/Compiler/HIKARI_MaterialRampRasterizer.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

MaterialGraphData *EditableGraph(MaterialAssetData &source) noexcept {
  if (source.kind == MaterialAssetKind::Material) {
    return &source.graph;
  }
  if (source.kind == MaterialAssetKind::Function) {
    return &source.function.graph;
  }
  return nullptr;
}

std::vector<std::string_view> OutputPins(MaterialNodeKind kind) {
  switch (kind) {
  case MaterialNodeKind::Constant:
  case MaterialNodeKind::Parameter:
    return {"Value"};
  case MaterialNodeKind::TextureSample:
    return {"Color", "RGB", "R", "G", "B", "A", "Alpha"};
  case MaterialNodeKind::PbrSurface:
    return {"Surface"};
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::AnisotropicHair:
  case MaterialNodeKind::FaceSdf:
  case MaterialNodeKind::SkinLighting:
  case MaterialNodeKind::HybridLighting:
    return {"Response"};
  case MaterialNodeKind::Outline:
    return {"Response"};
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::EyeLighting:
  case MaterialNodeKind::FabricSheen:
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
            "SpecularResponse", "Additive", "Outline"};
  case MaterialNodeKind::RampDiffuse:
    return {"ShadowThreshold", "ShadowSoftness", "MidThreshold",
            "ShadowTint", "MidTint", "LitTint", "MaskInfluence",
            "Intensity", "DirectGain", "Mask"};
  case MaterialNodeKind::MultiRampDiffuse:
    return {"ShadowThreshold", "ShadowSoftness", "MidThreshold",
            "ShadowTint", "MidTint", "LitTint", "RampRow",
            "MaskInfluence", "RampBlend", "Intensity", "DirectGain",
            "Mask"};
  case MaterialNodeKind::SpecularBand:
    return {"Width", "Softness", "Intensity", "Power", "Tint"};
  case MaterialNodeKind::FresnelRim:
    return {"Power", "Width", "Color", "Intensity", "LightAlignment"};
  case MaterialNodeKind::MatCap:
    return {"Mask"};
  case MaterialNodeKind::AnisotropicHair:
    return {"Shift", "Width", "Mask"};
  case MaterialNodeKind::FaceSdf:
    return {"Softness", "DirectGain"};
  case MaterialNodeKind::EyeLighting:
    return {"Mask"};
  case MaterialNodeKind::SkinLighting:
    return {"DiffuseResponse", "Wrap", "Subsurface", "Tint", "Mask"};
  case MaterialNodeKind::FabricSheen:
    return {"Sheen", "FiberRoughness", "Power", "Intensity", "Mask"};
  case MaterialNodeKind::HybridLighting:
    return {"DiffuseResponse", "DirectDiffuseBlend",
            "DirectSpecularBlend", "IndirectBlend"};
  case MaterialNodeKind::Outline:
    return {"Color", "Thickness"};
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

const MaterialGraphParameterData *FindParameter(
    const MaterialGraphData &graph, std::string_view id) noexcept {
  const auto found = std::find_if(
      graph.parameters.begin(), graph.parameters.end(),
      [id](const auto &parameter) { return parameter.id == id; });
  return found != graph.parameters.end() ? &*found : nullptr;
}

MaterialGraphParameterData *FindParameter(
    MaterialGraphData &graph, std::string_view id) noexcept {
  const auto found = std::find_if(
      graph.parameters.begin(), graph.parameters.end(),
      [id](const auto &parameter) { return parameter.id == id; });
  return found != graph.parameters.end() ? &*found : nullptr;
}

const MaterialGraphResourceData *FindGraphResource(
    const MaterialGraphData &graph, std::string_view id) noexcept {
  const auto found = std::find_if(
      graph.resources.begin(), graph.resources.end(),
      [id](const auto &resource) { return resource.id == id; });
  return found != graph.resources.end() ? &*found : nullptr;
}

MaterialGraphResourceData *FindGraphResource(
    MaterialGraphData &graph, std::string_view id) noexcept {
  const auto found = std::find_if(
      graph.resources.begin(), graph.resources.end(),
      [id](const auto &resource) { return resource.id == id; });
  return found != graph.resources.end() ? &*found : nullptr;
}

bool LooksLikeColor(std::string_view displayName,
                    std::string_view id) {
  std::string folded(displayName);
  folded.append(id);
  std::transform(folded.begin(), folded.end(), folded.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return folded.find("color") != std::string::npos ||
         folded.find("tint") != std::string::npos;
}

namespace {
constexpr uint32_t PackColor(uint32_t red, uint32_t green,
                             uint32_t blue, uint32_t alpha = 255u) noexcept {
  return red | (green << 8u) | (blue << 16u) | (alpha << 24u);
}
} // namespace

uint32_t NodeHeaderColor(MaterialNodeKind kind) noexcept {
  switch (kind) {
  case MaterialNodeKind::PbrSurface:
  case MaterialNodeKind::SurfaceOutput:
    return PackColor(92u, 66u, 142u);
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::AnisotropicHair:
  case MaterialNodeKind::FaceSdf:
  case MaterialNodeKind::EyeLighting:
  case MaterialNodeKind::SkinLighting:
  case MaterialNodeKind::FabricSheen:
  case MaterialNodeKind::HybridLighting:
  case MaterialNodeKind::Outline:
    return PackColor(40u, 112u, 126u);
  case MaterialNodeKind::TextureSample:
  case MaterialNodeKind::Parameter:
  case MaterialNodeKind::Constant:
    return PackColor(86u, 94u, 108u);
  default:
    return PackColor(83u, 74u, 56u);
  }
}

uint32_t PinColor(std::string_view pin) noexcept {
  if (pin == "Surface") return PackColor(186u, 128u, 255u);
  if (pin.find("Response") != std::string_view::npos) {
    return PackColor(255u, 166u, 76u);
  }
  if (pin == "Outline") return PackColor(244u, 118u, 143u);
  if (pin == "Color" || pin == "RGB" || pin == "Additive" ||
      pin == "Emissive" || pin == "BaseColor" || pin == "SpecularColor") {
    return PackColor(109u, 208u, 232u);
  }
  return PackColor(188u, 205u, 214u);
}

std::string PinKey(std::string_view nodeId, std::string_view pin) {
  std::string key{};
  key.reserve(nodeId.size() + pin.size() + 1u);
  key.append(nodeId);
  key.push_back(':');
  key.append(pin);
  return key;
}

float NodeInlineHeight(MaterialNodeKind kind) noexcept {
  switch (kind) {
  case MaterialNodeKind::Parameter:
    return 52.0f;
  case MaterialNodeKind::Constant:
    return 34.0f;
  case MaterialNodeKind::TextureSample:
    return 58.0f;
  case MaterialNodeKind::RampDiffuse:
    return 334.0f;
  case MaterialNodeKind::MultiRampDiffuse:
    return 382.0f;
  case MaterialNodeKind::PbrSurface:
    return 515.0f;
  case MaterialNodeKind::SurfaceOutput:
    return 26.0f;
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::FresnelRim:
    return 136.0f;
  case MaterialNodeKind::FaceSdf:
  case MaterialNodeKind::FabricSheen:
    return kind == MaterialNodeKind::FaceSdf ? 276.0f : 154.0f;
  case MaterialNodeKind::AnisotropicHair:
    return 200.0f;
  case MaterialNodeKind::MatCap:
    return 102.0f;
  case MaterialNodeKind::EyeLighting:
  case MaterialNodeKind::SkinLighting:
    return kind == MaterialNodeKind::EyeLighting ? 148.0f : 126.0f;
  case MaterialNodeKind::HybridLighting:
    return 84.0f;
  case MaterialNodeKind::Outline:
    return 112.0f;
  default:
    return 0.0f;
  }
}

bool PinHasConnection(const MaterialGraphData &graph,
                      std::string_view nodeId, std::string_view pin,
                      bool output) noexcept {
  return std::any_of(
      graph.edges.begin(), graph.edges.end(),
      [nodeId, pin, output](const MaterialGraphEdgeData &edge) {
        return output
            ? edge.sourceNodeId == nodeId && edge.sourcePin == pin
            : edge.targetNodeId == nodeId && edge.targetPin == pin;
      });
}

float NodeLogicalHeight(const MaterialGraphData &graph,
                        const MaterialGraphNodeData &node,
                        bool collapsed) {
  std::vector<std::string_view> inputs = InputPins(node.kind);
  std::vector<std::string_view> outputs = OutputPins(node.kind);
  if (collapsed) {
    std::erase_if(inputs, [&](std::string_view pin) {
      return !PinHasConnection(graph, node.id, pin, false);
    });
    std::erase_if(outputs, [&](std::string_view pin) {
      return !PinHasConnection(graph, node.id, pin, true);
    });
  }
  const size_t visibleInputs = collapsed
      ? inputs.size()
      : static_cast<size_t>(std::count_if(
            inputs.begin(), inputs.end(),
            [kind = node.kind](std::string_view pin) {
                return !IsInlineOwnedInput(kind, pin);
            }));
  const size_t rows = (std::max)(
      size_t{1u}, (std::max)(visibleInputs, outputs.size()));
  const float inlineHeight = collapsed
      ? 0.0f
      : NodeInlineHeight(node.kind);
  return kHeaderHeight + kNodePadding +
         static_cast<float>(rows) * kPinRowHeight + inlineHeight;
}

bool HitNodeAuthoringControls(const MATH::Vec2 &pointer,
                              const MATH::Vec2 &minimum,
                              const MATH::Vec2 &maximum,
                              float headerHeight,
                              float inlineHeight) noexcept {
  const bool overCollapse = pointer.x >= maximum.x - 30.0f &&
      pointer.x <= maximum.x && pointer.y >= minimum.y &&
      pointer.y <= minimum.y + headerHeight;
  const bool overInline = inlineHeight > 0.0f &&
      pointer.x >= minimum.x && pointer.x <= maximum.x &&
      pointer.y >= minimum.y + headerHeight &&
      pointer.y <= minimum.y + headerHeight + inlineHeight;
  return overCollapse || overInline;
}

MaterialGraphResourceData *FindRampResource(
    MaterialGraphData &graph, const MaterialGraphNodeData &node) noexcept {
  if (MaterialGraphResourceData *resource =
          FindGraphResource(graph, node.referenceId)) {
    if (resource->semantic == MaterialResourceSemantic::Ramp ||
        resource->type == MaterialResourceType::Ramp1D ||
        resource->type == MaterialResourceType::Ramp2D) {
      return resource;
    }
  }
  const auto found = std::find_if(
      graph.resources.begin(), graph.resources.end(), [](const auto &resource) {
        return resource.semantic == MaterialResourceSemantic::Ramp ||
               resource.type == MaterialResourceType::Ramp1D ||
               resource.type == MaterialResourceType::Ramp2D;
      });
  return found != graph.resources.end() ? &*found : nullptr;
}

void DrawRampStrip(const MaterialRampData &ramp, float width, float height) {
#if defined(HIKARI_WITH_EDITOR)
  ASSETS::MATERIAL::MaterialRampRasterImage image{};
  std::string error{};
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(origin, {origin.x + width, origin.y + height},
                          IM_COL32(23, 27, 33, 255), 3.0f);
  if (ASSETS::MATERIAL::RasterizeMaterialRamp(ramp, image, error) &&
      image.width > 0u && image.height > 0u) {
    constexpr uint32_t kSegments = 64u;
    const uint32_t row = (std::min)(image.height / 2u, image.height - 1u);
    for (uint32_t segment = 0u; segment < kSegments; ++segment) {
      const uint32_t x = (std::min)(
          image.width - 1u, segment * image.width / kSegments);
      const size_t pixel =
          (static_cast<size_t>(row) * image.width + x) * 4u;
      const ImU32 color = IM_COL32(
          image.rgba8[pixel + 0u], image.rgba8[pixel + 1u],
          image.rgba8[pixel + 2u], image.rgba8[pixel + 3u]);
      const float x0 = origin.x + width *
          static_cast<float>(segment) / static_cast<float>(kSegments);
      const float x1 = origin.x + width *
          static_cast<float>(segment + 1u) / static_cast<float>(kSegments);
      drawList->AddRectFilled({x0, origin.y}, {x1 + 1.0f, origin.y + height},
                              color);
    }
  }
  drawList->AddRect(origin, {origin.x + width, origin.y + height},
                    IM_COL32(112, 126, 142, 255), 3.0f);
  ImGui::InvisibleButton("##RampStrip", {width, height});
#else
  (void)ramp;
  (void)width;
  (void)height;
#endif
}

MaterialValueType DefaultNodeValueType(MaterialNodeKind kind) noexcept {
  switch (kind) {
  case MaterialNodeKind::PbrSurface:
  case MaterialNodeKind::SurfaceOutput:
    return MaterialValueType::Surface;
  case MaterialNodeKind::RampDiffuse:
  case MaterialNodeKind::MultiRampDiffuse:
  case MaterialNodeKind::FaceSdf:
  case MaterialNodeKind::SkinLighting:
  case MaterialNodeKind::HybridLighting:
    return MaterialValueType::DiffuseResponse;
  case MaterialNodeKind::Outline:
    return MaterialValueType::OutlineResponse;
  case MaterialNodeKind::SpecularBand:
  case MaterialNodeKind::AnisotropicHair:
    return MaterialValueType::SpecularResponse;
  case MaterialNodeKind::FresnelRim:
  case MaterialNodeKind::MatCap:
  case MaterialNodeKind::EyeLighting:
  case MaterialNodeKind::NormalUnpack:
  case MaterialNodeKind::FabricSheen:
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
    return "Advanced / Inputs";
  case MaterialNodeKind::Add:
  case MaterialNodeKind::Multiply:
  case MaterialNodeKind::Lerp:
  case MaterialNodeKind::Clamp:
  case MaterialNodeKind::Saturate:
  case MaterialNodeKind::OneMinus:
  case MaterialNodeKind::Power:
  case MaterialNodeKind::Dot:
  case MaterialNodeKind::NormalUnpack:
    return "Advanced / Math & Masks";
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
  case MaterialNodeKind::SkinLighting:
  case MaterialNodeKind::FabricSheen:
  case MaterialNodeKind::HybridLighting:
  case MaterialNodeKind::Outline:
    return "Ready-made Shading";
  case MaterialNodeKind::FunctionCall:
    return "Functions";
  }
  return "Other";
}

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
