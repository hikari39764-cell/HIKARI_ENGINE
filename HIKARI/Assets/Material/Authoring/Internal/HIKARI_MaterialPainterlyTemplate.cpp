#include "Assets/Material/Authoring/Internal/HIKARI_MaterialTemplateGraphBuilder.h"

#include <algorithm>
#include <string>
#include <string_view>

#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "RenderContracts/Material/HIKARI_MaterialShellFields.h"

namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL {
namespace {

// 筆触の殻は renderer の固定経路。グラフは Shell ノードへ繋ぐ uniform パラメーターと、Shell ノードが所有する
// 2 枚の筆触テクスチャだけで、画素ごとの式は持たない。既定値は MaterialShellSettings が持ち、ここは
// パラメーター名・表示名・グループ・UI 範囲だけを決める。
struct ShellPin {
    const char* pin;
    const char* parameter;
    const char* label;
    const char* group;
    float uiMinimum;
    float uiMaximum;
};

constexpr ShellPin kShellPins[]{
    {"StrokeTiling", "strokeTiling", "material.painterly.tiling", "material.painterly.groupStroke", 0.1f, 8.0f},
    {"StrokeRotation", "strokeRotation", "material.painterly.rotation", "material.painterly.groupStroke", -3.14159265f,
     3.14159265f},
    {"StrokeVariation", "strokeVariation", "material.painterly.variation", "material.painterly.groupStroke", 0.0f,
     1.0f},
    {"StrokeNormalStrength", "strokeNormalStrength", "material.painterly.normalStrength",
     "material.painterly.groupStroke", 0.0f, 2.0f},
    {"Coverage", "strokeCoverage", "material.painterly.density", "material.painterly.groupShell", 0.0f, 1.0f},
    {"EdgePower", "strokeEdgePower", "material.painterly.edgePower", "material.painterly.groupShell", 0.1f, 8.0f},
    {"MaxLayers", "shellMaxLayers", "material.painterly.maxLayers", "material.painterly.groupShell", 0.0f, 8.0f},
    {"Thickness", "shellThickness", "material.painterly.thickness", "material.painterly.groupShell", 0.0f, 0.15f},
    {"LodStartPixels", "shellLodStartPixels", "material.painterly.lodStart", "material.painterly.groupShell", 1.0f,
     256.0f},
    {"LodFullPixels", "shellLodFullPixels", "material.painterly.lodFull", "material.painterly.groupShell", 2.0f,
     1024.0f},
};

void AddStrokeResource(MaterialAssetData& data, std::string id, std::string displayName,
                       MaterialResourceSemantic semantic, std::string_view asset) {
    MaterialGraphResourceData resource{};
    resource.id = std::move(id);
    resource.displayName = std::move(displayName);
    resource.semantic = semantic;
    resource.assetGuid = AssetGuid{std::string(asset)};
    resource.enabled = true;
    data.graph.resources.push_back(std::move(resource));
}

} // namespace

void AddPainterlyShellGraph(MaterialAssetData& data) {
    for (auto& parameter : data.graph.parameters)
        parameter.group = "material.painterly.groupSurface";
    data.surface.alphaMode = MaterialAlphaMode::Mask;
    AddNode(data, "node.shell", "Painterly Shell", MaterialNodeKind::Shell, MaterialValueType::ShellSettings, -360.0f,
            420.0f);
    ConnectNodeInput(data, "edge.shell.settings", "node.shell", "Settings", "node.standardPbr", "Shell");

    MaterialShellSettings defaults{};
    defaults.maxLayers = 4u;
    float row = 0.0f;
    kMaterialShellFields([&](const char* pin, auto member, JSONFIELDS::Range) {
        const auto entry = std::find_if(std::begin(kShellPins), std::end(kShellPins), [pin](const ShellPin& candidate) {
            return std::string_view(candidate.pin) == pin;
        });
        if (entry == std::end(kShellPins))
            return;
        const std::string parameterId = std::string("parameter.") + entry->parameter;
        const std::string nodeId = std::string("node.shell.") + entry->parameter;
        AddNode(data, nodeId, entry->parameter, MaterialNodeKind::Parameter, MaterialValueType::Float1, -720.0f, row,
                parameterId);
        row += 90.0f;
        MaterialGraphParameterData parameter{};
        parameter.id = parameterId;
        parameter.displayName = entry->label;
        parameter.defaultValue.type = MaterialValueType::Float1;
        parameter.defaultValue.numeric.x = static_cast<float>(defaults.*member);
        parameter.group = entry->group;
        parameter.useUiRange = true;
        parameter.uiMinimum = entry->uiMinimum;
        parameter.uiMaximum = entry->uiMaximum;
        data.graph.parameters.push_back(std::move(parameter));
        ConnectNodeInput(data, std::string("edge.shell.") + entry->parameter, nodeId, "Value", "node.shell", pin);
    });
    AddStrokeResource(data, "resource.strokeMask", "Stroke Mask", MaterialResourceSemantic::StrokeMask,
                      kBuiltInPainterlyStrokeMaskTextureId);
    AddStrokeResource(data, "resource.strokeNormal", "Stroke Normal", MaterialResourceSemantic::StrokeNormal,
                      kBuiltInPainterlyStrokeNormalTextureId);
}

} // namespace HIKARI::ASSETS::MATERIAL::TEMPLATE_FACTORY_INTERNAL
