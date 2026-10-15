#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
struct CurveChoice {
    std::string moduleId{};
    std::string propertyId{};
    std::string label{};
    std::array<std::string_view, 4u> channels{};
    size_t channelCount = 0u;
};

ImU32 ToColor(const MATH::Vec4& color) { return ImGui::ColorConvertFloat4ToU32({color.x, color.y, color.z, color.w}); }

std::vector<CurveChoice> CollectCurveChoices(const VFX::VfxEmitterData& emitter) {
    std::vector<CurveChoice> choices{};
    for (const auto& track : CollectVfxTimelineCurves(emitter)) {
        auto found = std::find_if(choices.begin(), choices.end(), [&](const auto& c) {
            return c.moduleId == track.address.moduleId && c.propertyId == track.address.propertyId;
        });
        const std::string_view channels[]{"x", "y", "z", "r", "g", "b", "a"};
        const auto channel =
            *std::find_if(std::begin(channels), std::end(channels), [&](auto c) { return c == track.address.channel; });
        if (found == choices.end())
            choices.push_back({track.address.moduleId, track.address.propertyId, VfxUiText(track.label), {channel}, 1});
        else if (found->channelCount < 4)
            found->channels[found->channelCount++] = channel;
    }
    return choices;
}

void DrawGradient(VfxWorkspaceController& controller, const VFX::VfxEmitterData& emitter,
                  const VFX::VfxColorGradientData& gradient, std::string& selectedStopId, std::string& statusMessage) {
    ImGui::SeparatorText(VfxUiText("Color Gradient").c_str());
    const ImVec2 size{(std::max)(ImGui::GetContentRegionAvail().x, 220.0f), 58.0f};
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    if (!gradient.stops.empty()) {
        drawList->AddRectFilled(origin, {origin.x + gradient.stops.front().time * size.x, origin.y + size.y},
                                ToColor(gradient.stops.front().color));
        for (size_t index = 1u; index < gradient.stops.size(); ++index) {
            const auto& left = gradient.stops[index - 1u];
            const auto& right = gradient.stops[index];
            drawList->AddRectFilledMultiColor({origin.x + left.time * size.x, origin.y},
                                              {origin.x + right.time * size.x, origin.y + size.y}, ToColor(left.color),
                                              ToColor(right.color), ToColor(right.color), ToColor(left.color));
        }
        drawList->AddRectFilled({origin.x + gradient.stops.back().time * size.x, origin.y},
                                {origin.x + size.x, origin.y + size.y}, ToColor(gradient.stops.back().color));
    }
    drawList->AddRect(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(113, 128, 147, 180));
    for (const VFX::VfxColorStopData& stop : gradient.stops) {
        const float x = origin.x + stop.time * size.x;
        drawList->AddTriangleFilled(
            {x, origin.y + size.y + 2.0f}, {x - 6.0f, origin.y + size.y + 12.0f}, {x + 6.0f, origin.y + size.y + 12.0f},
            selectedStopId == stop.id ? IM_COL32(255, 224, 110, 255) : IM_COL32(218, 226, 238, 255));
    }
    ImGui::InvisibleButton("##VfxColorGradient", {size.x, size.y + 16.0f});
    if (ImGui::IsItemClicked() && !gradient.stops.empty()) {
        const float normalized = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
        float nearestDistance = (std::numeric_limits<float>::max)();
        for (const VFX::VfxColorStopData& stop : gradient.stops) {
            const float distance = std::abs(stop.time - normalized);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                selectedStopId = stop.id;
            }
        }
        controller.Select({
            VfxWorkspaceSelectionKind::ColorStop,
            selectedStopId,
            emitter.id,
            emitter.color.id,
            "value",
        });
    }
    if (ImGui::Button(VfxUiText("+ Stop").c_str())) {
        std::string id{};
        (void)controller.AddColorStop(emitter.id, emitter.color.id, "value", id, statusMessage);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedStopId.empty());
    if (ImGui::Button(VfxUiText("Delete Stop").c_str())) {
        (void)controller.RemoveColorStop(emitter.id, emitter.color.id, "value", selectedStopId, statusMessage);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(TEXT::Localize("editor.ui.zu.u.stops.76ecd3").c_str(), gradient.stops.size(),
                        RENDER3D::PARTICLES::kMaxParticleColorStops);
}
} // namespace

void VfxWorkspaceController::DrawCurveDiagnosticsWindow() {
    if (!ImGui::Begin((VfxUiText("Timing & Curves") + "###Vfx/CurvesDiagnostics").c_str())) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("VfxBottomTabs")) {
        if (ImGui::BeginTabItem(VfxUiText("Timeline").c_str())) {
            DrawTimeline();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(VfxUiText("Curves").c_str())) {
            const VFX::VfxEmitterData* emitter = FindEmitter(selection_.emitterId);
            if (emitter == nullptr && document_.IsOpen() && !document_.Asset().emitters.empty()) {
                emitter = &document_.Asset().emitters.front();
            }
            if (emitter != nullptr) {
                DrawCurveEditor(*emitter);
            } else {
                ImGui::TextDisabled(VfxUiText("Select or create an emitter to edit curves.").c_str());
            }
            ImGui::EndTabItem();
        }
        const std::string diagnosticsLabel =
            VfxUiText("Diagnostics").append(" (") + std::to_string(preview_.diagnostics_.size()) + ")";
        if (ImGui::BeginTabItem(diagnosticsLabel.c_str())) {
            DrawDiagnostics();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void VfxWorkspaceController::DrawCurveEditor(const VFX::VfxEmitterData& source) {
    const VFX::VfxEmitterData emitter = source;
    ImGui::TextDisabled(TEXT::Localize("editor.ui.emitter.s.6d1333").c_str(), emitter.name.c_str());
    const std::vector<CurveChoice> choices = CollectCurveChoices(emitter);
    if (selection_.kind == VfxWorkspaceSelectionKind::CurveKey && selection_.emitterId == emitter.id) {
        curveEditorModuleId_ = selection_.moduleId;
        curveEditorPropertyId_ = selection_.propertyId;
        curveEditorChannel_ = selection_.channel;
    }
    const auto selectedChoice = std::find_if(choices.begin(), choices.end(), [this](const CurveChoice& choice) {
        return choice.moduleId == curveEditorModuleId_ && choice.propertyId == curveEditorPropertyId_;
    });
    if (selectedChoice == choices.end() && !choices.empty()) {
        curveEditorModuleId_ = choices.front().moduleId;
        curveEditorPropertyId_ = choices.front().propertyId;
        curveEditorChannel_ = choices.front().channels.front();
    }
    const auto activeChoice = std::find_if(choices.begin(), choices.end(), [this](const CurveChoice& choice) {
        return choice.moduleId == curveEditorModuleId_ && choice.propertyId == curveEditorPropertyId_;
    });

    if (!choices.empty()) {
        const char* preview = activeChoice != choices.end() ? activeChoice->label.c_str() : "Select Curve";
        if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.property.9ae33a", "Property").c_str(), preview)) {
            for (const CurveChoice& choice : choices) {
                if (ImGui::Selectable(choice.label.c_str(), activeChoice != choices.end() &&
                                                                choice.moduleId == activeChoice->moduleId &&
                                                                choice.propertyId == activeChoice->propertyId)) {
                    curveEditorModuleId_ = choice.moduleId;
                    curveEditorPropertyId_ = choice.propertyId;
                    curveEditorChannel_ = choice.channels.front();
                    selectedCurveKeyId_.clear();
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled(
            VfxUiText("Set Position, Rotation, Scale or Color mode to Curve in the Inspector.").c_str());
    }

    if (activeChoice != choices.end()) {
        if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.channel.879f0b", "Channel").c_str(),
                              curveEditorChannel_.c_str())) {
            for (size_t index = 0u; index < activeChoice->channelCount; ++index) {
                const std::string_view channel = activeChoice->channels[index];
                if (ImGui::Selectable(channel.data(), channel == curveEditorChannel_)) {
                    curveEditorChannel_ = channel;
                    selectedCurveKeyId_.clear();
                }
            }
            ImGui::EndCombo();
        }
        const VfxCurveAddress address{
            emitter.id,
            activeChoice->moduleId,
            activeChoice->propertyId,
            curveEditorChannel_,
        };
        if (const VFX::VfxCurveData* curve = ResolveVfxCurve(emitter, address)) {
            const std::string curvePath =
                address.propertyId == "curve" ? "curve" : address.propertyId + ".curve." + address.channel;
            if (ImGui::BeginCombo(VfxUiText("Interpolation").c_str(), VFX::ToString(curve->interpolation).data())) {
                for (const char* mode : {"Linear", "Smooth", "Step"})
                    if (ImGui::Selectable(VfxUiText(mode).c_str()))
                        (void)SetProperty(address.moduleId, curvePath + ".interpolation", mode, statusMessage_);
                ImGui::EndCombo();
            }
            ImGui::TextDisabled(
                "%s", TEXT::Localize(emitter.kind == VFX::VfxLayerKind::Particles && address.propertyId != "curve"
                                         ? "ui.vfx.timeline.particleTime"
                                         : "ui.vfx.timeline.compositionTime")
                          .c_str());
            const ImVec2 size{(std::max)(ImGui::GetContentRegionAvail().x, 240.0f), 172.0f};
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(origin, {origin.x + size.x, origin.y + size.y}, IM_COL32(13, 17, 23, 255));
            for (int line = 1; line < 4; ++line) {
                const float x = origin.x + size.x * line / 4.0f;
                const float y = origin.y + size.y * line / 4.0f;
                drawList->AddLine({x, origin.y}, {x, origin.y + size.y}, IM_COL32(68, 78, 91, 80));
                drawList->AddLine({origin.x, y}, {origin.x + size.x, y}, IM_COL32(68, 78, 91, 80));
            }
            float minimum = 0.0f;
            float maximum = 1.0f;
            for (const auto& key : curve->keys) {
                minimum = (std::min)(minimum, key.value);
                maximum = (std::max)(maximum, key.value);
            }
            if (maximum - minimum < 0.001f)
                maximum = minimum + 1.0f;
            if (timelineEdit_.curveDragging) {
                minimum = timelineEdit_.curveMinimum;
                maximum = timelineEdit_.curveMaximum;
            }
            const auto point = [&](float time, float value) {
                return ImVec2{
                    origin.x + time * size.x,
                    origin.y + size.y - (value - minimum) / (maximum - minimum) * size.y,
                };
            };
            std::vector<RENDER3D::PARTICLES::ParticleCurveKey> samples;
            for (const auto& key : curve->keys)
                samples.push_back({key.time, key.value, key.inTangent, key.outTangent});
            std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.time < b.time; });
            ImVec2 previous{};
            for (int i = 0; i <= 192; ++i) {
                const float time = float(i) / 192;
                const auto current =
                    point(time, RENDER3D::PARTICLES::EvaluateParticleCurve(
                                    samples,
                                    static_cast<RENDER3D::PARTICLES::ParticleCurveInterpolation>(curve->interpolation),
                                    time));
                if (i)
                    drawList->AddLine(previous, current, IM_COL32(91, 205, 255, 255), 2);
                previous = current;
            }
            for (const auto& key : curve->keys) {
                drawList->AddCircleFilled(point(key.time, key.value), selectedCurveKeyId_ == key.id ? 6.0f : 4.0f,
                                          selectedCurveKeyId_ == key.id ? IM_COL32(255, 224, 110, 255)
                                                                        : IM_COL32(91, 205, 255, 255));
            }
            ImGui::InvisibleButton("##VfxCurve", size);
            if (ImGui::IsItemClicked() && !timelineEdit_.baseline && !timelineEdit_.suppressDragUntilRelease) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                float nearestDistance = 12;
                selectedCurveKeyId_.clear();
                for (const auto& key : curve->keys) {
                    const auto p = point(key.time, key.value);
                    const float distance = std::hypot(mouse.x - p.x, mouse.y - p.y);
                    if (distance < nearestDistance) {
                        nearestDistance = distance;
                        selectedCurveKeyId_ = key.id;
                    }
                }
                if (!selectedCurveKeyId_.empty()) {
                    Select({VfxWorkspaceSelectionKind::CurveKey, selectedCurveKeyId_, emitter.id, address.moduleId,
                            address.propertyId, address.channel});
                    BeginTimelineEdit();
                    timelineEdit_.curveDragging = true;
                    timelineEdit_.curveMinimum = minimum;
                    timelineEdit_.curveMaximum = maximum;
                }
            }
            if (timelineEdit_.curveDragging && timelineEdit_.baseline) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    EndTimelineEdit(true);
                    timelineEdit_.suppressDragUntilRelease = true;
                } else if (!ImGui::IsMouseDown(0))
                    EndTimelineEdit(false);
                else if (ImGui::IsMouseDragging(0)) {
                    auto next = *timelineEdit_.baseline;
                    auto* target = FindEmitterMutable(next, emitter.id);
                    auto* edited = target ? ResolveVfxCurve(*target, address) : nullptr;
                    if (edited) {
                        auto found = std::find_if(edited->keys.begin(), edited->keys.end(),
                                                  [&](const auto& k) { return k.id == selectedCurveKeyId_; });
                        if (found != edited->keys.end()) {
                            float lo = 0, hi = 1;
                            for (const auto& neighbor : edited->keys)
                                if (neighbor.id != found->id) {
                                    if (neighbor.time < found->time)
                                        lo = (std::max)(lo, neighbor.time + 0.0001f);
                                    else
                                        hi = (std::min)(hi, neighbor.time - 0.0001f);
                                }
                            float time = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
                            const bool particleTime =
                                emitter.kind == VFX::VfxLayerKind::Particles && address.propertyId != "curve";
                            const float span = emitter.activeDuration > 0 ? emitter.activeDuration
                                                                          : next.durationSeconds - emitter.startDelay;
                            if (timelineEdit_.snap && !particleTime && !ImGui::GetIO().KeyShift) {
                                float seconds = emitter.startDelay + emitter.animationTiming.LayerSeconds(time, span);
                                seconds = std::round(seconds * 60) / 60;
                                time = emitter.animationTiming.Normalized(seconds - emitter.startDelay, span);
                            }
                            found->time = std::clamp(time, lo, hi);
                            found->value =
                                minimum + (1 - (ImGui::GetIO().MousePos.y - origin.y) / size.y) * (maximum - minimum);
                            if (address.propertyId == "curve")
                                for (const auto& track : emitter.parameterTracks)
                                    if (track.id == address.moduleId)
                                        for (const auto& p : VFX::GetVfxAnimatableParameters(emitter))
                                            if (p.targetId == track.targetId && p.propertyId == track.propertyId)
                                                found->value = std::clamp(found->value, p.minimum, p.maximum);
                            if (address.moduleId == emitter.color.id && address.channel == "a")
                                found->value = std::clamp(found->value, 0.0f, 1.0f);
                            std::sort(edited->keys.begin(), edited->keys.end(),
                                      [](const auto& a, const auto& b) { return a.time < b.time; });
                            (void)ApplyAsset(std::move(next), statusMessage_, VfxTimelineEditState::mergeGroup);
                        }
                    }
                }
            }
            if (!ImGui::IsMouseDown(0))
                timelineEdit_.suppressDragUntilRelease = false;
            if (ImGui::Button(VfxUiText("+ Key").c_str())) {
                std::string id{};
                (void)AddCurveKey(emitter.id, address.moduleId, address.propertyId, address.channel, id,
                                  statusMessage_);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(selectedCurveKeyId_.empty());
            if (ImGui::Button(VfxUiText("Delete Key").c_str())) {
                (void)RemoveCurveKey(emitter.id, address.moduleId, address.propertyId, address.channel,
                                     selectedCurveKeyId_, statusMessage_);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled(TEXT::Localize("editor.ui.zu.u.keys.s.ff3ddd").c_str(), curve->keys.size(),
                                emitter.kind == VFX::VfxLayerKind::Particles && address.propertyId != "curve"
                                    ? RENDER3D::PARTICLES::kMaxParticleCurveKeys
                                    : VFX::kMaxVfxLayerCurveKeys,
                                VFX::ToString(curve->interpolation).data());
        }
    }
    if (emitter.color.value.mode == VFX::VfxAnimatedColorMode::Gradient) {
        DrawGradient(*this, emitter, emitter.color.value.gradient, selectedColorStopId_, statusMessage_);
    }
}

} // namespace HIKARI::EDITOR
