#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsPresentation.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Cinematics/HIKARI_CinematicsWorkspaceController.h"
#include <algorithm>
#include <array>
#include <cmath>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool CinematicsWorkspaceController::DrawCurve(HIKARI::ScalarCurve& parameter, float seconds,
                                              const std::string& channel) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    float value = parameter.Evaluate(seconds);
    ImGui::SetNextItemWidth((std::max)(60.0f, ImGui::GetContentRegionAvail().x - 62));
    if (ImGui::DragFloat("##value", &value, 0.01f)) {
        if (autoKey_ || !parameter.keys.empty())
            HIKARI::SetScalarCurveKey(parameter, seconds, value);
        else
            parameter.value = value;
        changed = true;
    }
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Keyframe, "addParameterKey", EditorButtonTone::Quiet, {22, 22},
                   TEXT::Localize("cine.addKey").c_str())) {
        HIKARI::SetScalarCurveKey(parameter, seconds, value);
        selectedChannel_ = channel;
        curveView_ = {};
        changed = true;
    }
    ImGui::SameLine();
    if (IconButton(EditorGlyph::Animation, "showCurve", EditorButtonTone::Quiet, {22, 22},
                   TEXT::Localize("cine.editCurve").c_str())) {
        selectedChannel_ = channel;
        curveView_ = {};
        curveMode_ = true;
    }
#endif
    return changed;
}

bool CinematicsWorkspaceController::DrawEffects(std::vector<SEQUENCER::SequenceScreenEffect>& effects, float seconds) {
    bool changed = false;
#if defined(HIKARI_WITH_EDITOR)
    using namespace SEQUENCER;
    std::array<std::string, 12> names;
    for (size_t i = 0; i < names.size(); ++i)
        names[i] = TEXT::Localize(kCinematicEffectKeys[i]);
    if (IconTextButton(EditorGlyph::Add, TEXT::Localize("cine.addEffect").c_str(), "addEffect"))
        ImGui::OpenPopup("effectMenu");
    if (ImGui::BeginPopup("effectMenu")) {
        static std::array<char, 96> filter{};
        SearchField("effectFilter", TEXT::Localize("ui.vfx.effects.search").c_str(), filter.data(), filter.size());
        for (int i = 0; i < 12; ++i) {
            if (i == 0)
                ImGui::SeparatorText(TEXT::Localize("cine.baseLook").c_str());
            if (i == 6)
                ImGui::SeparatorText(TEXT::Localize("cine.styleTransition").c_str());
            if (filter[0] && std::string(names[i]).find(filter.data()) == std::string::npos)
                continue;
            if (ImGui::Selectable(names[i].c_str())) {
                SequenceScreenEffect e;
                for (const auto& old : effects)
                    e.id = (std::max)(e.id, old.id + 1);
                e.kind = static_cast<ScreenEffectKind>(i);
                e.name = names[i];
                e.amount.value = i == 5 ? 6500.0f : i == 10 ? 8.0f : i == 2 || i == 3 || i == 4 ? 1.0f : 0.5f;
                if (i == 9)
                    e.color = {0, 0, 0};
                effects.push_back(e);
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    int remove = -1, move = -1;
    for (size_t i = 0; i < effects.size(); ++i) {
        auto& e = effects[i];
        ImGui::PushID(static_cast<int>(e.id));
        changed |= ImGui::Checkbox("##enabled", &e.enabled);
        ImGui::SameLine();
        const bool open = ImGui::TreeNodeEx("effect", 0, "%s", names[static_cast<int>(e.kind)].c_str());
        ImGui::SameLine();
        if (IconButton(EditorGlyph::Delete, "deleteEffect", EditorButtonTone::Quiet, {20, 20},
                       TEXT::Localize("ui.vfx.effects.remove").c_str()))
            remove = static_cast<int>(i);
        if (i) {
            ImGui::SameLine();
            if (ImGui::SmallButton(TEXT::LocalizeWindow("ui.vfx.effects.up", "上移").c_str()))
                move = static_cast<int>(i);
        }
        if (open) {
            ImGui::TextDisabled(static_cast<int>(e.kind) < 6 ? TEXT::LocalizeCStr("cine.stageOverride")
                                                             : TEXT::LocalizeCStr("cine.hdrEffect"));
            changed |=
                ImGui::DragFloat(TEXT::LocalizeWindow("ui.vfx.text.Start", "开始").c_str(), &e.start, 0.02f, 0, 10000);
            changed |= ImGui::DragFloat(TEXT::LocalizeWindow("cine.duration", "时长").c_str(), &e.duration, 0.02f,
                                        0.01f, 10000);
            changed |=
                ImGui::DragFloat(TEXT::LocalizeWindow("cine.fadeIn", "淡入").c_str(), &e.fadeIn, 0.02f, 0, e.duration);
            changed |= ImGui::DragFloat(TEXT::LocalizeWindow("cine.fadeOut", "淡出").c_str(), &e.fadeOut, 0.02f, 0,
                                        e.duration);
            const float local = (std::max)(0.0f, seconds - e.start);
            ImGui::PushID("amount");
            ImGui::TextUnformatted(TEXT::Localize("cine.amount").c_str());
            changed |= DrawCurve(e.amount, local, "effect/" + std::to_string(e.id) + "/amount");
            ImGui::PopID();
            ImGui::PushID("weight");
            ImGui::TextUnformatted(TEXT::Localize("cine.blendWeight").c_str());
            changed |= DrawCurve(e.weight, local, "effect/" + std::to_string(e.id) + "/weight");
            ImGui::PopID();
            if (e.kind == ScreenEffectKind::Tint || e.kind == ScreenEffectKind::Fade)
                changed |= ImGui::ColorEdit3(TEXT::LocalizeWindow("ui.vfx.text.Color", "颜色").c_str(), &e.color.x,
                                             ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove >= 0) {
        effects.erase(effects.begin() + remove);
        changed = true;
    } else if (move > 0) {
        std::swap(effects[move], effects[move - 1]);
        changed = true;
    }
#endif
    return changed;
}
} // namespace HIKARI::EDITOR
