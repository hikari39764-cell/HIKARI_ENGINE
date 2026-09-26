#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include "Assets/Material/Graph/HIKARI_MaterialGraphQuery.h"
#include <algorithm>
#include <cctype>
#include <unordered_set>

#include "Assets/Material/Compiler/HIKARI_MaterialRampRasterizer.h"
#include "Assets/Material/Graph/HIKARI_MaterialNodeDefinition.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {

namespace {

bool InputDependsOn(const MaterialGraphData& graph, const MaterialGraphNodeData& node, std::string_view pin,
                    std::string_view reference, MaterialNodeKind kind) {
    const auto* edge = FindMaterialGraphInputEdge(graph, node.id, pin);
    if (!edge)
        return true;
    std::unordered_set<std::string> visited;
    const auto dependsOn = [&](auto&& self, std::string_view id) -> bool {
        if (!visited.emplace(id).second)
            return false;
        const auto* upstream = FindMaterialGraphNode(graph, id);
        if (!upstream)
            return false;
        if (upstream->kind == kind)
            return upstream->referenceId == reference;
        for (const auto& input : graph.edges)
            if (input.targetNodeId == id && self(self, input.sourceNodeId))
                return true;
        return false;
    };
    return dependsOn(dependsOn, edge->sourceNodeId);
}

} // namespace

bool InputUsesParameterDefault(const MaterialGraphData& graph, const MaterialGraphNodeData& node, std::string_view pin,
                               std::string_view parameterId) {
    return InputDependsOn(graph, node, pin, parameterId, MaterialNodeKind::Parameter);
}

bool InputUsesResourceDefault(const MaterialGraphData& graph, const MaterialGraphNodeData& node, std::string_view pin,
                              std::string_view resourceId) {
    return InputDependsOn(graph, node, pin, resourceId, MaterialNodeKind::TextureSample);
}

MaterialGraphData* EditableGraph(MaterialAssetData& source) noexcept {
    if (source.kind == MaterialAssetKind::Material) {
        return &source.graph;
    }
    if (source.kind == MaterialAssetKind::Function) {
        return &source.function.graph;
    }
    return nullptr;
}

std::vector<std::string_view> InputPins(const MaterialNodeTypeId& typeId) {
    std::vector<std::string_view> pins = GetMaterialGraphNodeInputPins(typeId);
    std::erase_if(pins, [&typeId](std::string_view pin) { return IsRuntimeDrivenInput(typeId, pin); });
    return pins;
}

std::vector<std::string_view> InputPins(const MaterialGraphNodeData& node) {
    std::vector<std::string_view> pins = GetMaterialGraphNodeInputPins(node);
    std::erase_if(pins, [&node](std::string_view pin) { return IsRuntimeDrivenInput(node.kind, pin); });
    return pins;
}

bool LooksLikeColor(std::string_view displayName, std::string_view id) {
    std::string folded(displayName);
    folded.append(id);
    std::transform(folded.begin(), folded.end(), folded.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return folded.find("color") != std::string::npos || folded.find("tint") != std::string::npos;
}

namespace {
constexpr uint32_t PackColor(uint32_t red, uint32_t green, uint32_t blue, uint32_t alpha = 255u) noexcept {
    return red | (green << 8u) | (blue << 16u) | (alpha << 24u);
}
} // namespace

uint32_t NodeHeaderColor(const MaterialNodeTypeId& typeId) noexcept {
    const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(typeId);
    if (definition == nullptr) {
        return PackColor(83u, 74u, 56u);
    }
    switch (definition->semanticClass) {
    case MaterialNodeSemanticClass::Surface: return PackColor(92u, 66u, 142u);
    case MaterialNodeSemanticClass::Shading: return PackColor(40u, 112u, 126u);
    case MaterialNodeSemanticClass::Input: return PackColor(86u, 94u, 108u);
    case MaterialNodeSemanticClass::Math:
    case MaterialNodeSemanticClass::Function:
    default: return PackColor(83u, 74u, 56u);
    }
}

uint32_t PinColor(std::string_view pin) noexcept {
    if (pin == "Surface")
        return PackColor(186u, 128u, 255u);
    if (pin.find("Response") != std::string_view::npos) {
        return PackColor(255u, 166u, 76u);
    }
    if (pin == "Outline")
        return PackColor(244u, 118u, 143u);
    if (pin == "Color" || pin == "RGB" || pin == "Eye" || pin == "Control" || pin == "Additive" || pin == "Emissive" ||
        pin == "BaseColor" || pin == "SpecularColor") {
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

NodeInlineLayoutSpec ResolveNodeInlineLayoutSpec(const MaterialNodeTypeId& typeId) noexcept {
    MaterialNodeKind kind{};
    if (!TryGetBuiltinMaterialNodeKind(typeId, kind)) {
        return {};
    }
    switch (kind) {
    case MaterialNodeKind::Variable: return {1u, 0.0f};
    case MaterialNodeKind::Parameter: return {2u, 0.0f};
    case MaterialNodeKind::Constant: return {1u, 0.0f};
    case MaterialNodeKind::TextureSample: return {5u, 20.0f};
    case MaterialNodeKind::RampDiffuse: return {11u, 68.0f};
    case MaterialNodeKind::MultiRampDiffuse: return {17u, 104.0f};
    case MaterialNodeKind::PbrSurface: return {8u, 58.0f};
    case MaterialNodeKind::SurfaceOutput: return {7u, 0.0f};
    case MaterialNodeKind::ParticleOutput: return {4u, 0.0f};
    case MaterialNodeKind::SpecularBand:
    case MaterialNodeKind::FresnelRim: return {5u, 0.0f};
    case MaterialNodeKind::FaceSdf:
    case MaterialNodeKind::FabricSheen:
        return kind == MaterialNodeKind::FaceSdf ? NodeInlineLayoutSpec{7u, 88.0f} : NodeInlineLayoutSpec{6u, 0.0f};
    case MaterialNodeKind::AnisotropicHair: return {13u, 48.0f};
    case MaterialNodeKind::MatCap: return {6u, 0.0f};
    case MaterialNodeKind::EyeLighting:
    case MaterialNodeKind::CorneaHighlight:
    case MaterialNodeKind::SkinLighting:
        return kind == MaterialNodeKind::EyeLighting       ? NodeInlineLayoutSpec{20u, 0.0f}
               : kind == MaterialNodeKind::CorneaHighlight ? NodeInlineLayoutSpec{12u, 24.0f}
                                                           : NodeInlineLayoutSpec{5u, 0.0f};
    case MaterialNodeKind::HybridLighting: return {4u, 0.0f};
    case MaterialNodeKind::Outline: return {5u, 0.0f};
    case MaterialNodeKind::HairShadow: return {4u, 0.0f};
    default: return {};
    }
}

float NodeInlineHeight(const MaterialNodeTypeId& typeId) noexcept {
    const NodeInlineLayoutSpec spec = ResolveNodeInlineLayoutSpec(typeId);
    if (spec.rowCount == 0u && spec.fixedHeight == 0.0f) {
        return 0.0f;
    }
    return static_cast<float>(spec.rowCount) * kInlineControlRowHeight + spec.fixedHeight + kInlineControlSafetyPadding;
}

bool PinHasConnection(const MaterialGraphData& graph, std::string_view nodeId, std::string_view pin,
                      bool output) noexcept {
    return std::any_of(graph.edges.begin(), graph.edges.end(),
                       [nodeId, pin, output](const MaterialGraphEdgeData& edge) {
                           return output ? edge.sourceNodeId == nodeId && edge.sourcePin == pin
                                         : edge.targetNodeId == nodeId && edge.targetPin == pin;
                       });
}

std::string NodeDisplayName(const MaterialNodeTypeId& typeId) {
    const std::string name(ToString(typeId));
    const std::string key = "material.node." + name;
    const std::string translated = TEXT::Localize(key);
    return translated == key ? name : translated;
}

float NodeLogicalHeight(const MaterialGraphData& graph, const MaterialGraphNodeData& node, bool collapsed) {
    std::vector<std::string_view> inputs = InputPins(node);
    std::vector<std::string_view> outputs = GetMaterialGraphNodeOutputPins(node);
    if (collapsed) {
        std::erase_if(inputs, [&](std::string_view pin) { return !PinHasConnection(graph, node.id, pin, false); });
        std::erase_if(outputs, [&](std::string_view pin) { return !PinHasConnection(graph, node.id, pin, true); });
    }
    const size_t visibleInputs =
        collapsed
            ? inputs.size()
            : static_cast<size_t>(std::count_if(inputs.begin(), inputs.end(), [kind = node.kind](std::string_view pin) {
                  return !IsInlineOwnedInput(kind, pin);
              }));
    const size_t rows = (std::max)(size_t{1u}, (std::max)(visibleInputs, outputs.size()));
    const float inlineHeight = collapsed ? 0.0f : NodeInlineHeight(node.kind);
    return kHeaderHeight + kNodePadding + static_cast<float>(rows) * kPinRowHeight + inlineHeight;
}

bool HitNodeAuthoringControls(const MATH::Vec2& pointer, const MATH::Vec2& minimum, const MATH::Vec2& maximum,
                              float headerHeight, float inlineHeight) noexcept {
    const bool overCollapse = pointer.x >= maximum.x - 30.0f && pointer.x <= maximum.x && pointer.y >= minimum.y &&
                              pointer.y <= minimum.y + headerHeight;
    const bool overInline = inlineHeight > 0.0f && pointer.x >= minimum.x && pointer.x <= maximum.x &&
                            pointer.y >= minimum.y + headerHeight &&
                            pointer.y <= minimum.y + headerHeight + inlineHeight;
    return overCollapse || overInline;
}

MaterialGraphResourceData* FindRampResource(MaterialGraphData& graph, const MaterialGraphNodeData& node) noexcept {
    if (MaterialGraphResourceData* resource = FindMaterialGraphResource(graph, node.referenceId)) {
        if (resource->semantic == MaterialResourceSemantic::Ramp || resource->type == MaterialResourceType::Ramp1D ||
            resource->type == MaterialResourceType::Ramp2D) {
            return resource;
        }
    }
    const auto found = std::find_if(graph.resources.begin(), graph.resources.end(), [](const auto& resource) {
        return resource.semantic == MaterialResourceSemantic::Ramp || resource.type == MaterialResourceType::Ramp1D ||
               resource.type == MaterialResourceType::Ramp2D;
    });
    return found != graph.resources.end() ? &*found : nullptr;
}

void DrawRampStrip(const MaterialRampData& ramp, float width, float height) {
#if defined(HIKARI_WITH_EDITOR)
    ASSETS::MATERIAL::MaterialRampRasterImage image{};
    std::string error{};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + width, origin.y + height}, IM_COL32(23, 27, 33, 255), 3.0f);
    if (ASSETS::MATERIAL::RasterizeMaterialRamp(ramp, image, error) && image.width > 0u && image.height > 0u) {
        constexpr uint32_t kSegments = 64u;
        const uint32_t row = (std::min)(image.height / 2u, image.height - 1u);
        for (uint32_t segment = 0u; segment < kSegments; ++segment) {
            const uint32_t x = (std::min)(image.width - 1u, segment * image.width / kSegments);
            const size_t pixel = (static_cast<size_t>(row) * image.width + x) * 4u;
            const ImU32 color = IM_COL32(image.rgba8[pixel + 0u], image.rgba8[pixel + 1u], image.rgba8[pixel + 2u],
                                         image.rgba8[pixel + 3u]);
            const float x0 = origin.x + width * static_cast<float>(segment) / static_cast<float>(kSegments);
            const float x1 = origin.x + width * static_cast<float>(segment + 1u) / static_cast<float>(kSegments);
            drawList->AddRectFilled({x0, origin.y}, {x1 + 1.0f, origin.y + height}, color);
        }
    }
    drawList->AddRect(origin, {origin.x + width, origin.y + height}, IM_COL32(112, 126, 142, 255), 3.0f);
    ImGui::InvisibleButton("##RampStrip", {width, height});
#else
    (void)ramp;
    (void)width;
    (void)height;
#endif
}

MaterialValueType DefaultNodeValueType(const MaterialNodeTypeId& typeId) noexcept {
    const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(typeId);
    return definition == nullptr ? MaterialValueType::Float1 : definition->defaultValueType;
}

std::string_view NodeMathSubgroup(MaterialNodeKind kind) {
    switch (kind) {
    case MaterialNodeKind::Add:
    case MaterialNodeKind::Subtract:
    case MaterialNodeKind::Multiply:
    case MaterialNodeKind::Divide:
    case MaterialNodeKind::Minimum:
    case MaterialNodeKind::Maximum:
    case MaterialNodeKind::Absolute:
    case MaterialNodeKind::Negate:
    case MaterialNodeKind::Modulo:
    case MaterialNodeKind::Power:
    case MaterialNodeKind::SquareRoot: return TEXT::LocalizeCStr("editor.material.math.basic");
    case MaterialNodeKind::Sine:
    case MaterialNodeKind::Cosine:
    case MaterialNodeKind::Atan2: return TEXT::LocalizeCStr("editor.material.math.trigonometry");
    case MaterialNodeKind::Exp:
    case MaterialNodeKind::Log2: return TEXT::LocalizeCStr("editor.material.math.exponential");
    case MaterialNodeKind::Floor:
    case MaterialNodeKind::Ceil:
    case MaterialNodeKind::Fraction:
    case MaterialNodeKind::Round:
    case MaterialNodeKind::Trunc:
    case MaterialNodeKind::Sign:
    case MaterialNodeKind::Saturate:
    case MaterialNodeKind::OneMinus:
    case MaterialNodeKind::Clamp: return TEXT::LocalizeCStr("editor.material.math.range");
    case MaterialNodeKind::Step:
    case MaterialNodeKind::SmoothStep:
    case MaterialNodeKind::Remap: return TEXT::LocalizeCStr("editor.material.math.compare");
    case MaterialNodeKind::Dot:
    case MaterialNodeKind::Cross:
    case MaterialNodeKind::Length:
    case MaterialNodeKind::Normalize:
    case MaterialNodeKind::Distance:
    case MaterialNodeKind::Reflect:
    case MaterialNodeKind::MakeFloat2:
    case MaterialNodeKind::MakeFloat3:
    case MaterialNodeKind::MakeFloat4:
    case MaterialNodeKind::Split:
    case MaterialNodeKind::Select: return TEXT::LocalizeCStr("editor.material.math.vector");
    case MaterialNodeKind::ValueNoise:
    case MaterialNodeKind::Voronoi: return TEXT::LocalizeCStr("editor.material.math.noise");
    case MaterialNodeKind::UvTransform: return TEXT::LocalizeCStr("editor.material.math.uv");
    case MaterialNodeKind::Fresnel: return TEXT::LocalizeCStr("editor.material.math.shading");
    default: return {};
    }
}

std::string_view NodeCategory(const MaterialNodeTypeId& typeId) noexcept {
    const MaterialNodeDefinition* definition = FindMaterialNodeDefinition(typeId);
    if (definition == nullptr) {
        return "Other";
    }
    switch (definition->semanticClass) {
    case MaterialNodeSemanticClass::Input: return "Advanced / Inputs";
    case MaterialNodeSemanticClass::Math: return "Advanced / Math & Masks";
    case MaterialNodeSemanticClass::Surface: return "Surface";
    case MaterialNodeSemanticClass::Shading: return "Ready-made Shading";
    case MaterialNodeSemanticClass::Function: return "Functions";
    }
    return "Other";
}

} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
