#include "Assets/Material/Authoring/Internal/HIKARI_MaterialTemplateGraphBuilder.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"

namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL {
namespace {

// テンプレートも通常の公開ノードとパラメーターだけで組み立てる。
struct StrokeGraph {
    MaterialAssetData& data;
    float row = 0.0f;

    std::string Node(std::string name, MaterialNodeKind kind, MaterialValueType type = MaterialValueType::Float1) {
        std::string id = "node.stroke." + name;
        std::replace(id.begin(), id.end(), ' ', '_');
        AddNode(data, id, name, kind, type, -1400.0f, row);
        row += 140.0f;
        data.graph.nodes.back().value.type = type;
        return id;
    }

    std::string Parameter(std::string name, std::string label, std::string group, float value, float minimum,
                          float maximum) {
        const auto id = Node(name, MaterialNodeKind::Parameter);
        auto& node = data.graph.nodes.back();
        node.referenceId = "parameter." + name;
        MaterialGraphParameterData parameter{};
        parameter.id = node.referenceId;
        parameter.displayName = std::move(label);
        parameter.defaultValue.numeric.x = value;
        parameter.group = std::move(group);
        parameter.useUiRange = true;
        parameter.uiMinimum = minimum;
        parameter.uiMaximum = maximum;
        data.graph.parameters.push_back(std::move(parameter));
        return id;
    }

    std::string Constant(std::string name, float value) {
        const auto id = Node(std::move(name), MaterialNodeKind::Constant);
        data.graph.nodes.back().value.numeric.x = value;
        return id;
    }

    void Link(const std::string& source, std::string_view output, const std::string& target, std::string_view input) {
        ConnectNodeInput(data, "edge.stroke." + std::to_string(data.graph.edges.size()), source, std::string(output),
                         target, std::string(input));
    }

    std::string Texture(std::string name, std::string_view asset, MaterialResourceSemantic semantic,
                        const std::string& uv) {
        MaterialGraphResourceData resource{};
        resource.id = "resource." + name;
        resource.displayName = name;
        resource.semantic = semantic;
        resource.assetGuid = AssetGuid{std::string(asset)};
        resource.enabled = true;
        data.graph.resources.push_back(resource);
        const auto id = Node(name, MaterialNodeKind::TextureSample, MaterialValueType::Float4);
        data.graph.nodes.back().referenceId = resource.id;
        Link(uv, "Value", id, "UV");
        return id;
    }
};

} // namespace

void AddPainterlyShellGraph(MaterialAssetData& data) {
    StrokeGraph graph{data};
    const float baseNormalStrength = data.pbr.normalScale;
    data.pbr.normalScale = 1.0f;
    for (auto& parameter : data.graph.parameters) {
        parameter.group = parameter.id.starts_with("parameter.ramp") || parameter.id.starts_with("parameter.shadow") ||
                                  parameter.id == "parameter.midThreshold" || parameter.id.ends_with("Tint") ||
                                  parameter.id == "parameter.toonDirectGain"
                              ? "material.painterly.groupLighting"
                              : "material.painterly.groupSurface";
        if (parameter.id == "parameter.normalScale") {
            parameter.defaultValue.numeric.x = 1.0f;
            parameter.exposed = false;
        }
    }
    data.surface.alphaMode = MaterialAlphaMode::Mask;
    const auto one = graph.Constant("One", 1.0f);
    const auto half = graph.Constant("Half", 0.5f);
    const auto size =
        graph.Parameter("strokeSize", "material.painterly.size", "material.painterly.groupStroke", 0.4f, 0.05f, 2.0f);
    const auto rotation = graph.Parameter("strokeRotation", "material.painterly.rotation",
                                          "material.painterly.groupStroke", 0.0f, -3.14159265f, 3.14159265f);
    const auto variation = graph.Parameter("strokeVariation", "material.painterly.variation",
                                           "material.painterly.groupStroke", 0.25f, 0.0f, 1.0f);
    const auto strength = graph.Parameter("strokeNormalStrength", "material.painterly.normalStrength",
                                          "material.painterly.groupStroke", 0.65f, 0.0f, 2.0f);
    const auto baseStrength = graph.Parameter("strokeBaseNormalStrength", "material.painterly.baseNormalStrength",
                                              "material.painterly.groupSurface", baseNormalStrength, 0.0f, 2.0f);
    const auto density = graph.Parameter("strokeDensity", "material.painterly.density", "material.painterly.groupShell",
                                         0.72f, 0.0f, 1.0f);
    const auto edgePower = graph.Parameter("strokeEdgePower", "material.painterly.edgePower",
                                           "material.painterly.groupShell", 1.5f, 0.1f, 8.0f);
    const auto maxLayers = graph.Parameter("shellMaxLayers", "material.painterly.maxLayers",
                                           "material.painterly.groupShell", 4.0f, 0.0f, 8.0f);
    const auto thickness = graph.Parameter("shellThickness", "material.painterly.thickness",
                                           "material.painterly.groupShell", 0.015f, 0.0f, 0.15f);
    const auto lodStart = graph.Parameter("shellLodStartPixels", "material.painterly.lodStart",
                                          "material.painterly.groupShell", 24.0f, 1.0f, 256.0f);
    const auto lodFull = graph.Parameter("shellLodFullPixels", "material.painterly.lodFull",
                                         "material.painterly.groupShell", 160.0f, 2.0f, 1024.0f);
    const auto shell = graph.Node("Shell", MaterialNodeKind::Shell, MaterialValueType::ShellSettings);
    graph.Link(maxLayers, "Value", shell, "MaxLayers");
    graph.Link(thickness, "Value", shell, "Thickness");
    graph.Link(lodStart, "Value", shell, "LodStartPixels");
    graph.Link(lodFull, "Value", shell, "LodFullPixels");
    graph.Link(shell, "Settings", "node.standardPbr", "Shell");

    const auto layer = graph.Node("Shell Layer", MaterialNodeKind::ShellLayer, MaterialValueType::Float4);
    const auto splitLayer = graph.Node("Layer Depth and Base", MaterialNodeKind::Split, MaterialValueType::Float4);
    graph.Link(layer, "Value", splitLayer, "Value");
    const auto tiling = graph.Node("Inverse Stroke Size", MaterialNodeKind::Divide);
    graph.Link(one, "Value", tiling, "A");
    graph.Link(size, "Value", tiling, "B");
    const auto scale = graph.Node("Stroke UV Scale", MaterialNodeKind::MakeFloat2, MaterialValueType::Float2);
    graph.Link(tiling, "Result", scale, "X");
    graph.Link(tiling, "Result", scale, "Y");
    const auto uv = graph.Node("Stroke UV", MaterialNodeKind::UvTransform, MaterialValueType::Float2);
    graph.Link(scale, "Value", uv, "Scale");
    graph.Link(rotation, "Value", uv, "Rotation");
    const auto layerShift = graph.Node("Stable Layer UV Shift", MaterialNodeKind::Constant, MaterialValueType::Float2);
    data.graph.nodes.back().value.numeric = {0.045f, 0.071f, 0.0f, 0.0f};
    const auto layerOffset = graph.Node("Layer UV Offset", MaterialNodeKind::Multiply, MaterialValueType::Float2);
    graph.Link(layerShift, "Value", layerOffset, "A");
    graph.Link(splitLayer, "X", layerOffset, "B");
    graph.Link(layerOffset, "Result", uv, "Offset");
    const auto mask =
        graph.Texture("strokeMask", kBuiltInPainterlyStrokeMaskTextureId, MaterialResourceSemantic::Mask, uv);
    const auto packedNormal =
        graph.Texture("strokeNormal", kBuiltInPainterlyStrokeNormalTextureId, MaterialResourceSemantic::Custom, uv);
    const auto unpack = graph.Node("Unpack Stroke Normal", MaterialNodeKind::NormalUnpack, MaterialValueType::Float3);
    graph.Link(packedNormal, "RGB", unpack, "PackedNormal");
    const auto oriented =
        graph.Node("Rotate Stroke Normal", MaterialNodeKind::NormalUvTransform, MaterialValueType::Float3);
    graph.Link(unpack, "Normal", oriented, "Normal");
    graph.Link(rotation, "Value", oriented, "Rotation");
    graph.Link(scale, "Value", oriented, "Scale");
    const auto blend =
        graph.Node("Blend Base and Stroke Normals", MaterialNodeKind::NormalBlend, MaterialValueType::Float3);
    graph.Link(oriented, "Normal", blend, "Detail");
    graph.Link(strength, "Value", blend, "DetailStrength");
    graph.Link(baseStrength, "Value", blend, "BaseStrength");
    for (auto& edge : data.graph.edges) {
        if (edge.targetNodeId == "node.standardPbr" && edge.targetPin == "Normal") {
            edge.targetNodeId = blend;
            edge.targetPin = "Base";
        }
    }
    graph.Link(blend, "Normal", "node.standardPbr", "Normal");

    // 同じマスクは色と稜線に使い、基底面は層数ゼロでも筆触の法線を残す。
    const auto centered = graph.Node("Centered Stroke", MaterialNodeKind::Subtract);
    graph.Link(mask, "R", centered, "A");
    graph.Link(half, "Value", centered, "B");
    const auto modulation = graph.Node("Stroke Color Strength", MaterialNodeKind::Multiply);
    graph.Link(centered, "Result", modulation, "A");
    graph.Link(variation, "Value", modulation, "B");
    const auto gain = graph.Node("Stroke Color Gain", MaterialNodeKind::Add);
    graph.Link(one, "Value", gain, "A");
    graph.Link(modulation, "Result", gain, "B");
    const auto baseColor = graph.Node("Base Color", MaterialNodeKind::Parameter, MaterialValueType::Float4);
    data.graph.nodes.back().referenceId = "parameter.baseColor";
    const auto tint = graph.Node("Base Color and Texture", MaterialNodeKind::Multiply, MaterialValueType::Float4);
    graph.Link(baseColor, "Value", tint, "A");
    bool hadTexture = false;
    for (auto& edge : data.graph.edges) {
        if (edge.targetNodeId == "node.standardPbr" && edge.targetPin == "BaseColor") {
            edge.targetNodeId = tint;
            edge.targetPin = "B";
            hadTexture = true;
        }
    }
    if (!hadTexture)
        graph.Link(one, "Value", tint, "B");
    const auto painted = graph.Node("Painted Color", MaterialNodeKind::Multiply, MaterialValueType::Float4);
    graph.Link(tint, "Result", painted, "A");
    graph.Link(gain, "Result", painted, "B");
    graph.Link(painted, "Result", "node.standardPbr", "BaseColor");

    const auto fresnel = graph.Node("Shell Edge Region", MaterialNodeKind::Fresnel);
    graph.Link(edgePower, "Value", fresnel, "Power");
    const auto edgeMask = graph.Node("Stroke Silhouette Mask", MaterialNodeKind::Multiply);
    graph.Link(mask, "R", edgeMask, "A");
    graph.Link(fresnel, "Result", edgeMask, "B");
    const auto threshold = graph.Node("Density Threshold", MaterialNodeKind::OneMinus);
    graph.Link(density, "Value", threshold, "Value");
    const auto outerThinning = graph.Node("Outer Layer Thinning", MaterialNodeKind::Multiply);
    graph.Link(splitLayer, "X", outerThinning, "A");
    graph.Link(graph.Constant("Outer Layer Falloff", 0.2f), "Value", outerThinning, "B");
    const auto cutoff = graph.Node("Layer Threshold", MaterialNodeKind::Add);
    graph.Link(threshold, "Result", cutoff, "A");
    graph.Link(outerThinning, "Result", cutoff, "B");
    const auto shellMask = graph.Node("Shell Stroke Coverage", MaterialNodeKind::Step);
    graph.Link(cutoff, "Result", shellMask, "Edge");
    graph.Link(edgeMask, "Result", shellMask, "Value");
    const auto baseAlpha = graph.Node("Original Surface Alpha", MaterialNodeKind::Split, MaterialValueType::Float4);
    graph.Link(tint, "Result", baseAlpha, "Value");
    const auto opacity = graph.Node("Preserve Base Surface", MaterialNodeKind::Select);
    graph.Link(shellMask, "Result", opacity, "False");
    graph.Link(baseAlpha, "W", opacity, "True");
    graph.Link(splitLayer, "Y", opacity, "Condition");
    graph.Link(opacity, "Result", "node.standardPbr", "Opacity");
    graph.Link(half, "Value", "node.standardPbr", "AlphaClipThreshold");
    ReconcileMaterialGraphDynamicNodeTypes(data.graph);
}

} // namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL
