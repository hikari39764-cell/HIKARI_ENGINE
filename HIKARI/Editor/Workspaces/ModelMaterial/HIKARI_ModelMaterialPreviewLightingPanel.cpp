#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cmath>

#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

void ModelMaterialWorkspaceController::DrawPreviewLightingPanel(DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
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
        const auto applyPreset = [this](float ambient, float skyExposure, float reflection, float keyIntensity) {
            previewEnvironment_.ambient.intensity = ambient;
            previewEnvironment_.sky.exposure = skyExposure;
            previewEnvironment_.sky.reflectionIntensity = reflection;
            if (!previewLighting_.lights.empty()) {
                previewLighting_.lights.front().intensity = keyIntensity;
            }
            MarkPreviewLightingChanged();
        };
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.dark.studio.9df0ca", "Dark Studio").c_str())) {
            applyPreset(0.15f, 0.40f, 0.15f, 2.2f);
        }
        NextToolbarItem(ImGui::CalcTextSize(TEXT::LocalizeCStr("editor.ui.neutral.studio.17e540")).x +
                        ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.neutral.studio.17e540", "Neutral Studio").c_str())) {
            applyPreset(0.35f, 1.00f, 0.45f, 3.0f);
        }
        NextToolbarItem(ImGui::CalcTextSize(TEXT::LocalizeCStr("editor.ui.bright.studio.6d3c4a")).x +
                        ImGui::GetStyle().FramePadding.x * 2);
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.bright.studio.6d3c4a", "Bright Studio").c_str())) {
            applyPreset(0.55f, 1.60f, 0.80f, 4.2f);
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
        changed = ImGui::DragFloat(TEXT::LocalizeWindow("ui.quality.intensity", "Intensity").c_str(), &light.intensity,
                                   0.05f, 0.0f, 64.0f) ||
                  changed;
        if (light.type == RENDER3D::LIGHTING::SceneLightType::Point) {
            changed = ImGui::DragFloat(TEXT::LocalizeWindow("ui.vfx.text.Range", "Range").c_str(), &light.range, 0.10f,
                                       0.1f, 200.0f) ||
                      changed;
            changed = ImGui::DragFloat3(TEXT::LocalizeWindow("ui.vfx.text.Position", "Position").c_str(),
                                        &light.position.x, 0.05f) ||
                      changed;
        } else if (light.type == RENDER3D::LIGHTING::SceneLightType::Directional) {
            MATH::Vec3 direction = MATH::Length(light.direction) > 1.0e-5f ? MATH::Normalize(light.direction)
                                                                           : MATH::Vec3{0.0f, -1.0f, 0.0f};
            float yawDegrees = std::atan2(direction.x, direction.z) * 57.2957795131f;
            float pitchDegrees = std::asin(std::clamp(direction.y, -1.0f, 1.0f)) * 57.2957795131f;
            bool directionChanged = false;
            directionChanged = ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.yaw.deg.ede749", "Yaw (deg)").c_str(),
                                                &yawDegrees, 0.5f, -180.0f, 180.0f, "%.1f") ||
                               directionChanged;
            directionChanged =
                ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.pitch.deg.7d380b", "Pitch (deg)").c_str(),
                                 &pitchDegrees, 0.5f, -89.0f, 89.0f, "%.1f") ||
                directionChanged;
            if (directionChanged) {
                const float yawRad = yawDegrees * 0.0174532925f;
                const float pitchRad = pitchDegrees * 0.0174532925f;
                const float cosinePitch = std::cos(pitchRad);
                light.direction = MATH::Normalize({
                    std::sin(yawRad) * cosinePitch,
                    std::sin(pitchRad),
                    std::cos(yawRad) * cosinePitch,
                });
                changed = true;
            }
        }
        if (ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.key.light.sync.f25d49", "keyLightSync").c_str())) {
            const SceneEnvironment& environment = scene.GetSceneEnvironment();
            if (environment.directional.enabled && MATH::Length(environment.directional.direction) > 1.0e-5f) {
                if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.sync.key.with.runtime.directional.635975",
                                                       "Sync Key with Runtime Directional")
                                      .c_str())) {
                    light.type = RENDER3D::LIGHTING::SceneLightType::Directional;
                    light.direction = MATH::Normalize(environment.directional.direction);
                    light.color = environment.directional.color;
                    light.intensity = environment.directional.intensity;
                    changed = true;
                }
            } else {
                ImGui::TextDisabled(
                    TEXT::Localize("editor.ui.scene.has.no.enabled.directional.light.to.sync.214d79").c_str());
            }
        }
        if (changed) {
            MarkPreviewLightingChanged();
        }
    }
    ImGui::End();
#else
    (void)scene;
#endif
}

} // namespace HIKARI::EDITOR
