#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include "Editor/Style/HIKARI_EditorWidgets.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawPreviewLightingPanel() {
    if (!previewLightingPanelOpen_) {
        return;
    }
    if (!ImGui::Begin(
            TEXT::LocalizeWindow("editor.ui.preview.lighting.87cd64", "ModelMaterial/PreviewLighting").c_str(),
            &previewLightingPanelOpen_)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.studio.inspect.light.rig.scene.match.and.runtime.asset.lighting..431367").c_str());
    if (ImGui::CollapsingHeader(
            TEXT::LocalizeWindow("editor.ui.environment.presets.3b220a", "Environment Presets").c_str(),
            ImGuiTreeNodeFlags_DefaultOpen)) {
        // 天空光の倍率、反射の倍率、太陽の照度 [lux]。
        const auto applyPreset = [this](float skyLight, float reflection, float sunIlluminance) {
            previewProfile_.sky.lightIntensity = skyLight;
            previewProfile_.sky.reflectionIntensity = reflection;
            previewProfile_.sun.illuminance = sunIlluminance;
            RebuildPreviewEnvironment();
        };
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.dark.studio.9df0ca", "Dark Studio").c_str())) {
            applyPreset(0.4f, 0.15f, 20000.0f);
        }
        NextToolbarItem(ImGui::CalcTextSize(TEXT::Localize("editor.ui.neutral.studio.17e540").c_str()).x +
                        ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.neutral.studio.17e540", "Neutral Studio").c_str())) {
            applyPreset(1.0f, 0.45f, 60000.0f);
        }
        NextToolbarItem(ImGui::CalcTextSize(TEXT::Localize("editor.ui.bright.studio.6d3c4a").c_str()).x +
                        ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.bright.studio.6d3c4a", "Bright Studio").c_str())) {
            applyPreset(1.6f, 0.8f, 100000.0f);
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.reset.key.light.655b13", "Reset Key Light").c_str())) {
            ResetPreviewKeyLight();
        }
    }
    if (!previewLighting_.lights.empty()) {
        const size_t lightIndex =
            selectedPreviewLightIndex_.has_value() && *selectedPreviewLightIndex_ < previewLighting_.lights.size()
                ? *selectedPreviewLightIndex_
                : 0u;
        auto& light = previewLighting_.lights[lightIndex];
        bool changed = false;
        ImGui::SeparatorText(TEXT::Localize("editor.ui.selected.light.67da0b").c_str());
        ImGui::TextUnformatted(light.name.c_str());
        changed =
            ImGui::Checkbox(TEXT::LocalizeWindow("ui.vfx.text.Enabled", "Enabled").c_str(), &light.enabled) || changed;
        changed =
            ImGui::ColorEdit3(TEXT::LocalizeWindow("ui.vfx.text.Color", "Color").c_str(), &light.color.x) || changed;
        changed = ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.light.lumens", "Luminous Flux (lm)").c_str(),
                                   &light.intensity, 100.0f, 0.0f, 1000000.0f, "%.0f") ||
                  changed;
        changed = ImGui::DragFloat(TEXT::LocalizeWindow("ui.vfx.text.Range", "Range").c_str(), &light.range, 0.10f,
                                   0.1f, 200.0f) ||
                  changed;
        changed = ImGui::DragFloat3(TEXT::LocalizeWindow("ui.vfx.text.Position", "Position").c_str(), &light.position.x,
                                    0.05f) ||
                  changed;
        if (changed) {
            MarkPreviewLightingChanged();
        }
    }
    ImGui::End();
}

} // namespace HIKARI::EDITOR
