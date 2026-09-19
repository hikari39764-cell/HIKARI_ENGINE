#include "Assets/Vfx/HIKARI_VfxBudgets.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <cctype>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
        uint32_t EffectOrder(const MaterialEffectStep& effect) {
            const auto stage = GetMaterialEffectStage(effect.kind);
            if (stage != MaterialEffectStage::Uv) return static_cast<uint32_t>(stage) * 8u;
            if (effect.kind == MaterialEffectKind::FlowMap) return 6u;
            if (effect.kind == MaterialEffectKind::UvFlow) return static_cast<uint32_t>(effect.textureTarget);
            return 0u;
        }
    }
    bool VfxWorkspaceController::CopyLayerEffects(std::string_view layerId) {
        const auto* layer = FindEmitter(layerId);
        if (!layer || layer->effects.empty()) return false;
        effectClipboard_ = layer->effects;
        effectClipboardAdjustment_ = layer->kind == VFX::VfxLayerKind::Adjustment;
        effectTrackClipboard_.clear();
        for (const auto& track : layer->parameterTracks)
            if (std::any_of(layer->effects.begin(), layer->effects.end(), [&](const auto& e) { return e.id == track.targetId; }))
                effectTrackClipboard_.push_back(track);
        statusMessage_ = "Effect stack and animation copied";
        return true;
    }
    bool VfxWorkspaceController::PasteLayerEffects(std::string_view layerId, std::string& message) {
        auto next = document_.Asset();
        auto* layer = FindEmitterMutable(next, layerId);
        if (!layer || effectClipboard_.empty() || layer->effects.size() + effectClipboard_.size() > VFX::kMaximumVfxEffectsPerLayer ||
            layer->kind == VFX::VfxLayerKind::Group || VFX::IsCustomVfxMaterial(layer->renderCommon.materialMode) ||
            effectClipboardAdjustment_ != (layer->kind == VFX::VfxLayerKind::Adjustment)) {
            message = "Paste requires a compatible layer and enough effect slots"; return false;
        }
        auto copy = VFX::MakeDefaultVfxAssetData().emitters.front();
        copy.effects = effectClipboard_; copy.parameterTracks = effectTrackClipboard_;
        RegenerateEmitterIds(copy);
        layer->effects.insert(layer->effects.end(), copy.effects.begin(), copy.effects.end());
        layer->parameterTracks.insert(layer->parameterTracks.end(), copy.parameterTracks.begin(), copy.parameterTracks.end());
        return ApplyAsset(std::move(next), message);
    }
    bool VfxWorkspaceController::AddLayerEffect(std::string_view layerId,
        MaterialEffectKind kind, std::string& message) {
        auto next = document_.Asset();
        auto* layer = FindEmitterMutable(next, layerId);
        if (!layer || layer->kind == VFX::VfxLayerKind::Group || layer->effects.size() >= VFX::kMaximumVfxEffectsPerLayer ||
            MaterialEffectName(kind) == "Invalid" || VFX::IsCustomVfxMaterial(layer->renderCommon.materialMode)) {
            message = "Choose a supported layer with fewer than " + std::to_string(VFX::kMaximumVfxEffectsPerLayer) + " effects";
            return false;
        }
        const bool adjustment = layer->kind == VFX::VfxLayerKind::Adjustment;
        if ((adjustment && !IsCompositeMaterialEffect(kind)) || (!adjustment && IsCompositeOnlyMaterialEffect(kind))) {
            message = "Effect is not supported by this layer type"; return false;
        }
        auto effect = MakeMaterialEffect(kind, AllocateStableId("effect"));
        if (adjustment && kind == MaterialEffectKind::Blur) effect.parameters.x = 2;
        if (adjustment && kind == MaterialEffectKind::Glow) effect.parameters.z = 6;
        layer->effects.push_back(std::move(effect));
        return ApplyAsset(std::move(next), message);
    }
    bool VfxWorkspaceController::RemoveLayerEffect(std::string_view layerId,
        std::string_view effectId, std::string& message) {
        auto next = document_.Asset();
        auto* layer = FindEmitterMutable(next, layerId);
        if (!layer || std::erase_if(layer->effects, [effectId](const auto& e) { return e.id == effectId; }) == 0) {
            message = "Effect was not found"; return false;
        }
        std::erase_if(layer->parameterTracks,[effectId](const auto& t){return t.targetId==effectId;});
        return ApplyAsset(std::move(next), message);
    }
    bool VfxWorkspaceController::MoveLayerEffect(std::string_view layerId,
        std::string_view effectId, int direction, std::string& message) {
        auto next = document_.Asset();
        auto* layer = FindEmitterMutable(next, layerId);
        if (!layer || (direction != -1 && direction != 1)) { message = "Expected layer and direction -1 or 1"; return false; }
        auto it = std::find_if(layer->effects.begin(), layer->effects.end(), [effectId](const auto& e) { return e.id == effectId; });
        if (it == layer->effects.end() || (direction < 0 && it == layer->effects.begin()) ||
            (direction > 0 && std::next(it) == layer->effects.end())) { message = "Effect cannot move beyond the stack"; return false; }
        auto candidate = it + direction;
        if (layer->kind != VFX::VfxLayerKind::Adjustment) {
            const auto stage = EffectOrder(*it);
            while (EffectOrder(*candidate) != stage) {
                if ((direction < 0 && candidate == layer->effects.begin()) ||
                    (direction > 0 && std::next(candidate) == layer->effects.end())) {
                    message = "Effects can move only within their execution stage"; return false;
                }
                candidate += direction;
            }
        }
        std::iter_swap(it, candidate);
        return ApplyAsset(std::move(next), message);
    }

    namespace {
        MaterialEffectStep* FindEffectMutable(
            VFX::VfxEmitterData& layer,
            std::string_view effectId) {
            const auto found = std::find_if(
                layer.effects.begin(), layer.effects.end(),
                [effectId](const MaterialEffectStep& effect) {
                    return effect.id == effectId;
                });
            return found == layer.effects.end() ? nullptr : &*found;
        }
    }

    bool VfxWorkspaceController::AddLayerEffectColorStop(
        std::string_view layerId,
        std::string_view effectId,
        std::string& outId,
        std::string& outMessage) {

        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* layer = FindEmitterMutable(next, layerId);
        MaterialEffectStep* effect =
            layer != nullptr ? FindEffectMutable(*layer, effectId) : nullptr;
        if (effect == nullptr || effect->kind != MaterialEffectKind::Gradient) {
            outMessage = "Gradient effect was not found on this layer";
            return false;
        }
        if (effect->colorStops.size() >= kMaximumMaterialEffectColorStops) {
            outMessage = "Gradient stop limit reached";
            return false;
        }
        if (effect->colorStops.empty()) {
            // 旧資産は 2 色だけ。増やす時にストップへ展開する。
            effect->colorStops.push_back({"gradient.start", 0.0f, effect->colorA});
            effect->colorStops.push_back({"gradient.end", 1.0f, effect->colorB});
        }
        std::vector<float> times{};
        for (const auto& stop : effect->colorStops) {
            times.push_back(stop.time);
        }
        const std::string id = AllocateStableId("effect.gradient.stop");
        effect->colorStops.push_back({
            id, FindAvailableCurveTime(std::move(times)), { 1.0f, 1.0f, 1.0f, 1.0f } });
        std::sort(effect->colorStops.begin(), effect->colorStops.end(),
            [](const auto& left, const auto& right) { return left.time < right.time; });
        if (!ApplyAsset(std::move(next), outMessage)) return false;
        outId = id;
        outMessage = "Gradient stop added";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::RemoveLayerEffectColorStop(
        std::string_view layerId,
        std::string_view effectId,
        std::string_view stopId,
        std::string& outMessage) {

        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* layer = FindEmitterMutable(next, layerId);
        MaterialEffectStep* effect =
            layer != nullptr ? FindEffectMutable(*layer, effectId) : nullptr;
        if (effect == nullptr) {
            outMessage = "Effect was not found on this layer";
            return false;
        }
        // 2 ストップを割るとグラデーションとして成立しない。
        if (effect->colorStops.size() <= 2u ||
            std::erase_if(effect->colorStops,
                [stopId](const auto& stop) { return stop.id == stopId; }) == 0) {
            outMessage = "Gradient needs at least two stops";
            return false;
        }
        return ApplyAsset(std::move(next), outMessage);
    }

    bool VfxWorkspaceController::SetLayerEffectColorStop(
        std::string_view layerId,
        std::string_view effectId,
        std::string_view stopId,
        float time,
        const MATH::Vec4& color,
        std::string& outMessage,
        uint64_t mergeGroup) {

        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* layer = FindEmitterMutable(next, layerId);
        MaterialEffectStep* effect =
            layer != nullptr ? FindEffectMutable(*layer, effectId) : nullptr;
        if (effect == nullptr) {
            outMessage = "Effect was not found on this layer";
            return false;
        }
        const auto stop = std::find_if(effect->colorStops.begin(),
            effect->colorStops.end(),
            [stopId](const auto& candidate) { return candidate.id == stopId; });
        if (stop == effect->colorStops.end()) {
            outMessage = "Gradient stop was not found";
            return false;
        }
        stop->time = std::clamp(time, 0.0f, 1.0f);
        stop->color = color;
        std::sort(effect->colorStops.begin(), effect->colorStops.end(),
            [](const auto& left, const auto& right) { return left.time < right.time; });
        return ApplyAsset(std::move(next), outMessage, mergeGroup);
    }

    void VfxWorkspaceController::DrawLayerEffects(AssetDatabase& database, const VFX::VfxEmitterData& layer) {
#if defined(HIKARI_WITH_EDITOR)
        ImGui::SeparatorText(TEXT::Localize("ui.vfx.effects").c_str());
        if (VFX::IsCustomVfxMaterial(layer.renderCommon.materialMode)) {
            ImGui::TextWrapped("%s", TEXT::Localize("ui.vfx.effects.custom").c_str()); return;
        }
        ImGui::BeginDisabled(layer.effects.size() >= VFX::kMaximumVfxEffectsPerLayer);
        if (ImGui::Button(TEXT::Localize("ui.vfx.effects.add").c_str())) ImGui::OpenPopup("EffectMenu");
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(layer.effects.empty());
        if (ImGui::SmallButton(TEXT::Localize("ui.vfx.effects.copy").c_str())) (void)CopyLayerEffects(layer.id);
        ImGui::EndDisabled();
        ImGui::SameLine();
        const bool canPaste = !effectClipboard_.empty() &&
            effectClipboardAdjustment_ == (layer.kind == VFX::VfxLayerKind::Adjustment) &&
            layer.effects.size() + effectClipboard_.size() <= VFX::kMaximumVfxEffectsPerLayer;
        ImGui::BeginDisabled(!canPaste);
        if (ImGui::SmallButton(TEXT::Localize("ui.vfx.effects.paste").c_str())) (void)PasteLayerEffects(layer.id, statusMessage_);
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("EffectMenu")) {
            ImGui::InputTextWithHint("##EffectSearch", TEXT::Localize("ui.vfx.effects.search").c_str(), effectSearch_, sizeof(effectSearch_));
            // 一覧の所有者は資産側の名前表。UI に手書きの件数を残さない。
            const auto kindNames = GetMaterialEffectKindNames();
            for (size_t i = 0; i < kindNames.size(); ++i) {
                const auto kind = static_cast<MaterialEffectKind>(i);
                if (layer.kind == VFX::VfxLayerKind::Adjustment ? !IsCompositeMaterialEffect(kind) : IsCompositeOnlyMaterialEffect(kind)) continue;
                const auto key = "ui.vfx.effect." + std::string(layer.kind == VFX::VfxLayerKind::Adjustment && kind == MaterialEffectKind::Blur ? "ImageBlur" : MaterialEffectName(kind));
                const auto localized = TEXT::Localize(key);
                const auto matches = [this](std::string_view value) {
                    const std::string_view query{effectSearch_};
                    return std::search(value.begin(), value.end(), query.begin(), query.end(),
                        [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != value.end();
                };
                if (effectSearch_[0] && !matches(localized) && !matches(kindNames[i])) continue;
                if (ImGui::MenuItem(localized.c_str())) (void)AddLayerEffect(layer.id, kind, statusMessage_);
            }
            ImGui::EndPopup();
        }
        std::vector<const MaterialEffectStep*> ordered;
        for (const auto& effect : layer.effects) ordered.push_back(&effect);
        if (layer.kind != VFX::VfxLayerKind::Adjustment)
            std::stable_sort(ordered.begin(), ordered.end(), [](const auto* a, const auto* b) {
                return EffectOrder(*a) < EffectOrder(*b);
            });
        std::optional<MaterialEffectStage> lastStage;
        for (const auto* entry : ordered) {
            const auto& effect = *entry;
            const auto stage = GetMaterialEffectStage(effect.kind);
            if (layer.kind != VFX::VfxLayerKind::Adjustment && lastStage != stage) {
                ImGui::SeparatorText(TEXT::Localize("ui.vfx.effects.stage." + std::to_string(static_cast<int>(stage))).c_str());
                lastStage = stage;
            }
            ImGui::PushID(effect.id.c_str());
            VfxInspectorPropertyEditor edit(*this, database, effect.id, statusMessage_);
            const auto label = TEXT::Localize("ui.vfx.effect." + std::string(layer.kind == VFX::VfxLayerKind::Adjustment && effect.kind == MaterialEffectKind::Blur ? "ImageBlur" : MaterialEffectName(effect.kind)));
            const bool open = ImGui::TreeNodeEx("settings", ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str());
            if (ImGui::BeginPopupContextItem()) {
                const auto position = std::find(ordered.begin(), ordered.end(), entry);
                const auto compatible = [&](const auto* adjacent) { return layer.kind == VFX::VfxLayerKind::Adjustment || EffectOrder(*adjacent) == EffectOrder(effect); };
                const bool canUp = position != ordered.begin() && compatible(*std::prev(position));
                const bool canDown = std::next(position) != ordered.end() && compatible(*std::next(position));
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.up").c_str(), nullptr, false, canUp)) (void)MoveLayerEffect(layer.id, effect.id, -1, statusMessage_);
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.down").c_str(), nullptr, false, canDown)) (void)MoveLayerEffect(layer.id, effect.id, 1, statusMessage_);
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.remove").c_str())) (void)RemoveLayerEffect(layer.id, effect.id, statusMessage_);
                ImGui::EndPopup();
            }
            if (open) {
                edit.Bool(TEXT::Localize("ui.vfx.effects.enabled").c_str(), "enabled", effect.enabled);
                if (effect.kind == MaterialEffectKind::UvFlow || effect.kind == MaterialEffectKind::FlowMap) {
                    const auto targets = GetMaterialEffectTextureTargets();
                    if (ImGui::BeginCombo(TEXT::Localize("ui.vfx.effects.textureTarget").c_str(),
                            TEXT::Localize("ui.vfx.uvTarget." + std::string(MaterialEffectTextureTargetName(effect.textureTarget))).c_str())) {
                        for (size_t i = 0; i < targets.size(); ++i) {
                            if (effect.kind == MaterialEffectKind::FlowMap && i > 2) continue;
                            if (ImGui::Selectable(TEXT::Localize("ui.vfx.uvTarget." + std::string(targets[i])).c_str(),
                                    static_cast<size_t>(effect.textureTarget) == i))
                                (void)SetProperty(effect.id, "textureTarget", std::string(targets[i]), statusMessage_);
                        }
                        ImGui::EndCombo();
                    }
                    if (effect.kind == MaterialEffectKind::FlowMap)
                        ImGui::TextWrapped("%s", TEXT::Localize("ui.vfx.effects.flowHelp").c_str());
                }
                // 多段グラデーションはストップを直接並べて編集する。
                if (effect.kind == MaterialEffectKind::Gradient) {
                    ImGui::TextDisabled("%s", VfxUiText("Gradient stops").c_str());
                    for (const auto& stop : effect.colorStops) {
                        ImGui::PushID(stop.id.c_str());
                        float time = stop.time;
                        MATH::Vec4 color = stop.color;
                        bool changed = false;
                        ImGui::SetNextItemWidth(120.0f);
                        changed |= ImGui::SliderFloat(
                            "##time", &time, 0.0f, 1.0f, "%.2f");
                        bool seal = ImGui::IsItemDeactivatedAfterEdit();
                        ImGui::SameLine();
                        changed |= ImGui::ColorEdit4(
                            "##color", &color.x,
                            ImGuiColorEditFlags_NoInputs |
                                ImGuiColorEditFlags_AlphaBar);
                        seal |= ImGui::IsItemDeactivatedAfterEdit();
                        ImGui::SameLine();
                        if (ImGui::SmallButton(VfxUiText("Remove").c_str())) {
                            (void)RemoveLayerEffectColorStop(
                                layer.id, effect.id, stop.id, statusMessage_);
                            changed = false;
                        } else if (changed) {
                            (void)SetLayerEffectColorStop(
                                layer.id, effect.id, stop.id, time, color,
                                statusMessage_,
                                VfxInspectorPropertyEditor::MergeGroup(stop.id, "gradient"));
                        }
                        if (seal) SealPropertyEdit();
                        ImGui::PopID();
                    }
                    if (ImGui::SmallButton(VfxUiText("+ Stop").c_str())) {
                        std::string id{};
                        (void)AddLayerEffectColorStop(
                            layer.id, effect.id, id, statusMessage_);
                    }
                    ImGui::Separator();
                }
                const auto drawSlots = [&](const std::array<const char*, 4>& table, const char* slot,
                                           const MATH::Vec4& value) {
                    float values[] = {value.x, value.y, value.z, value.w};
                    bool changed = false;
                    bool seal = false;
                    for (size_t p = 0; p < 4 && table[p]; ++p) {
                        const auto name = TEXT::Localize("ui.vfx.effect.param." + std::string(table[p]));
                        const std::string property=std::string(slot)+"."+"xyzw"[p];
                        if (DrawLayerParameter(layer,effect.id,property)) continue;
                        changed |= ImGui::DragFloat(name.c_str(), &values[p], 0.01f);
                        seal |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    if (changed) (void)SetProperty(effect.id, slot, {values[0],values[1],values[2],values[3]}, statusMessage_, VfxInspectorPropertyEditor::MergeGroup(effect.id, slot), &database);
                    if (seal) SealPropertyEdit();
                };
                drawSlots(layer.kind == VFX::VfxLayerKind::Adjustment ? CompositeEffectParameterNames(effect.kind) : MaterialEffectParameterNames(effect.kind), "parameters", effect.parameters);
                drawSlots(layer.kind == VFX::VfxLayerKind::Adjustment ? CompositeEffectParameterNames(effect.kind, true) : MaterialEffectParameterNames(effect.kind, true), "parameters2", effect.parameters2);
                if (effect.kind==MaterialEffectKind::Dissolve || effect.kind==MaterialEffectKind::Wipe)
                    (void)DrawLayerParameter(layer,effect.id,"progress");
                if (effect.kind == MaterialEffectKind::Gradient || effect.kind == MaterialEffectKind::Dissolve ||
                    effect.kind == MaterialEffectKind::Emission || effect.kind == MaterialEffectKind::Fresnel ||
                    effect.kind == MaterialEffectKind::Glow || effect.kind == MaterialEffectKind::LightSweep || effect.kind == MaterialEffectKind::Ink)
                    edit.Color(TEXT::Localize("ui.vfx.effects.colorA").c_str(), "colorA", effect.colorA);
                if (effect.kind == MaterialEffectKind::Gradient)
                    edit.Color(TEXT::Localize("ui.vfx.effects.colorB").c_str(), "colorB", effect.colorB);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
#else
        (void)database; (void)layer;
#endif
    }
}
