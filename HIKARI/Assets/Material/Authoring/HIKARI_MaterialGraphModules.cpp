#include "Assets/Material/Authoring/HIKARI_MaterialGraphReuse.h"
#include "Assets/Material/Graph/HIKARI_MaterialGraphValidation.h"
#include "RenderContracts/Material/HIKARI_MaterialNodeDefinition.h"
#include <algorithm>
#include <array>

namespace HIKARI::ASSETS::MATERIAL {
namespace {
constexpr std::array kModules{
    MaterialGraphModuleInfo{"dissolve", "material.module.dissolve", "dissolve edge mask 溶解 边缘 消散 ディゾルブ"},
    MaterialGraphModuleInfo{"uv_flow", "material.module.uvFlow", "flow uv scroll 流动 流光 スクロール"},
    MaterialGraphModuleInfo{"radial_mask", "material.module.radial", "radial circle mask 径向 圆形 遮罩 円形 マスク"},
    MaterialGraphModuleInfo{"noise_breakup", "material.module.noise", "noise breakup variation 噪声 扰动 ノイズ"},
};
}
std::span<const MaterialGraphModuleInfo> GetMaterialGraphModules() noexcept { return kModules; }
MaterialGraphData CreateMaterialGraphModule(std::string_view id) {
    MaterialGraphData graph;
    const auto node = [&](std::string name, MaterialNodeKind kind, MaterialValueType type = MaterialValueType::Float1) {
        MaterialGraphNodeData n;
        n.id = name;
        n.displayName = name;
        std::replace(n.id.begin(), n.id.end(), ' ', '_');
        n.kind = kind;
        n.valueType = type;
        n.value.type = type;
        graph.nodes.push_back(std::move(n));
        return graph.nodes.back().id;
    };
    const auto parameter = [&](std::string name, MaterialValueType type, MATH::Vec4 value, float min, float max) {
        const auto n = node(name, MaterialNodeKind::Parameter, type);
        auto& item = graph.nodes.back();
        item.referenceId = "parameter." + n;
        MaterialGraphParameterData p;
        p.id = item.referenceId;
        p.displayName = name;
        p.defaultValue.type = type;
        p.defaultValue.numeric = value;
        p.group = std::string(id);
        p.useUiRange = type == MaterialValueType::Float1;
        p.uiMinimum = min;
        p.uiMaximum = max;
        graph.parameters.push_back(p);
        return n;
    };
    const auto link = [&](const std::string& from, const std::string& to, std::string_view pin,
                          std::string_view output = {}) {
        const auto found =
            std::find_if(graph.nodes.begin(), graph.nodes.end(), [&](const auto& n) { return n.id == from; });
        if (output.empty() && found != graph.nodes.end())
            output = GetMaterialGraphNodeOutputPins(*found).front();
        graph.edges.push_back(
            {"edge" + std::to_string(graph.edges.size()), from, std::string(output), to, std::string(pin)});
    };
    if (id == "uv_flow") {
        const auto uv = node("UV", MaterialNodeKind::TextureCoordinate, MaterialValueType::Float2);
        const auto time = node("Time", MaterialNodeKind::Time);
        const auto speed = parameter("Speed", MaterialValueType::Float2, {0.15f, 0.0f, 0.0f, 0.0f}, -2, 2);
        const auto offset = node("Offset", MaterialNodeKind::Multiply, MaterialValueType::Float2);
        const auto flow = node("Flow UV", MaterialNodeKind::Add, MaterialValueType::Float2);
        link(time, offset, "A");
        link(speed, offset, "B");
        link(uv, flow, "A");
        link(offset, flow, "B");
        graph.outputNodeId = flow;
    } else if (id == "radial_mask") {
        const auto uv = node("UV", MaterialNodeKind::TextureCoordinate, MaterialValueType::Float2);
        const auto center = parameter("Center", MaterialValueType::Float2, {0.5f, 0.5f, 0, 0}, 0, 1);
        const auto radius = parameter("Radius", MaterialValueType::Float1, {0.4f, 0, 0, 0}, 0.001f, 1);
        const auto softness = parameter("Softness", MaterialValueType::Float1, {0.08f, 0, 0, 0}, 0.001f, 1);
        const auto distance = node("Distance", MaterialNodeKind::Distance);
        const auto outer = node("Outer radius", MaterialNodeKind::Add);
        const auto fade = node("Fade", MaterialNodeKind::SmoothStep);
        const auto mask = node("Radial mask", MaterialNodeKind::OneMinus);
        link(uv, distance, "A");
        link(center, distance, "B");
        link(radius, outer, "A");
        link(softness, outer, "B");
        link(radius, fade, "Edge0");
        link(outer, fade, "Edge1");
        link(distance, fade, "Value");
        link(fade, mask, "Value");
        graph.outputNodeId = mask;
    } else if (id == "dissolve" || id == "noise_breakup") {
        const auto uv = node("UV", MaterialNodeKind::TextureCoordinate, MaterialValueType::Float2);
        const auto scale = parameter("Scale", MaterialValueType::Float1, {6, 0, 0, 0}, 0.01f, 32);
        const auto scaled = node("Scaled UV", MaterialNodeKind::Multiply, MaterialValueType::Float2);
        const auto noise = node("Noise", MaterialNodeKind::ValueNoise);
        link(uv, scaled, "A");
        link(scale, scaled, "B");
        link(scaled, noise, "UV");
        if (id == "dissolve") {
            const auto threshold = parameter("Progress", MaterialValueType::Float1, {0.5f, 0, 0, 0}, 0, 1);
            const auto width = parameter("Edge width", MaterialValueType::Float1, {0.07f, 0, 0, 0}, 0.001f, 0.5f);
            const auto end = node("Edge end", MaterialNodeKind::Add);
            const auto mask = node("Opacity mask", MaterialNodeKind::Step);
            const auto inner = node("Inner mask", MaterialNodeKind::Step);
            const auto edge = node("Emissive edge mask", MaterialNodeKind::Subtract);
            link(threshold, end, "A");
            link(width, end, "B");
            link(threshold, mask, "Edge");
            link(noise, mask, "Value");
            link(end, inner, "Edge");
            link(noise, inner, "Value");
            link(mask, edge, "A");
            link(inner, edge, "B");
            graph.outputNodeId = edge;
        } else {
            const auto contrast = parameter("Contrast", MaterialValueType::Float1, {2, 0, 0, 0}, 0.1f, 8);
            const auto breakup = node("Noise breakup", MaterialNodeKind::Power);
            link(noise, breakup, "Base");
            link(contrast, breakup, "Exponent");
            graph.outputNodeId = breakup;
        }
    }
    ReconcileMaterialGraphDynamicNodeTypes(graph);
    return graph;
}
} // namespace HIKARI::ASSETS::MATERIAL
