#include "Editor/Workspaces/GameplayCamera/HIKARI_GameplayCameraWorkspaceController.h"
#include "Editor/Widgets/HIKARI_CameraMotionWidgets.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace HIKARI::EDITOR {
namespace {
const char* L(const char* key) { return TEXT::LocalizeCStr(key); }
bool Icon(EditorGlyph glyph, const char* id, const char* key) {
    return IconButton(glyph, id, EditorButtonTone::Quiet, {26, 26}, L(key));
}
bool Field(const char* key, std::string& value) {
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (!ImGui::InputText(L(key), buffer, sizeof(buffer)))
        return false;
    value = buffer;
    return true;
}
} // namespace

bool GameplayCameraWorkspaceController::PreviewEffect(DocumentSceneBase& scene, std::string_view id, float time,
                                                      float strength, bool play, std::string& message) {
    const auto* effect = document_.Profile().FindEffect(id);
    if (!effect || !std::isfinite(time) || !std::isfinite(strength) || time < 0 || strength < 0) {
        message = "Invalid camera effect preview.";
        return false;
    }
    if (scene.IsRuntimePlayActive()) {
        auto* object = scene.GetWorld().FindObject(camera_);
        auto* follow = object ? object->GetComponent<CameraFollowComponent>() : nullptr;
        if (!follow || !play) {
            message = "Live camera playback requires an active rig and a play request.";
            return false;
        }
        return CAMERA::PlayGameplayCameraEffect(*object, *follow, id, strength, message) != 0;
    }
    effect_ = id;
    effectTime_ = std::min(time, effect->duration);
    effectStrength_ = strength;
    effectsEditing_ = true;
    flowEditing_ = false;
    effectBypass_ = false;
    effectPlaying_ = play;
    message = "Camera effect preview ready.";
    return true;
}

bool GameplayCameraWorkspaceController::SelectCamera(DocumentSceneBase& scene, SceneObjectId id) {
    const auto* object = scene.GetWorld().FindObject(id);
    const auto* follow = object ? object->GetComponent<CameraFollowComponent>() : nullptr;
    const auto guid = follow ? follow->GameplayBinding().profile : AssetGuid{};
    if (guid.IsValid() && guid != document_.Guid() && !document_.Open(scene.GetAssetDatabase(), guid, message_))
        return false;
    camera_ = id;
    target_ = follow ? follow->GetTargetObjectId() : SceneObjectId{};
    lookTarget_ = follow ? follow->GameplayBinding().lookTarget : SceneObjectId{};
    if (follow && !follow->GameplayBinding().initialMode.empty())
        mode_ = follow->GameplayBinding().initialMode;
    focused_ = false;
    previewBinding_.clear();
    effectPlaying_ = false;
    effectTime_ = 0;
    return true;
}

void GameplayCameraWorkspaceController::DrawEffectLibrary() {
    if (!document_.IsOpen())
        return;
    ImGui::SeparatorText(L("cm.clips"));
    if (IconTextButton(EditorGlyph::Add, L("cm.newClip"), "newCameraEffect")) {
        auto profile = document_.Profile();
        auto effect = CAMERA::MakeGameplayCameraPreset("third_person").effects.front();
        for (unsigned i = 1; profile.FindEffect(effect.id); ++i)
            effect.id = "impact_" + std::to_string(i);
        effect.name = L("cm.impact");
        effect_ = effect.id;
        profile.effects.push_back(std::move(effect));
        if (document_.Apply(std::move(profile), message_)) {
            effectsEditing_ = true;
            flowEditing_ = false;
            effectTime_ = 0;
        }
    }
    for (const auto& effect : document_.Profile().effects) {
        if (!search_.empty() && effect.name.find(search_) == std::string::npos &&
            effect.id.find(search_) == std::string::npos)
            continue;
        ImGui::PushID(effect.id.c_str());
        if (ImGui::Selectable(effect.name.c_str(), effectsEditing_ && effect_ == effect.id)) {
            effect_ = effect.id;
            effectsEditing_ = true;
            flowEditing_ = false;
            effectTime_ = 0;
            effectPlaying_ = false;
            document_.SealMerge();
        }
        if (ImGui::BeginPopupContextItem()) {
            bool done = false;
            if (ImGui::MenuItem(L("gc.duplicate"))) {
                auto profile = document_.Profile();
                auto copy = effect;
                copy.id = effect.id + "_copy";
                for (unsigned i = 2; profile.FindEffect(copy.id); ++i)
                    copy.id = effect.id + "_" + std::to_string(i);
                effect_ = copy.id;
                profile.effects.push_back(copy);
                document_.Apply(std::move(profile), message_);
                done = true;
            }
            if (!done && ImGui::MenuItem(L("cine.removeModule"))) {
                auto profile = document_.Profile();
                const auto id = effect.id;
                std::erase_if(profile.effects, [&](const auto& e) { return e.id == id; });
                document_.Apply(std::move(profile), message_);
                effect_.clear();
                effectPlaying_ = false;
                done = true;
            }
            ImGui::EndPopup();
            if (done) {
                ImGui::PopID();
                break;
            }
        }
        ImGui::PopID();
    }
}

void GameplayCameraWorkspaceController::DrawEffectInspector() {
    auto profile = document_.Profile();
    auto it =
        std::find_if(profile.effects.begin(), profile.effects.end(), [&](const auto& e) { return e.id == effect_; });
    if (it == profile.effects.end()) {
        ImGui::TextWrapped("%s", L("cm.pickClip"));
        return;
    }
    auto& effect = *it;
    bool changed = Field("gc.name", effect.name);
    ImGui::TextDisabled("%s: %s", L("cm.callId"), effect.id.c_str());
    if (IconTextButton(EditorGlyph::Duplicate, L("cm.copyCall"), "copyEffectCall")) {
        const auto quoted = nlohmann::json(effect.id).dump(-1, ' ', true);
        ImGui::SetClipboardText(
            ("auto token = world.GetCamera().PlayGameplayEffect(cameraHandle, " + quoted + ", 1.0f);").c_str());
    }
    changed |= ImGui::DragFloat(L("cine.duration"), &effect.duration, .01f, .01f, 3600);
    if (ImGui::TreeNode(L("cm.identifier"))) {
        ImGui::TextWrapped("%s", L("cm.identifierHint"));
        changed |= Field("cm.callId", effect.id);
        ImGui::TreePop();
    }
    ImGui::SeparatorText(L("gc.effects"));
    changed |= DrawCameraMotionStack(effect.effects, effectTime_, {}, true);
    if (changed) {
        const auto id = effect.id;
        if (document_.Apply(std::move(profile), message_, ImGui::GetActiveID()))
            effect_ = id;
    }
}

void GameplayCameraWorkspaceController::DrawEffectTimeline(DocumentSceneBase& scene) {
    const auto* effect = document_.Profile().FindEffect(effect_);
    if (!effect)
        return;
    ImGui::Separator();
    const bool runtime = scene.IsRuntimePlayActive();
    if (Icon(effectPlaying_ ? EditorGlyph::Pause : EditorGlyph::Play, "effectPlay", "cm.audition")) {
        if (runtime) {
            auto* object = scene.GetWorld().FindObject(camera_);
            auto* follow = object ? object->GetComponent<CameraFollowComponent>() : nullptr;
            if (follow)
                CAMERA::PlayGameplayCameraEffect(*object, *follow, effect_, effectStrength_, message_);
        } else {
            if (effectTime_ >= effect->duration)
                effectTime_ = 0;
            effectPlaying_ = !effectPlaying_;
        }
    }
    ImGui::SameLine();
    if (Icon(EditorGlyph::Revert, "effectRestart", "gc.reset")) {
        effectTime_ = 0;
        effectPlaying_ = false;
    }
    ImGui::SameLine();
    if (IconToggleButton(EditorGlyph::Loop, "effectLoop", effectLoop_, {26, 26}, L("cm.loop")))
        effectLoop_ = !effectLoop_;
    ImGui::SameLine();
    if (IconToggleButton(EditorGlyph::Hidden, "effectBypass", effectBypass_, {26, 26}, L("cm.bypass")))
        effectBypass_ = !effectBypass_;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    ImGui::SliderFloat(L("cm.strength"), &effectStrength_, 0, 3);
    ImGui::SetNextItemWidth(std::max(100.0f, ImGui::GetContentRegionAvail().x - 100));
    if (ImGui::SliderFloat(L("cm.time"), &effectTime_, 0, effect->duration, "%.3f s"))
        effectPlaying_ = false;
    ImGui::TextDisabled("%s", L(runtime ? "cm.runtimeHint" : "cm.timelineHint"));
    const float label = 145, row = 24;
    const float width = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    auto* draw = ImGui::GetWindowDrawList();
    for (const auto& item : effect->effects) {
        ImGui::PushID(std::to_string(item.id).c_str());
        const auto origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("span", {width, row});
        if (ImGui::IsItemActive()) {
            effectTime_ = std::clamp((ImGui::GetIO().MousePos.x - origin.x - label) / (width - label), 0.0f, 1.0f) *
                          effect->duration;
            effectPlaying_ = false;
        }
        const auto x = [&](float t) {
            return origin.x + label + std::clamp(t / effect->duration, 0.0f, 1.0f) * (width - label);
        };
        const float end = item.duration > 0 ? item.start + item.duration : effect->duration;
        draw->AddRectFilled({x(item.start), origin.y + 3}, {x(end), origin.y + row - 3},
                            item.enabled ? IM_COL32(30, 100, 112, 255) : IM_COL32(50, 55, 60, 255), 3);
        const auto index = static_cast<size_t>(item.kind);
        if (index < SEQUENCER::kCameraMotionDescriptors.size())
            draw->AddText({origin.x, origin.y + 4}, IM_COL32(180, 195, 205, 255),
                          L(SEQUENCER::kCameraMotionDescriptors[index].key));
        for (const auto* parameter : {&item.amount, &item.weight})
            for (const auto& key : parameter->keys) {
                const float px = x(item.start + key.time), py = origin.y + row * .5f;
                const ImVec2 diamond[]{{px, py - 4}, {px + 4, py}, {px, py + 4}, {px - 4, py}};
                draw->AddConvexPolyFilled(diamond, 4, IM_COL32(240, 200, 105, 255));
            }
        draw->AddLine({x(effectTime_), origin.y}, {x(effectTime_), origin.y + row}, IM_COL32(120, 230, 240, 255), 2);
        ImGui::PopID();
    }
}
} // namespace HIKARI::EDITOR
