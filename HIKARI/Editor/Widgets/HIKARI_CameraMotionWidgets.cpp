#include "Editor/Widgets/HIKARI_CameraMotionWidgets.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "imgui.h"
#include "Scene/Sequencer/Serialization/HIKARI_ShotPresentationJson.h"
#include <algorithm>
#include <array>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::Localize(key).c_str(); }
bool Button(EditorGlyph glyph, const char* id, const char* key) {
    return IconButton(glyph, id, EditorButtonTone::Quiet, {23, 23}, L(key));
}
bool Parameter(HIKARI::ScalarCurve& p, float time) {
    bool changed = false;
    float value = p.Evaluate(time);
    ImGui::SetNextItemWidth(std::max(65.0f, ImGui::GetContentRegionAvail().x - 60));
    if (ImGui::DragFloat("##value", &value, .01f)) {
        if (p.keys.empty())
            p.value = value;
        else
            HIKARI::SetScalarCurveKey(p, time, value);
        changed = true;
    }
    ImGui::SameLine();
    if (Button(EditorGlyph::Keyframe, "key", "cine.addKey")) {
        HIKARI::SetScalarCurveKey(p, time, value);
        changed = true;
    }
    if (!p.keys.empty() && ImGui::TreeNode(L("cine.editCurve"))) {
        std::array<float, 96> samples;
        const float end = std::max(1.0f, p.keys.back().time);
        for (size_t i = 0; i < samples.size(); ++i)
            samples[i] = p.Evaluate(end * i / (samples.size() - 1));
        ImGui::PlotLines("##curve", samples.data(), static_cast<int>(samples.size()), 0, nullptr, FLT_MAX, FLT_MAX,
                         {0, 64});
        int remove = -1;
        for (size_t i = 0; i < p.keys.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(65);
            if (ImGui::DragFloat("##time", &p.keys[i].time, .01f, 0, 10000, "%.2f s")) {
                const float low = i ? p.keys[i - 1].time + .0001f : 0;
                const float high = i + 1 < p.keys.size() ? p.keys[i + 1].time - .0001f : 10000;
                p.keys[i].time = std::clamp(p.keys[i].time, low, std::max(low, high));
                changed = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70);
            changed |= ImGui::DragFloat("##value", &p.keys[i].value, .01f);
            ImGui::SameLine();
            if (Button(EditorGlyph::Delete, "delete", "cine.deleteKey"))
                remove = static_cast<int>(i);
            ImGui::PopID();
        }
        if (remove >= 0) {
            p.keys.erase(p.keys.begin() + remove);
            changed = true;
        }
        ImGui::TreePop();
    }
    return changed;
}
} // namespace

bool DrawCameraMotionStack(std::vector<SEQUENCER::CameraMotionEffect>& effects, float seconds,
                           const CameraMotionCurveEditor& curveEditor, bool allowSpeedDriven) {
    using namespace SEQUENCER;
    bool changed = false;
    if (IconTextButton(EditorGlyph::Add, L("cine.addEffect"), "addMotion"))
        ImGui::OpenPopup("motionMenu");
    if (ImGui::BeginPopup("motionMenu")) {
        static std::array<char, 96> filter{};
        if (ImGui::IsWindowAppearing()) {
            filter[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        SearchField("filter", L("ui.vfx.effects.search"), filter.data(), filter.size());
        const char* group = nullptr;
        for (const auto& descriptor : kCameraMotionDescriptors) {
            if (descriptor.speedDriven && !allowSpeedDriven)
                continue;
            const std::string name = L(descriptor.key);
            if (filter[0] && name.find(filter.data()) == std::string::npos &&
                std::string(descriptor.key).find(filter.data()) == std::string::npos)
                continue;
            if (!group || std::string_view(group) != descriptor.group) {
                ImGui::SeparatorText(L(descriptor.group));
                group = descriptor.group;
            }
            if (ImGui::Selectable(name.c_str())) {
                CameraMotionEffect e;
                e.kind = descriptor.kind;
                e.amount.value = descriptor.defaultAmount;
                if (descriptor.oscillating)
                    e.frequency = e.kind == CameraMotionKind::Shake ? 12.0f : 2.0f;
                for (const auto& old : effects)
                    e.id = std::max(e.id, old.id + 1);
                effects.push_back(e);
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (Button(EditorGlyph::Duplicate, "copyStack", "cm.copyStack"))
        ImGui::SetClipboardText(
            nlohmann::json{{"assetType", "CameraMotionStack"}, {"effects", WriteCameraMotionEffects(effects)}}
                .dump()
                .c_str());
    ImGui::SameLine();
    if (Button(EditorGlyph::Import, "pasteStack", "cm.pasteStack")) {
        const char* text = ImGui::GetClipboardText();
        try {
            const auto json = nlohmann::json::parse(text ? text : "");
            if (json.value("assetType", "") == "CameraMotionStack") {
                auto incoming = ReadCameraMotionEffects(json.at("effects"));
                if (!allowSpeedDriven && std::any_of(incoming.begin(), incoming.end(), [](const auto& e) {
                        return kCameraMotionDescriptors[static_cast<size_t>(e.kind)].speedDriven;
                    }))
                    throw std::runtime_error("Speed effects require a gameplay target");
                effects.insert(effects.end(), incoming.begin(), incoming.end());
                NormalizeCameraMotionEffects(effects);
                changed = true;
            } else
                ImGui::OpenPopup("invalidStack");
        } catch (...) {
            ImGui::OpenPopup("invalidStack");
        }
    }
    if (ImGui::BeginPopup("invalidStack")) {
        ImGui::TextWrapped("%s", L("cm.invalidStack"));
        ImGui::EndPopup();
    }
    if (effects.empty())
        ImGui::TextWrapped("%s", L("cm.empty"));
    int remove = -1, move = -1, duplicate = -1;
    int direction = 0;
    for (size_t i = 0; i < effects.size(); ++i) {
        auto& e = effects[i];
        const auto index = static_cast<size_t>(e.kind);
        if (index >= kCameraMotionDescriptors.size())
            continue;
        const auto& descriptor = kCameraMotionDescriptors[index];
        ImGui::PushID(std::to_string(e.id).c_str());
        changed |= ImGui::Checkbox("##enabled", &e.enabled);
        ImGui::SameLine();
        const bool open = ImGui::TreeNodeEx("effect", ImGuiTreeNodeFlags_DefaultOpen, "%s", L(descriptor.key));
        ImGui::SameLine();
        if (Button(EditorGlyph::More, "options", "cm.options"))
            ImGui::OpenPopup("options");
        if (ImGui::BeginPopup("options")) {
            if (ImGui::MenuItem(L("gc.duplicate")))
                duplicate = static_cast<int>(i);
            if (ImGui::MenuItem(L("ui.vfx.effects.up"), nullptr, false, i > 0)) {
                move = static_cast<int>(i);
                direction = -1;
            }
            if (ImGui::MenuItem(L("cm.down"), nullptr, false, i + 1 < effects.size())) {
                move = static_cast<int>(i);
                direction = 1;
            }
            if (ImGui::MenuItem(L("cine.removeModule")))
                remove = static_cast<int>(i);
            ImGui::EndPopup();
        }
        if (open) {
            const float local = std::max(0.0f, seconds - e.start);
            for (const auto parameter : {0, 1}) {
                ImGui::PushID(parameter);
                ImGui::TextUnformatted(L(parameter ? "cine.weight" : "cine.amount"));
                auto& value = parameter ? e.weight : e.amount;
                const auto channel = "motion/" + std::to_string(e.id) + (parameter ? "/weight" : "/amount");
                changed |= curveEditor ? curveEditor(value, local, channel) : Parameter(value, local);
                ImGui::PopID();
            }
            if (descriptor.oscillating)
                changed |= ImGui::SliderFloat(L("cine.frequency"), &e.frequency, 0, 30);
            if (descriptor.speedDriven)
                changed |= ImGui::DragFloat(L("gc.speed"), &e.maximumSpeed, .1f, .01f, 1000);
            if (ImGui::TreeNode(L("cm.timing"))) {
                changed |= ImGui::DragFloat(L("ui.vfx.text.Start"), &e.start, .01f, 0, 10000);
                bool limited = e.duration > 0;
                if (ImGui::Checkbox(L("cm.limited"), &limited)) {
                    e.duration = limited ? 1.0f : 0;
                    changed = true;
                }
                if (limited) {
                    changed |= ImGui::DragFloat(L("cine.duration"), &e.duration, .01f, .01f, 10000);
                    changed |= ImGui::DragFloat(L("cine.fadeOut"), &e.fadeOut, .01f, 0, e.duration);
                }
                changed |= ImGui::DragFloat(L("cine.fadeIn"), &e.fadeIn, .01f, 0, 10000);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    if (remove >= 0) {
        effects.erase(effects.begin() + remove);
        changed = true;
    } else if (move >= 0) {
        std::swap(effects[move], effects[move + direction]);
        changed = true;
    } else if (duplicate >= 0) {
        auto copy = effects[duplicate];
        for (const auto& e : effects)
            copy.id = std::max(copy.id, e.id + 1);
        effects.insert(effects.begin() + duplicate + 1, std::move(copy));
        changed = true;
    }
    if (changed)
        NormalizeCameraMotionEffects(effects);
    return changed;
}
} // namespace HIKARI::EDITOR
