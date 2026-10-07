#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::DrawEyeLightingNodeInlineControls(MaterialAssetData& source,
                                                                         MaterialGraphData& graph, float width) {
#if defined(HIKARI_WITH_EDITOR)
    bool capturesPointer = false;
    const auto trackItem = [&capturesPointer]() {
        capturesPointer = capturesPointer || ImGui::IsItemHovered() || ImGui::IsItemActive();
    };
    const auto drawParameter = [&](std::string_view id, const char* label, MaterialValueType type,
                                   const MATH::Vec4& value) {
        MaterialValueData fallback{};
        fallback.type = type;
        fallback.numeric = value;
        capturesPointer |= DrawGraphParameter(source, graph, nullptr, id, label, width, {}, &fallback);
    };

    ImGui::TextDisabled(TEXT::Localize("editor.ui.optional.eye.control.texture.sample.color.control.5dc66f").c_str());
    ImGui::TextDisabled(TEXT::Localize("editor.ui.r.iris.g.cornea.b.catchlight.a.parallax.mask.linear.ec9a4c").c_str());
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.no.control.map.uses.the.editable.radial.iris.mask.below.7dd4cd").c_str());

    MaterialGraphParameterData* eyeLayerMode = FindMaterialGraphParameter(graph, "parameter.eyeLayerMode");
    int layerMode = eyeLayerMode != nullptr && eyeLayerMode->defaultValue.numeric.x >= 0.5f ? 1 : 0;
    const char* kEyeLayerModes[]{
        TEXT::Localize("editor.ui.iris.base.38a41d").c_str(),
        TEXT::Localize("editor.ui.transparent.highlight.7964bc").c_str(),
    };
    ImGui::SetNextItemWidth(width);
    if (ImGui::Combo("##EyeLayerMode", &layerMode, kEyeLayerModes, 2)) {
        MaterialAssetData before = source;
        if (eyeLayerMode == nullptr) {
            MaterialGraphParameterData parameter{};
            parameter.id = "parameter.eyeLayerMode";
            parameter.displayName = "Eye Layer Mode";
            parameter.defaultValue.type = MaterialValueType::Float1;
            parameter.defaultValue.numeric.x = static_cast<float>(layerMode);
            parameter.exposed = true;
            graph.parameters.push_back(std::move(parameter));
        } else {
            eyeLayerMode->defaultValue.numeric.x = static_cast<float>(layerMode);
        }
        if (layerMode == 1) {
            source.surface.alphaMode = MaterialAlphaMode::Blend;
            source.surface.alphaRepresentation = MaterialAlphaRepresentation::Straight;
        }
        authoringDocument_.CommitMaterial(std::move(before), "Change eye layer mode");
    }
    trackItem();

    drawParameter("parameter.irisDepth", "Iris Depth", MaterialValueType::Float1, {0.12f, 0.0f, 0.0f, 0.0f});
    drawParameter("parameter.eyeParallaxScale", "Parallax Scale", MaterialValueType::Float1, {0.10f, 0.0f, 0.0f, 0.0f});
    drawParameter("parameter.irisCenter", "Iris UV Center", MaterialValueType::Float2, {0.50f, 0.50f, 0.0f, 0.0f});
    drawParameter("parameter.irisRadius", "Iris UV Radius", MaterialValueType::Float1, {0.48f, 0.0f, 0.0f, 0.0f});
    drawParameter("parameter.irisMaskSoftness", "Mask Softness", MaterialValueType::Float1, {0.08f, 0.0f, 0.0f, 0.0f});

    if (layerMode == 1) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.use.surface.output.blend.straight.for.png.art.efb36d").c_str());
        drawParameter("parameter.eyeHighlightOpacity", "Highlight Opacity", MaterialValueType::Float1,
                      {1.0f, 0.0f, 0.0f, 0.0f});
        drawParameter("parameter.eyeHighlightThreshold", "Black Threshold", MaterialValueType::Float1,
                      {0.02f, 0.0f, 0.0f, 0.0f});
        drawParameter("parameter.eyeHighlightSoftness", "Highlight Softness", MaterialValueType::Float1,
                      {0.04f, 0.0f, 0.0f, 0.0f});
        drawParameter("parameter.eyeHighlightIntensity", "HDR Intensity", MaterialValueType::Float1,
                      {2.0f, 0.0f, 0.0f, 0.0f});
    } else {
        drawParameter("parameter.eyeSparkle", "Catchlight Intensity", MaterialValueType::Float1,
                      {0.35f, 0.0f, 0.0f, 0.0f});
        drawParameter("parameter.eyeSparklePower", "Catchlight Sharpness", MaterialValueType::Float1,
                      {64.0f, 0.0f, 0.0f, 0.0f});
        drawParameter("parameter.eyeBaseLift", "Iris Light Lift", MaterialValueType::Float1, {0.18f, 0.0f, 0.0f, 0.0f});
        drawParameter("parameter.eyeCausticIntensity", "Caustic Intensity", MaterialValueType::Float1,
                      {0.25f, 0.0f, 0.0f, 0.0f});
        drawParameter("parameter.eyeCausticTint", "Caustic Tint", MaterialValueType::Float3,
                      {1.0f, 0.72f, 0.38f, 0.0f});
        drawParameter("parameter.eyeCausticWidth", "Caustic Width", MaterialValueType::Float1,
                      {0.28f, 0.0f, 0.0f, 0.0f});
    }
    return capturesPointer;
#else
    (void)source;
    (void)graph;
    (void)width;
    return false;
#endif
}

bool ModelMaterialWorkspaceController::DrawCorneaHighlightNodeInlineControls(MaterialAssetData& source,
                                                                             MaterialGraphData& graph, float width) {
#if defined(HIKARI_WITH_EDITOR)
    bool capturesPointer = false;
    const auto trackItem = [&capturesPointer]() {
        capturesPointer = capturesPointer || ImGui::IsItemHovered() || ImGui::IsItemActive();
    };
    const auto drawParameter = [&](std::string_view id, const char* label) {
        capturesPointer |= DrawGraphParameter(source, graph, nullptr, id, label, width);
    };

    ImGui::TextDisabled(TEXT::Localize("editor.ui.uses.this.material.s.base.color.highlight.artwork.3af4d5").c_str());
    ImGui::TextDisabled(TEXT::Localize("editor.ui.connect.color.pbr.eye.surface.output.blend.2e4750").c_str());
    MaterialGraphParameterData* mode = FindMaterialGraphParameter(graph, "parameter.corneaHighlightMode");
    if (mode == nullptr) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.lighting.mode.missing.4cfe8a").c_str());
        return capturesPointer;
    }

    int current = mode->defaultValue.numeric.x < 0.5f ? 0 : mode->defaultValue.numeric.x < 1.5f ? 1 : 2;
    const char* kModes[]{
        TEXT::Localize("editor.ui.painted.fixed.7d22ac").c_str(),
        TEXT::Localize("editor.ui.light.intensity.d44ceb").c_str(),
        TEXT::Localize("editor.ui.light.projection.85c965").c_str(),
    };
    ImGui::SetNextItemWidth(width);
    if (ImGui::Combo("##CorneaHighlightMode", &current, kModes, 3)) {
        MaterialAssetData before = source;
        mode->defaultValue.numeric.x = static_cast<float>(current);
        authoringDocument_.CommitMaterial(std::move(before), "Change cornea highlight mode");
    }
    trackItem();

    if (current == 2) {
        drawParameter("parameter.corneaHighlightTravel", "Travel");
        drawParameter("parameter.corneaHighlightViewInfluence", "View Influence");
        drawParameter("parameter.corneaHighlightLightInfluence", "Light Influence");
        drawParameter("parameter.corneaHighlightCurvature", "Cornea Curvature");
    }
    if (current >= 1) {
        drawParameter("parameter.corneaHighlightMinBrightness", "Minimum Brightness");
        drawParameter("parameter.corneaHighlightLightResponse", "Light Response");
        drawParameter("parameter.corneaHighlightSharpness", "Response Sharpness");
        drawParameter("parameter.corneaHighlightLightColor", "Follow Light Color");
    }
    drawParameter("parameter.corneaHighlightEdgeFade", "Iris Edge Fade");
    return capturesPointer;
#else
    (void)source;
    (void)graph;
    (void)width;
    return false;
#endif
}

} // namespace HIKARI::EDITOR
