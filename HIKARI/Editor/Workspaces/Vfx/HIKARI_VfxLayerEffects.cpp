#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    bool VfxWorkspaceController::AddLayerEffect(std::string_view layerId,
        MaterialEffectKind kind, std::string& message) {
        auto next = document_.Asset();
        auto* layer = FindEmitterMutable(next, layerId);
        if (!layer || layer->kind == VFX::VfxLayerKind::Group || layer->effects.size() >= 8 ||
            MaterialEffectName(kind) == "Invalid" || VFX::IsCustomVfxMaterial(layer->renderCommon.materialMode)) {
            message = "Choose a texture-based shape/particle layer with fewer than eight effects";
            return false;
        }
        layer->effects.push_back(MakeMaterialEffect(kind, AllocateStableId("effect")));
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
        std::iter_swap(it, it + direction);
        return ApplyAsset(std::move(next), message);
    }

    void VfxWorkspaceController::DrawLayerEffects(AssetDatabase& database, const VFX::VfxEmitterData& layer) {
#if defined(HIKARI_WITH_EDITOR)
        ImGui::SeparatorText(TEXT::Localize("ui.vfx.effects").c_str());
        if (VFX::IsCustomVfxMaterial(layer.renderCommon.materialMode)) {
            ImGui::TextWrapped("%s", TEXT::Localize("ui.vfx.effects.custom").c_str()); return;
        }
        ImGui::BeginDisabled(layer.effects.size() >= 8);
        if (ImGui::Button(TEXT::Localize("ui.vfx.effects.add").c_str())) ImGui::OpenPopup("EffectMenu");
        ImGui::EndDisabled();
        if (ImGui::BeginPopup("EffectMenu")) {
            for (int i = 0; i < 8; ++i) {
                const auto kind = static_cast<MaterialEffectKind>(i);
                const auto key = "ui.vfx.effect." + std::string(MaterialEffectName(kind));
                if (ImGui::MenuItem(TEXT::Localize(key).c_str())) (void)AddLayerEffect(layer.id, kind, statusMessage_);
            }
            ImGui::EndPopup();
        }
        constexpr const char* parameterNames[8][4] = {
            {"speedU", "speedV", "repeatU", "repeatV"}, {"strength", "noiseScale", "speed", nullptr},
            {nullptr}, {"fadeIn", "fadeOut", nullptr}, {"start", "end", "softness", "axis"},
            {"start", "end", "noiseScale", "edgeWidth"}, {"intensity", "pulse", "frequency", nullptr},
            {"power", "intensity", "rimOpacity", nullptr}
        };
        for (const auto& effect : layer.effects) {
            ImGui::PushID(effect.id.c_str());
            VfxInspectorPropertyEditor edit(*this, database, effect.id, statusMessage_);
            const auto label = TEXT::Localize("ui.vfx.effect." + std::string(MaterialEffectName(effect.kind)));
            const bool open = ImGui::TreeNodeEx("settings", ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str());
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.up").c_str())) (void)MoveLayerEffect(layer.id, effect.id, -1, statusMessage_);
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.down").c_str())) (void)MoveLayerEffect(layer.id, effect.id, 1, statusMessage_);
                if (ImGui::MenuItem(TEXT::Localize("ui.vfx.effects.remove").c_str())) (void)RemoveLayerEffect(layer.id, effect.id, statusMessage_);
                ImGui::EndPopup();
            }
            if (open) {
                edit.Bool(TEXT::Localize("ui.vfx.effects.enabled").c_str(), "enabled", effect.enabled);
                auto parameters = effect.parameters;
                float values[] = {parameters.x, parameters.y, parameters.z, parameters.w};
                bool changed = false;
                const auto index = static_cast<size_t>(effect.kind);
                for (size_t p = 0; p < 4 && parameterNames[index][p]; ++p) {
                    const auto name = TEXT::Localize("ui.vfx.effect.param." + std::string(parameterNames[index][p]));
                    const std::string property=std::string("parameters.")+"xyzw"[p];
                    if (DrawLayerParameter(layer,effect.id,property)) continue;
                    changed |= ImGui::DragFloat(name.c_str(), &values[p], 0.01f);
                    if (ImGui::IsItemDeactivatedAfterEdit()) SealPropertyEdit();
                }
                if (changed) (void)SetProperty(effect.id, "parameters", {values[0],values[1],values[2],values[3]}, statusMessage_, VfxInspectorPropertyEditor::MergeGroup(effect.id, "parameters"), &database);
                if (effect.kind==MaterialEffectKind::Dissolve || effect.kind==MaterialEffectKind::Wipe)
                    (void)DrawLayerParameter(layer,effect.id,"progress");
                if (effect.kind == MaterialEffectKind::Gradient || effect.kind == MaterialEffectKind::Dissolve ||
                    effect.kind == MaterialEffectKind::Emission || effect.kind == MaterialEffectKind::Fresnel)
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
