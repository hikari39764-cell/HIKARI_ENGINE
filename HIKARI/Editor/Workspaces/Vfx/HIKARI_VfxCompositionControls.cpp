#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
bool VfxWorkspaceController::ResetLayerEffect(std::string_view layerId, std::string_view effectId,
                                              std::string& message) {
    auto next = document_.Asset();
    auto* layer = FindEmitterMutable(next, layerId);
    if (!layer) {
        message = "Layer not found";
        return false;
    }
    auto effect =
        std::find_if(layer->effects.begin(), layer->effects.end(), [&](const auto& e) { return e.id == effectId; });
    if (effect == layer->effects.end()) {
        message = "Effect not found";
        return false;
    }
    const auto space = effect->space;
    *effect = MakeMaterialEffect(effect->kind, effect->id);
    effect->space = space;
    std::erase_if(layer->parameterTracks, [&](const auto& track) { return track.targetId == effectId; });
    return ApplyAsset(std::move(next), message);
}
void VfxWorkspaceController::DrawEffectBlend(AssetDatabase& database, const VFX::VfxEmitterData& layer,
                                             const MaterialEffectStep& effect) {
#if defined(HIKARI_WITH_EDITOR)
    (void)DrawLayerParameter(layer, effect.id, "mix");
    if (ImGui::TreeNode(TEXT::Localize("ui.vfx.effects.mask").c_str())) {
        VfxInspectorPropertyEditor edit(*this, database, effect.id, statusMessage_);
        const auto label = [](unsigned mode) { return TEXT::Localize("ui.vfx.effects.mask." + std::to_string(mode)); };
        if (ImGui::BeginCombo("##MaskMode", label(unsigned(effect.mask)).c_str())) {
            const auto count = GetMaterialEffectMasks(effect.space).size();
            for (unsigned i = 0; i < count; ++i)
                if (ImGui::Selectable(label(i).c_str(), i == unsigned(effect.mask)))
                    (void)SetProperty(effect.id, "mask", i, statusMessage_);
            ImGui::EndCombo();
        }
        if (effect.mask != MaterialEffectMask::None) {
            edit.Bool(TEXT::Localize("ui.vfx.matte.invert").c_str(), "maskInvert", effect.maskInvert);
            auto p = effect.maskParameters;
            bool changed = false, seal = false;
            const auto field = [&](const char* name, float& v, float lo, float hi) {
                changed |= ImGui::DragFloat(TEXT::Localize(name).c_str(), &v, 0.005f, lo, hi, "%.3f",
                                            ImGuiSliderFlags_AlwaysClamp);
                seal |= ImGui::IsItemDeactivatedAfterEdit();
            };
            if (effect.mask == MaterialEffectMask::Radial) {
                field("ui.vfx.effect.param.centerU", p.x, -2, 2);
                field("ui.vfx.effect.param.centerV", p.y, -2, 2);
            }
            if (effect.mask <= MaterialEffectMask::Vertical) {
                field("ui.vfx.effects.mask.extent", p.z, 0, 2);
                field("ui.vfx.effects.mask.feather", p.w, 0, 2);
            }
            if (changed)
                (void)SetProperty(effect.id, "maskParameters", {p.x, p.y, p.z, p.w}, statusMessage_,
                                  VfxInspectorPropertyEditor::MergeGroup(effect.id, "maskParameters"));
            if (seal)
                SealPropertyEdit();
        }
        ImGui::TreePop();
    }
#else
    (void)database;
    (void)layer;
    (void)effect;
#endif
}
void VfxWorkspaceController::DrawLayerMatte(AssetDatabase& database, const VFX::VfxEmitterData& layer) {
#if defined(HIKARI_WITH_EDITOR)
    if (layer.kind == VFX::VfxLayerKind::Adjustment)
        return;
    if (!ImGui::TreeNode(TEXT::Localize("ui.vfx.matte").c_str()))
        return;
    const auto none = TEXT::Localize("ui.vfx.effects.mask.0");
    const auto* source = FindEmitter(layer.matte.sourceLayerId);
    std::optional<std::string> selected;
    if (ImGui::BeginCombo("##MatteSource", source ? source->name.c_str() : none.c_str())) {
        if (ImGui::Selectable(none.c_str(), !source))
            selected = std::string{};
        for (const auto& candidate : document_.Asset().emitters) {
            if (candidate.id == layer.id || candidate.parentEmitterId != layer.parentEmitterId ||
                candidate.kind == VFX::VfxLayerKind::Adjustment)
                continue;
            ImGui::PushID(candidate.id.c_str());
            if (ImGui::Selectable(candidate.name.c_str(), candidate.id == layer.matte.sourceLayerId))
                selected = candidate.id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    if (selected)
        (void)SetProperty(layer.id, "matte.sourceLayerId", *selected, statusMessage_);
    VfxInspectorPropertyEditor edit(*this, database, layer.id, statusMessage_);
    if (source) {
        edit.Bool(TEXT::Localize("ui.vfx.matte.luminance").c_str(), "matte.luminance", layer.matte.luminance);
        edit.Bool(TEXT::Localize("ui.vfx.matte.invert").c_str(), "matte.inverted", layer.matte.inverted);
    }
    if (layer.kind != VFX::VfxLayerKind::Adjustment)
        edit.Bool(TEXT::Localize("ui.vfx.matte.only").c_str(), "matte.sourceOnly", layer.matte.sourceOnly);
    ImGui::TreePop();
#else
    (void)database;
    (void)layer;
#endif
}
} // namespace HIKARI::EDITOR
