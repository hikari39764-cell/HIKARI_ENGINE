#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        struct CurveChoice {
            std::string moduleId{};
            std::string propertyId{};
            std::string label{};
            std::array<std::string_view, 4u> channels{};
            size_t channelCount = 0u;
        };

        ImU32 ToColor(const MATH::Vec4& color) {
            return ImGui::ColorConvertFloat4ToU32({
                color.x, color.y, color.z, color.w
            });
        }

        std::vector<CurveChoice> CollectCurveChoices(
            const VFX::VfxEmitterData& emitter) {

            std::vector<CurveChoice> choices{};
            constexpr std::array<std::string_view, 4u> xyz{
                "x", "y", "z", ""
            };
            constexpr std::array<std::string_view, 4u> rgba{
                "r", "g", "b", "a"
            };
            if (emitter.position.value.mode ==
                VFX::VfxAnimatedValueMode::Curve) {
                choices.push_back({
                    emitter.position.id, "value", "Position / Value",
                    xyz, 3u
                });
            }
            if (emitter.rotation.eulerDegrees.mode ==
                VFX::VfxAnimatedValueMode::Curve) {
                choices.push_back({
                    emitter.rotation.id, "eulerDegrees",
                    "Rotation / Euler Degrees", xyz, 3u
                });
            }
            if (emitter.scale.value.mode ==
                VFX::VfxAnimatedValueMode::Curve) {
                choices.push_back({
                    emitter.scale.id, "value", "Scale / Value",
                    xyz, 3u
                });
            }
            if (emitter.color.value.mode ==
                VFX::VfxAnimatedColorMode::Curve) {
                choices.push_back({
                    emitter.color.id, "value", "Color / Value",
                    rgba, 4u
                });
            }
            return choices;
        }

        void DrawGradient(
            VfxWorkspaceController& controller,
            const VFX::VfxEmitterData& emitter,
            const VFX::VfxColorGradientData& gradient,
            std::string& selectedStopId,
            std::string& statusMessage) {

            ImGui::SeparatorText("Color Gradient");
            const ImVec2 size{
                (std::max)(ImGui::GetContentRegionAvail().x, 220.0f),
                58.0f
            };
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (!gradient.stops.empty()) {
                drawList->AddRectFilled(
                    origin,
                    { origin.x + gradient.stops.front().time * size.x,
                      origin.y + size.y },
                    ToColor(gradient.stops.front().color));
                for (size_t index = 1u;
                     index < gradient.stops.size();
                     ++index) {
                    const auto& left = gradient.stops[index - 1u];
                    const auto& right = gradient.stops[index];
                    drawList->AddRectFilledMultiColor(
                        { origin.x + left.time * size.x, origin.y },
                        { origin.x + right.time * size.x,
                          origin.y + size.y },
                        ToColor(left.color),
                        ToColor(right.color),
                        ToColor(right.color),
                        ToColor(left.color));
                }
                drawList->AddRectFilled(
                    { origin.x + gradient.stops.back().time * size.x,
                      origin.y },
                    { origin.x + size.x, origin.y + size.y },
                    ToColor(gradient.stops.back().color));
            }
            drawList->AddRect(
                origin,
                { origin.x + size.x, origin.y + size.y },
                IM_COL32(113, 128, 147, 180));
            for (const VFX::VfxColorStopData& stop : gradient.stops) {
                const float x = origin.x + stop.time * size.x;
                drawList->AddTriangleFilled(
                    { x, origin.y + size.y + 2.0f },
                    { x - 6.0f, origin.y + size.y + 12.0f },
                    { x + 6.0f, origin.y + size.y + 12.0f },
                    selectedStopId == stop.id
                        ? IM_COL32(255, 224, 110, 255)
                        : IM_COL32(218, 226, 238, 255));
            }
            ImGui::InvisibleButton(
                "##VfxColorGradient", { size.x, size.y + 16.0f });
            if (ImGui::IsItemClicked() && !gradient.stops.empty()) {
                const float normalized = std::clamp(
                    (ImGui::GetIO().MousePos.x - origin.x) / size.x,
                    0.0f,
                    1.0f);
                float nearestDistance =
                    (std::numeric_limits<float>::max)();
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
            if (ImGui::Button("+ Stop")) {
                std::string id{};
                (void)controller.AddColorStop(
                    emitter.id,
                    emitter.color.id,
                    "value",
                    id,
                    statusMessage);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(selectedStopId.empty());
            if (ImGui::Button("Delete Stop")) {
                (void)controller.RemoveColorStop(
                    emitter.id,
                    emitter.color.id,
                    "value",
                    selectedStopId,
                    statusMessage);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%zu / %u stops",
                gradient.stops.size(),
                RENDER3D::PARTICLES::kMaxParticleColorStops);
        }
#endif
    } // namespace

    void VfxWorkspaceController::DrawCurveDiagnosticsWindow() {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin(
                "Timeline & Curves###Vfx/CurvesDiagnostics")) {
            ImGui::End();
            return;
        }
        if (ImGui::BeginTabBar("VfxBottomTabs")) {
            if (ImGui::BeginTabItem("Timeline")) {
                DrawTimeline();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Curves")) {
                const VFX::VfxEmitterData* emitter =
                    FindEmitter(selection_.emitterId);
                if (emitter == nullptr && document_.IsOpen() &&
                    !document_.Asset().emitters.empty()) {
                    emitter = &document_.Asset().emitters.front();
                }
                if (emitter != nullptr) {
                    DrawCurveEditor(*emitter);
                } else {
                    ImGui::TextDisabled(
                        "Select or create an emitter to edit curves.");
                }
                ImGui::EndTabItem();
            }
            const std::string diagnosticsLabel = "Diagnostics (" +
                std::to_string(diagnostics_.size()) + ")";
            if (ImGui::BeginTabItem(diagnosticsLabel.c_str())) {
                DrawDiagnostics();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
#endif
    }

    void VfxWorkspaceController::DrawCurveEditor(
        const VFX::VfxEmitterData& source) {
#if defined(HIKARI_WITH_EDITOR)
        const VFX::VfxEmitterData emitter = source;
        ImGui::TextDisabled("Emitter: %s", emitter.name.c_str());
        const std::vector<CurveChoice> choices =
            CollectCurveChoices(emitter);
        if (selection_.kind == VfxWorkspaceSelectionKind::CurveKey &&
            selection_.emitterId == emitter.id) {
            curveEditorModuleId_ = selection_.moduleId;
            curveEditorPropertyId_ = selection_.propertyId;
            curveEditorChannel_ = selection_.channel;
        }
        const auto selectedChoice = std::find_if(
            choices.begin(), choices.end(),
            [this](const CurveChoice& choice) {
                return choice.moduleId == curveEditorModuleId_ &&
                    choice.propertyId == curveEditorPropertyId_;
            });
        if (selectedChoice == choices.end() && !choices.empty()) {
            curveEditorModuleId_ = choices.front().moduleId;
            curveEditorPropertyId_ = choices.front().propertyId;
            curveEditorChannel_ = choices.front().channels.front();
        }
        const auto activeChoice = std::find_if(
            choices.begin(), choices.end(),
            [this](const CurveChoice& choice) {
                return choice.moduleId == curveEditorModuleId_ &&
                    choice.propertyId == curveEditorPropertyId_;
            });

        if (!choices.empty()) {
            const char* preview = activeChoice != choices.end()
                ? activeChoice->label.c_str()
                : "Select Curve";
            if (ImGui::BeginCombo("Property", preview)) {
                for (const CurveChoice& choice : choices) {
                    if (ImGui::Selectable(
                            choice.label.c_str(),
                            activeChoice != choices.end() &&
                                choice.moduleId ==
                                    activeChoice->moduleId &&
                                choice.propertyId ==
                                    activeChoice->propertyId)) {
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
                "Set Position, Rotation, Scale or Color mode to Curve in the Inspector.");
        }

        if (activeChoice != choices.end()) {
            if (ImGui::BeginCombo(
                    "Channel", curveEditorChannel_.c_str())) {
                for (size_t index = 0u;
                     index < activeChoice->channelCount;
                     ++index) {
                    const std::string_view channel =
                        activeChoice->channels[index];
                    if (ImGui::Selectable(
                            channel.data(), channel == curveEditorChannel_)) {
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
            if (const VFX::VfxCurveData* curve =
                    ResolveVfxCurve(emitter, address)) {
                const ImVec2 size{
                    (std::max)(ImGui::GetContentRegionAvail().x, 240.0f),
                    172.0f
                };
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(
                    origin,
                    { origin.x + size.x, origin.y + size.y },
                    IM_COL32(13, 17, 23, 255));
                for (int line = 1; line < 4; ++line) {
                    const float x = origin.x + size.x * line / 4.0f;
                    const float y = origin.y + size.y * line / 4.0f;
                    drawList->AddLine(
                        { x, origin.y }, { x, origin.y + size.y },
                        IM_COL32(68, 78, 91, 80));
                    drawList->AddLine(
                        { origin.x, y }, { origin.x + size.x, y },
                        IM_COL32(68, 78, 91, 80));
                }
                float minimum = 0.0f;
                float maximum = 1.0f;
                for (const auto& key : curve->keys) {
                    minimum = (std::min)(minimum, key.value);
                    maximum = (std::max)(maximum, key.value);
                }
                if (maximum - minimum < 0.001f) maximum = minimum + 1.0f;
                const auto point = [&](float time, float value) {
                    return ImVec2{
                        origin.x + time * size.x,
                        origin.y + size.y -
                            (value - minimum) /
                                (maximum - minimum) * size.y,
                    };
                };
                for (size_t index = 1u;
                     index < curve->keys.size();
                     ++index) {
                    const auto& previous = curve->keys[index - 1u];
                    const auto& next = curve->keys[index];
                    ImVec2 previousPoint = point(
                        previous.time, previous.value);
                    constexpr int kSegments = 24;
                    for (int segment = 1;
                         segment <= kSegments;
                         ++segment) {
                        const float linear =
                            static_cast<float>(segment) / kSegments;
                        const float amount =
                            curve->interpolation ==
                                    VFX::VfxCurveInterpolation::Step
                                ? 0.0f
                                : curve->interpolation ==
                                        VFX::VfxCurveInterpolation::Smooth
                                    ? linear * linear *
                                        (3.0f - 2.0f * linear)
                                    : linear;
                        const ImVec2 current = point(
                            previous.time +
                                (next.time - previous.time) * linear,
                            previous.value +
                                (next.value - previous.value) * amount);
                        drawList->AddLine(
                            previousPoint,
                            current,
                            IM_COL32(91, 205, 255, 255),
                            2.0f);
                        previousPoint = current;
                    }
                }
                for (const auto& key : curve->keys) {
                    drawList->AddCircleFilled(
                        point(key.time, key.value),
                        selectedCurveKeyId_ == key.id ? 6.0f : 4.0f,
                        selectedCurveKeyId_ == key.id
                            ? IM_COL32(255, 224, 110, 255)
                            : IM_COL32(91, 205, 255, 255));
                }
                ImGui::InvisibleButton("##VfxCurve", size);
                if (ImGui::IsItemClicked()) {
                    const ImVec2 mouse = ImGui::GetIO().MousePos;
                    float nearestDistance = 12.0f;
                    for (const auto& key : curve->keys) {
                        const ImVec2 position = point(key.time, key.value);
                        const float dx = mouse.x - position.x;
                        const float dy = mouse.y - position.y;
                        const float distance = std::sqrt(dx * dx + dy * dy);
                        if (distance < nearestDistance) {
                            nearestDistance = distance;
                            selectedCurveKeyId_ = key.id;
                        }
                    }
                    if (!selectedCurveKeyId_.empty()) {
                        Select({
                            VfxWorkspaceSelectionKind::CurveKey,
                            selectedCurveKeyId_,
                            emitter.id,
                            address.moduleId,
                            address.propertyId,
                            address.channel,
                        });
                    }
                }
                if (ImGui::Button("+ Key")) {
                    std::string id{};
                    (void)AddCurveKey(
                        emitter.id,
                        address.moduleId,
                        address.propertyId,
                        address.channel,
                        id,
                        statusMessage_);
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(selectedCurveKeyId_.empty());
                if (ImGui::Button("Delete Key")) {
                    (void)RemoveCurveKey(
                        emitter.id,
                        address.moduleId,
                        address.propertyId,
                        address.channel,
                        selectedCurveKeyId_,
                        statusMessage_);
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "%zu / %u keys | %s",
                    curve->keys.size(),
                    RENDER3D::PARTICLES::kMaxParticleCurveKeys,
                    VFX::ToString(curve->interpolation).data());
            }
        }
        if (emitter.color.value.mode ==
            VFX::VfxAnimatedColorMode::Gradient) {
            DrawGradient(
                *this,
                emitter,
                emitter.color.value.gradient,
                selectedColorStopId_,
                statusMessage_);
        }
#else
        (void)source;
#endif
    }


} // namespace HIKARI::EDITOR
