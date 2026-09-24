#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Render3D/Vfx/HIKARI_ParticleCpuSimulation.h"

#include <algorithm>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
void VfxWorkspaceController::DrawLayerAnimation(AssetDatabase& database, const VFX::VfxEmitterData& layer) {
#if defined(HIKARI_WITH_EDITOR)
    using namespace RENDER3D::PARTICLES;
    ImGui::SeparatorText(TEXT::Localize("ui.vfx.layer.animation").c_str());
    LayerAnimationSample sample;
    sample.color = layer.color.value.fixed.minimum;
    sample.position = layer.position.value.fixed.minimum;
    sample.rotationRadians = layer.rotation.eulerDegrees.fixed.minimum * (3.14159265f / 180);
    sample.scale = layer.scale.value.fixed.minimum;
    if (preview_.previewProgram_) {
        for (const auto& compiled : preview_.previewProgram_->emitters) {
            if (compiled.sourceEmitterId != layer.id)
                continue;
            const float duration =
                layer.activeDuration > 0 ? layer.activeDuration : document_.Asset().durationSeconds - layer.startDelay;
            sample = VFX::EvaluateVfxLayerAnimation(
                compiled,
                std::clamp(preview_.previewElapsedSeconds_ - layer.startDelay, 0.0f, (std::max)(duration, 0.0f)),
                duration, 0);
            break;
        }
    }
    struct Track {
        const char* name;
        const char* label;
        std::string_view module;
        const char* property;
        VFX::VfxAnimatedValueMode mode;
        MATH::Vec3 value;
    };
    const Track tracks[] = {
        {"position", "ui.vfx.layer.position", layer.position.id, "value", layer.position.value.mode, sample.position},
        {"rotation", "ui.vfx.layer.rotation", layer.rotation.id, "eulerDegrees", layer.rotation.eulerDegrees.mode,
         sample.rotationRadians * (180 / 3.14159265f)},
        {"scale", "ui.vfx.layer.scale", layer.scale.id, "value", layer.scale.value.mode, sample.scale}};
    for (const auto& track : tracks) {
        ImGui::PushID(track.name);
        ImGui::TextUnformatted(TEXT::Localize(track.label).c_str());
        auto value = track.value;
        ImGui::SetNextItemWidth((std::max)(ImGui::GetContentRegionAvail().x - 34, 80.0f));
        const bool changed = ImGui::DragFloat3("##value", &value.x, 0.02f);
        const bool seal = ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        const bool key =
            IconToggleButton(EditorGlyph::Keyframe, "Keyframe", track.mode != VFX::VfxAnimatedValueMode::Fixed,
                             {26, ImGui::GetFrameHeight()}, TEXT::Localize("ui.vfx.layer.key").c_str());
        const auto mergeGroup = changed ? VfxInspectorPropertyEditor::MergeGroup(layer.id, track.name) : 0;
        if (key || (changed && (timelineEdit_.autoKey || track.mode != VFX::VfxAnimatedValueMode::Fixed))) {
            (void)SetLayerTransformKey(layer.id, track.name, preview_.previewElapsedSeconds_, value, statusMessage_,
                                       mergeGroup);
        } else if (changed) {
            (void)SetProperty(track.module, std::string(track.property) + ".fixed",
                              {{"mode", "Constant"},
                               {"minimum", {value.x, value.y, value.z}},
                               {"maximum", {value.x, value.y, value.z}},
                               {"randomGroup", 0}},
                              statusMessage_, mergeGroup, &database);
        }
        if (seal)
            SealPropertyEdit();
        ImGui::PopID();
    }
    if (layer.kind == VFX::VfxLayerKind::Adjustment)
        return;
    auto color = sample.color;
    ImGui::PushID("layerColor");
    ImGui::TextUnformatted(VfxUiText("Color").c_str());
    ImGui::SetNextItemWidth((std::max)(ImGui::GetContentRegionAvail().x - 34, 80.0f));
    const bool changed = ImGui::ColorEdit4("##value", &color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
    const bool seal = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    const bool key =
        IconToggleButton(EditorGlyph::Keyframe, "Keyframe", layer.color.value.mode != VFX::VfxAnimatedColorMode::Fixed,
                         {26, ImGui::GetFrameHeight()}, TEXT::Localize("ui.vfx.layer.key").c_str());
    if (key || (changed && (timelineEdit_.autoKey || layer.color.value.mode != VFX::VfxAnimatedColorMode::Fixed)))
        (void)SetLayerColorKey(layer.id, preview_.previewElapsedSeconds_, color, statusMessage_,
                               changed ? VfxInspectorPropertyEditor::MergeGroup(layer.id, "color") : 0);
    else if (changed)
        (void)SetProperty(layer.color.id, "value.fixed",
                          {{"mode", "Constant"},
                           {"minimum", {color.x, color.y, color.z, color.w}},
                           {"maximum", {color.x, color.y, color.z, color.w}},
                           {"randomGroup", 0}},
                          statusMessage_, VfxInspectorPropertyEditor::MergeGroup(layer.id, "color"));
    if (seal)
        SealPropertyEdit();
    ImGui::PopID();
    (void)IconButton(EditorGlyph::Help, "KeyHelp", EditorButtonTone::Quiet, {26, ImGui::GetFrameHeight()},
                     TEXT::Localize("ui.vfx.layer.keyHelp").c_str());
    DrawLayerPath(layer);
#else
    (void)database;
    (void)layer;
#endif
}
} // namespace HIKARI::EDITOR
