#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include <algorithm>
#include <cmath>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
bool VfxWorkspaceController::SetLayerParameterKey(std::string_view layerId, std::string_view targetId,
                                                  std::string_view propertyId, float seconds, float value,
                                                  std::string &message, uint64_t mergeGroup) {
    auto next = document_.Asset();
    auto *layer = FindEmitterMutable(next, layerId);
    if (!layer || !std::isfinite(seconds) || !std::isfinite(value)) {
        message = "Invalid animated parameter";
        return false;
    }
    const auto parameters = VFX::GetVfxAnimatableParameters(*layer);
    const auto p = std::find_if(parameters.begin(), parameters.end(), [&](const auto &p) {
        return p.targetId == targetId && p.propertyId == propertyId;
    });
    const float span =
        layer->activeDuration > 0 ? layer->activeDuration : next.durationSeconds - layer->startDelay;
    float time = 0;
    if (!ResolveVfxLayerKeyTime(next, *layer, seconds, time, message))
        return false;
    if (p == parameters.end() || value < p->minimum || value > p->maximum || span <= 0 ||
        seconds < layer->startDelay || seconds > layer->startDelay + span) {
        message = "Parameter key is outside its time/value range";
        return false;
    }
    auto track =
        std::find_if(layer->parameterTracks.begin(), layer->parameterTracks.end(),
                     [&](const auto &t) { return t.targetId == targetId && t.propertyId == propertyId; });
    if (track == layer->parameterTracks.end()) {
        VFX::VfxParameterTrackData created;
        created.id = AllocateStableId("parameter.track");
        created.targetId = targetId;
        created.propertyId = propertyId;
        created.curve = VFX::MakeVfxConstantCurve(created.id, p->value);
        layer->parameterTracks.push_back(std::move(created));
        track = std::prev(layer->parameterTracks.end());
    }
    auto key = std::find_if(track->curve.keys.begin(), track->curve.keys.end(),
                            [&](const auto &k) { return std::abs(k.time - time) < 0.0001f; });
    if (key != track->curve.keys.end())
        key->value = value;
    else {
        if (track->curve.keys.size() >= VFX::kMaxVfxLayerCurveKeys) {
            message = "Layer key budget reached";
            return false;
        }
        track->curve.keys.push_back({AllocateStableId("parameter.key"), time, value});
        std::sort(track->curve.keys.begin(), track->curve.keys.end(),
                  [](const auto &a, const auto &b) { return a.time < b.time; });
    }
    const auto trackId = track->id;
    if (!ApplyAsset(std::move(next), message, mergeGroup))
        return false;
    curveEditorModuleId_ = trackId;
    curveEditorPropertyId_ = "curve";
    curveEditorChannel_ = "x";
    return true;
}
bool VfxWorkspaceController::RemoveLayerParameterTrack(std::string_view layerId, std::string_view trackId,
                                                       std::string &message) {
    auto next = document_.Asset();
    auto *layer = FindEmitterMutable(next, layerId);
    if (!layer || !std::erase_if(layer->parameterTracks, [&](const auto &t) { return t.id == trackId; })) {
        message = "Parameter track not found";
        return false;
    }
    return ApplyAsset(std::move(next), message);
}
bool VfxWorkspaceController::DrawLayerParameter(const VFX::VfxEmitterData &layer, std::string_view targetId,
                                                std::string_view propertyId) {
#if defined(HIKARI_WITH_EDITOR)
    const auto parameters = VFX::GetVfxAnimatableParameters(layer);
    const auto p = std::find_if(parameters.begin(), parameters.end(), [&](const auto &p) {
        return p.targetId == targetId && p.propertyId == propertyId;
    });
    if (p == parameters.end())
        return false;
    const auto track =
        std::find_if(layer.parameterTracks.begin(), layer.parameterTracks.end(),
                     [&](const auto &t) { return t.targetId == targetId && t.propertyId == propertyId; });
    float value = p->value;
    const float span = layer.activeDuration > 0 ? layer.activeDuration
                                                : document_.Asset().durationSeconds - layer.startDelay;
    const float seconds = std::clamp(previewElapsedSeconds_, layer.startDelay, layer.startDelay + span);
    if (track != layer.parameterTracks.end()) {
        std::vector<RENDER3D::PARTICLES::ParticleCurveKey> keys;
        for (const auto &k : track->curve.keys)
            keys.push_back({k.time, k.value, k.inTangent, k.outTangent});
        std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
        value = RENDER3D::PARTICLES::EvaluateParticleCurve(
            keys, static_cast<RENDER3D::PARTICLES::ParticleCurveInterpolation>(track->curve.interpolation),
            layer.animationTiming.Normalized(seconds - layer.startDelay, span));
    }
    const auto identity = std::string(targetId) + "." + std::string(propertyId);
    ImGui::PushID(identity.c_str());
    ImGui::TextUnformatted(VfxUiText(p->label).c_str());
    ImGui::SetNextItemWidth((std::max)(60.0f, ImGui::GetContentRegionAvail().x - 74));
    const bool changed = ImGui::DragFloat("##value", &value, 0.01f, p->minimum, p->maximum);
    const bool seal = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    const bool add = ImGui::Button(TEXT::Localize("ui.vfx.layer.key").c_str());
    if (ImGui::BeginPopupContextItem()) {
        if (track != layer.parameterTracks.end() &&
            ImGui::MenuItem(TEXT::Localize("ui.vfx.timeline.removeTrack").c_str()))
            (void)RemoveLayerParameterTrack(layer.id, track->id, statusMessage_);
        ImGui::EndPopup();
    }
    if (add || (changed &&
                (timelineEdit_.autoKey || track != layer.parameterTracks.end() || propertyId == "progress")))
        (void)SetLayerParameterKey(layer.id, targetId, propertyId, seconds, value, statusMessage_,
                                   changed ? VfxInspectorPropertyEditor::MergeGroup(targetId, propertyId)
                                           : 0);
    else if (changed) {
        if (targetId == layer.renderShape.id)
            (void)SetProperty(targetId, propertyId, value, statusMessage_,
                              VfxInspectorPropertyEditor::MergeGroup(targetId, propertyId));
        else {
            const auto effect = std::find_if(layer.effects.begin(), layer.effects.end(),
                                             [&](const auto &e) { return e.id == targetId; });
            if (effect != layer.effects.end()) {
                auto v = effect->parameters;
                float *components[]{&v.x, &v.y, &v.z, &v.w};
                const auto c = std::string_view("xyzw").find(propertyId.back());
                if (c < 4)
                    *components[c] = value;
                (void)SetProperty(targetId, "parameters", {v.x, v.y, v.z, v.w}, statusMessage_,
                                  VfxInspectorPropertyEditor::MergeGroup(targetId, propertyId));
            }
        }
    }
    if (seal)
        SealPropertyEdit();
    if (track != layer.parameterTracks.end())
        ImGui::TextDisabled("%s", TEXT::Localize("ui.vfx.timeline.compositionTime").c_str());
    ImGui::PopID();
    return true;
#else
    (void)layer;
    (void)targetId;
    (void)propertyId;
    return false;
#endif
}
} // namespace HIKARI::EDITOR
