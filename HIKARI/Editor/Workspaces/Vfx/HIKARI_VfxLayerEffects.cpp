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
        uint32_t EffectOrder(const MaterialEffectStep& effect, bool adjustment) {
            if (effect.space == MaterialEffectSpace::Group3D) return 0;
            if (adjustment && IsCompositeAdjustmentEffect(effect)) return 100;
            const auto stage = GetMaterialEffectStage(effect.kind);
            if (stage != MaterialEffectStage::Uv) return static_cast<uint32_t>(stage) * 8u + 1u;
            if (effect.kind == MaterialEffectKind::FlowMap) return 7u;
            if (effect.kind == MaterialEffectKind::UvFlow) return static_cast<uint32_t>(effect.textureTarget) + 1u;
            return 1u;
        }
    }
    bool VfxWorkspaceController::CopyLayerEffects(std::string_view layerId) {
        const auto* layer = FindEmitter(layerId);
        if (!layer || layer->effects.empty()) return false;
        effectClipboard_ = layer->effects;
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
            (layer->kind == VFX::VfxLayerKind::Adjustment && std::any_of(effectClipboard_.begin(),effectClipboard_.end(),[](const auto& e){return !IsAdjustmentMaterialEffect(e.kind);}))) {
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
        if (adjustment && !IsAdjustmentMaterialEffect(kind)) {
            message = "Effect is not supported by this layer type"; return false;
        }
        auto effect = MakeMaterialEffect(kind, AllocateStableId("effect"));
        if (adjustment && SupportsSpatialMaterialEffect(kind)) effect.space = MaterialEffectSpace::Group3D;
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
        {
            const auto stage = EffectOrder(*it, layer->kind == VFX::VfxLayerKind::Adjustment);
            while (EffectOrder(*candidate, layer->kind == VFX::VfxLayerKind::Adjustment) != stage) {
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
        const bool adjustment = layer.kind == VFX::VfxLayerKind::Adjustment;
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
            (layer.kind != VFX::VfxLayerKind::Adjustment || std::all_of(effectClipboard_.begin(),effectClipboard_.end(),[](const auto& e){return IsAdjustmentMaterialEffect(e.kind);})) &&
            layer.effects.size() + effectClipboard_.size() <= VFX::kMaximumVfxEffectsPerLayer;
        ImGui::BeginDisabled(!canPaste);
        if (ImGui::SmallButton(TEXT::Localize("ui.vfx.effects.paste").c_str())) (void)PasteLayerEffects(layer.id, statusMessage_);
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("EffectMenu")) {
            ImGui::InputTextWithHint("##EffectSearch", TEXT::Localize("ui.vfx.effects.search").c_str(), effectSearch_, sizeof(effectSearch_));
            const auto matches = [this](std::string_view value) {
                const std::string_view query{effectSearch_};
                return std::search(value.begin(), value.end(), query.begin(), query.end(),
                    [](unsigned char a, unsigned char b) { return std::tolower(a) == std::tolower(b); }) != value.end();
            };
            bool any = false;
            ImGui::BeginChild("EffectChoices", ImVec2(320, 360));
            for (uint32_t c = 0; c < static_cast<uint32_t>(MaterialEffectCategory::Count); ++c) {
                const auto category = static_cast<MaterialEffectCategory>(c);
                const auto categoryName = TEXT::Localize("ui.vfx.effects.category." + std::string(MaterialEffectCategoryName(category)));
                bool heading = false;
                for (size_t i = 0; i < GetMaterialEffectKindNames().size(); ++i) {
                    const auto kind = static_cast<MaterialEffectKind>(i);
                    if (GetMaterialEffectCategory(kind) != category ||
                        (adjustment ? !IsAdjustmentMaterialEffect(kind) : false)) continue;
                    const auto name = TEXT::Localize("ui.vfx.effect." + std::string(MaterialEffectName(kind)));
                    if (effectSearch_[0] && !matches(name) && !matches(MaterialEffectName(kind)) && !matches(categoryName)) continue;
                    if (!heading) { ImGui::SeparatorText(categoryName.c_str()); heading = true; }
                    any = true;
                    if (ImGui::Selectable(name.c_str())) {
                        (void)AddLayerEffect(layer.id, kind, statusMessage_);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            if (!any) ImGui::TextDisabled("%s", TEXT::Localize("ui.vfx.effects.noResults").c_str());
            ImGui::EndChild();
            ImGui::EndPopup();
        }
        ImGui::TextWrapped("%s", TEXT::Localize("ui.vfx.effects.localUnits").c_str());
        std::vector<const MaterialEffectStep*> ordered;
        for (const auto& effect : layer.effects) ordered.push_back(&effect);
        std::stable_sort(ordered.begin(), ordered.end(), [&](const auto* a, const auto* b) {
                return EffectOrder(*a, adjustment) < EffectOrder(*b, adjustment);
            });
        std::optional<uint32_t> lastStage;
        for (const auto* entry : ordered) {
            const auto& effect = *entry;
            const auto stage = effect.space == MaterialEffectSpace::Group3D ? 5u :
                (adjustment && IsCompositeAdjustmentEffect(effect) ? 6u : uint32_t(GetMaterialEffectStage(effect.kind)));
            if (lastStage != stage) {
                ImGui::SeparatorText(TEXT::Localize("ui.vfx.effects.stage." + std::to_string(static_cast<int>(stage))).c_str());
                lastStage = stage;
            }
            ImGui::PushID(effect.id.c_str());
            VfxInspectorPropertyEditor edit(*this, database, effect.id, statusMessage_);
            const auto label = TEXT::Localize("ui.vfx.effect." + std::string(MaterialEffectName(effect.kind)));
            const bool open = ImGui::TreeNodeEx("settings", ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str());
            if (ImGui::BeginPopupContextItem()) {
                const auto position = std::find(ordered.begin(), ordered.end(), entry);
                const auto compatible = [&](const auto* adjacent) { return EffectOrder(*adjacent, adjustment) == EffectOrder(effect, adjustment); };
                const bool canUp = position != ordered.begin() && compatible(*std::prev(position));
                const bool canDown = std::next(position) != ordered.end() && compatible(*std::next(position));
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.up").c_str(), nullptr, false, canUp)) (void)MoveLayerEffect(layer.id, effect.id, -1, statusMessage_);
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.down").c_str(), nullptr, false, canDown)) (void)MoveLayerEffect(layer.id, effect.id, 1, statusMessage_);
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.reset").c_str())) (void)ResetLayerEffect(layer.id,effect.id,statusMessage_);
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.remove").c_str())) (void)RemoveLayerEffect(layer.id, effect.id, statusMessage_);
                ImGui::EndPopup();
            }
            if (open) {
                edit.Bool(TEXT::Localize("ui.vfx.effects.enabled").c_str(), "enabled", effect.enabled);
                if (SupportsSpatialMaterialEffect(effect.kind)) {
                    const auto current = TEXT::Localize(effect.space == MaterialEffectSpace::Group3D ? "ui.vfx.effects.space.group" : "ui.vfx.effects.space.uv");
                    if (ImGui::BeginCombo(TEXT::Localize("ui.vfx.effects.space").c_str(), current.c_str())) {
                        for (unsigned mode=0;mode<2;++mode) {
                            if (ImGui::Selectable(TEXT::Localize(mode ? "ui.vfx.effects.space.group" : "ui.vfx.effects.space.uv").c_str(), unsigned(effect.space)==mode)) {
                                auto next=document_.Asset();
                                auto* owner=FindEmitterMutable(next,layer.id);
                                if (auto* edited=owner?FindEffectMutable(*owner,effect.id):nullptr) {
                                    edited->space=static_cast<MaterialEffectSpace>(mode);
                                    if (mode && edited->mask>MaterialEffectMask::Vertical) edited->mask=MaterialEffectMask::None;
                                    (void)ApplyAsset(std::move(next),statusMessage_);
                                }
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (effect.space==MaterialEffectSpace::Group3D)
                        ImGui::TextWrapped("%s",TEXT::Localize("ui.vfx.effects.space.help").c_str());
                }
                DrawEffectBlend(database,layer,effect);
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
                const auto drawSlots = [&](const std::array<MaterialEffectParameterInfo, 4>& table, const char* slot,
                                           const MATH::Vec4& value) {
                    float values[] = {value.x, value.y, value.z, value.w};
                    bool changed = false;
                    bool seal = false;
                    for (size_t p = 0; p < 4 && table[p].name; ++p) {
                        const auto name = TEXT::Localize("ui.vfx.effect.param." + std::string(table[p].name));
                        const std::string property=std::string(slot)+"."+"xyzw"[p];
                        if (DrawLayerParameter(layer,effect.id,property)) continue;
                        changed |= ImGui::DragFloat(name.c_str(), &values[p], (table[p].maximum-table[p].minimum)*0.002f, table[p].minimum, table[p].maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
                        seal |= ImGui::IsItemDeactivatedAfterEdit();
                    }
                    if (changed) (void)SetProperty(effect.id, slot, {values[0],values[1],values[2],values[3]}, statusMessage_, VfxInspectorPropertyEditor::MergeGroup(effect.id, slot), &database);
                    if (seal) SealPropertyEdit();
                };
                drawSlots(GetMaterialEffectParameters(effect.kind), "parameters", effect.parameters);
                drawSlots(GetMaterialEffectParameters(effect.kind, true), "parameters2", effect.parameters2);
                if (effect.kind==MaterialEffectKind::Dissolve || effect.kind==MaterialEffectKind::Wipe)
                    (void)DrawLayerParameter(layer,effect.id,"progress");
                if (MaterialEffectUsesColor(effect.kind))
                    edit.Color(TEXT::Localize("ui.vfx.effects.colorA").c_str(), "colorA", effect.colorA);
                if (MaterialEffectUsesColor(effect.kind, true))
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
