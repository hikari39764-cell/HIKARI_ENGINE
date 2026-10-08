#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "Editor/Style/HIKARI_EditorGlyphs.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {

#if defined(HIKARI_WITH_EDITOR)
bool ProjectPreviewPoint(const Camera3D& camera, const MATH::Vec3& point, const ImVec2& origin, const ImVec2& size,
                         ImVec2& outScreen) {
    const MATH::Vec4 clip = camera.GetViewProj().TransformPoint({point.x, point.y, point.z, 1.0f});
    if (clip.w <= 1.0e-5f) {
        return false;
    }
    const float inverseW = 1.0f / clip.w;
    const float ndcX = clip.x * inverseW;
    const float ndcY = clip.y * inverseW;
    const float ndcZ = clip.z * inverseW;
    if (ndcZ < 0.0f || ndcZ > 1.0f) {
        return false;
    }
    outScreen = {origin.x + (ndcX * 0.5f + 0.5f) * size.x, origin.y + (-ndcY * 0.5f + 0.5f) * size.y};
    constexpr float kMarkerMargin = 18.0f;
    return outScreen.x >= origin.x + kMarkerMargin && outScreen.x <= origin.x + size.x - kMarkerMargin &&
           outScreen.y >= origin.y + kMarkerMargin && outScreen.y <= origin.y + size.y - kMarkerMargin;
}

ImU32 PreviewLightColor(const RENDER3D::LIGHTING::SceneLightProvider& light) {
    if (!light.enabled) {
        return IM_COL32(132, 142, 154, 220);
    }
    const auto toByte = [](float value) { return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f); };
    return IM_COL32(toByte(light.color.x), toByte(light.color.y), toByte(light.color.z), 245);
}

bool DrawStudioLightGlyph(ImDrawList& drawList, const Camera3D& camera,
                          const RENDER3D::LIGHTING::SceneLightProvider& light, const ImVec2& origin, const ImVec2& size,
                          bool selected) {
    ImVec2 screen{};
    if (!ProjectPreviewPoint(camera, light.position, origin, size, screen)) {
        return false;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float deltaX = mouse.x - screen.x;
    const float deltaY = mouse.y - screen.y;
    const bool hovered = deltaX * deltaX + deltaY * deltaY <= 18.0f * 18.0f;
    const ImU32 color = PreviewLightColor(light);
    const ImU32 background = hovered || selected ? IM_COL32(25, 34, 44, 245) : IM_COL32(17, 23, 31, 220);
    drawList.AddCircleFilled(screen, 15.0f, background, 20);
    drawList.AddCircle(screen, hovered || selected ? 16.0f : 15.0f,
                       hovered || selected ? IM_COL32(255, 210, 112, 255) : IM_COL32(169, 183, 199, 220), 20,
                       hovered || selected ? 2.0f : 1.0f);
    DrawEditorGlyph(drawList, EditorGlyph::Light, screen, 20.0f, color, 1.8f);
    if (hovered || selected) {
        const std::string label = light.name + (light.enabled ? "" : " (disabled)");
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        const ImVec2 labelMin{screen.x + 21.0f, screen.y - 12.0f};
        const ImVec2 labelMax{labelMin.x + textSize.x + 12.0f, labelMin.y + textSize.y + 8.0f};
        drawList.AddRectFilled(labelMin, labelMax, IM_COL32(9, 13, 18, 225), 4.0f);
        drawList.AddRect(labelMin, labelMax, color, 4.0f, 0, 1.0f);
        drawList.AddText({labelMin.x + 6.0f, labelMin.y + 4.0f}, color, label.c_str());
    }
    if (hovered) {
        ImGui::SetTooltip(TEXT::Localize("editor.ui.s.nclick.to.select.and.manipulate.in.the.viewport.57aeff").c_str(),
                          light.name.c_str());
    }
    return hovered;
}

void DrawProjectedCircle(ImDrawList& drawList, const Camera3D& camera, const MATH::Vec3& center,
                         const MATH::Vec3& axisA, const MATH::Vec3& axisB, float radius, const ImVec2& origin,
                         const ImVec2& size, ImU32 color) {
    constexpr int kSegments = 48;
    ImVec2 previous{};
    bool hasPrevious = false;
    for (int segment = 0; segment <= kSegments; ++segment) {
        const float angle = static_cast<float>(segment) * 6.28318530718f / static_cast<float>(kSegments);
        const MATH::Vec3 point = center + axisA * (std::cos(angle) * radius) + axisB * (std::sin(angle) * radius);
        ImVec2 screen{};
        if (!ProjectPreviewPoint(camera, point, origin, size, screen)) {
            hasPrevious = false;
            continue;
        }
        if (hasPrevious) {
            drawList.AddLine(previous, screen, color, 1.25f);
        }
        previous = screen;
        hasPrevious = true;
    }
}

bool DrawSelectedLightSpatialOverlay(ImDrawList& drawList, const Camera3D& camera,
                                     RENDER3D::LIGHTING::SceneLightProvider& light, const ImVec2& origin,
                                     const ImVec2& size, bool& rangeDragging) {
    const ImU32 color = IM_COL32(255, 205, 94, 190);
    if (light.type != RENDER3D::LIGHTING::SceneLightType::Point) {
        rangeDragging = false;
    }
    if (light.type == RENDER3D::LIGHTING::SceneLightType::Point) {
        const MATH::Vec3 forward = MATH::Normalize(camera.GetTarget() - camera.GetPosition());
        const MATH::Vec3 rightCandidate = MATH::Cross(forward, camera.GetUp());
        const MATH::Vec3 right =
            MATH::Length(rightCandidate) > 1.0e-5f ? MATH::Normalize(rightCandidate) : MATH::Vec3{1.0f, 0.0f, 0.0f};
        DrawProjectedCircle(drawList, camera, light.position, right, camera.GetUp(), (std::max)(0.01f, light.range),
                            origin, size, color);
        DrawProjectedCircle(drawList, camera, light.position, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
                            (std::max)(0.01f, light.range), origin, size, IM_COL32(255, 205, 94, 110));
        ImVec2 rangeHandle{};
        if (ProjectPreviewPoint(camera, light.position + right * light.range, origin, size, rangeHandle)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float dx = mouse.x - rangeHandle.x;
            const float dy = mouse.y - rangeHandle.y;
            const bool hovered = dx * dx + dy * dy <= 11.0f * 11.0f;
            drawList.AddCircleFilled(rangeHandle, hovered ? 8.0f : 6.0f, IM_COL32(255, 205, 94, 255), 16);
            drawList.AddCircle(rangeHandle, hovered ? 10.0f : 8.0f, IM_COL32(18, 24, 31, 255), 16, 2.0f);
            if (hovered) {
                ImGui::SetTooltip(
                    TEXT::Localize("editor.ui.drag.horizontally.to.change.point.light.range.9da5fd").c_str());
            }
            if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                rangeDragging = true;
            }
            if (rangeDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                light.range = (std::max)(0.05f, light.range + ImGui::GetIO().MouseDelta.x *
                                                                  (std::max)(light.range, 1.0f) * 0.0125f);
            }
            if (rangeDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                rangeDragging = false;
            }
            return hovered || rangeDragging;
        }
    }
    return false;
}
#endif

} // namespace

void ModelMaterialWorkspaceController::DrawPreviewLightingProperties() {
#if defined(HIKARI_WITH_EDITOR)
    if (!ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.ui.preview.lighting.87cd64", "Preview Lighting").c_str(),
                                 ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.studio.lights.are.workspace.only.they.use.the.runtime.light.cont.6d18be").c_str());
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.point.light.a8134e", "+ Point Light").c_str())) {
        AddPreviewLight();
    }
    for (size_t index = 0u; index < previewLighting_.lights.size(); ++index) {
        auto& light = previewLighting_.lights[index];
        ImGui::PushID(static_cast<int>(index));
        const bool selected = selectedPreviewLightIndex_ == index;
        if (ImGui::Selectable(light.name.c_str(), selected)) {
            previewLightRangeDragActive_ = false;
            selectedPreviewLightIndex_ = index;
            previewEnvironmentMode_ = PreviewEnvironmentMode::StudioInspect;
            previewLightGizmoState_.operation = EditorTransformGizmoOperation::Translate;
        }
        ImGui::PopID();
    }
    if (!selectedPreviewLightIndex_.has_value() || *selectedPreviewLightIndex_ >= previewLighting_.lights.size()) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.select.a.light.here.or.click.its.viewport.icon.6868f7").c_str());
        return;
    }

    auto& light = previewLighting_.lights[*selectedPreviewLightIndex_];
    ImGui::SeparatorText(TEXT::Localize("editor.ui.selected.light.67da0b").c_str());
    ImGui::Checkbox(TEXT::LocalizeWindow("ui.vfx.text.Enabled", "Enabled").c_str(), &light.enabled);
    ImGui::DragFloat3(TEXT::LocalizeWindow("ui.vfx.text.Position", "Position").c_str(), &light.position.x, 0.05f);
    ImGui::DragFloat(TEXT::LocalizeWindow("ui.vfx.text.Range", "Range").c_str(), &light.range, 0.05f, 0.05f, 10000.0f,
                     "%.2f");
    ImGui::TextDisabled(
        TEXT::Localize("editor.ui.the.gold.sphere.in.the.viewport.is.the.point.light.range.77d37e").c_str());
    ImGui::ColorEdit3(TEXT::LocalizeWindow("ui.vfx.text.Color", "Color").c_str(), &light.color.x,
                      ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
    ImGui::DragFloat(TEXT::LocalizeWindow("editor.ui.light.lumens", "Luminous Flux (lm)").c_str(), &light.intensity,
                     100.0f, 0.0f, 1000000.0f, "%.0f");
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.reset.around.preview.3f2318", "Reset Around Preview").c_str())) {
        ResetPreviewKeyLight();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.delete.light.7d387e", "Delete Light").c_str())) {
        DeleteSelectedPreviewLight();
    }
#endif
}

ModelMaterialWorkspaceController::PreviewLightViewportInteraction
ModelMaterialWorkspaceController::DrawPreviewLightViewportInteraction(float originX, float originY, float width,
                                                                      float height) {
    PreviewLightViewportInteraction interaction{};
#if defined(HIKARI_WITH_EDITOR)
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec2 origin{originX, originY};
    const ImVec2 size{width, height};
    const Camera3D& camera = cameraController_.GetCamera();
    for (size_t index = 0u; index < previewLighting_.lights.size(); ++index) {
        const bool selected = selectedPreviewLightIndex_ == index;
        if (selected) {
            const float previousRange = previewLighting_.lights[index].range;
            const bool rangeControl = DrawSelectedLightSpatialOverlay(drawList, camera, previewLighting_.lights[index],
                                                                      origin, size, previewLightRangeDragActive_);
            if (previewLighting_.lights[index].range != previousRange) {
                MarkPreviewLightingChanged();
            }
            interaction.glyphHovered = interaction.glyphHovered || rangeControl;
            interaction.gizmoInteracting =
                interaction.gizmoInteracting || (rangeControl && ImGui::IsMouseDown(ImGuiMouseButton_Left));
        }
        const bool hovered =
            DrawStudioLightGlyph(drawList, camera, previewLighting_.lights[index], origin, size, selected);
        interaction.glyphHovered = interaction.glyphHovered || hovered;
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            previewLightRangeDragActive_ = false;
            selectedPreviewLightIndex_ = index;
            previewLightGizmoState_.operation = EditorTransformGizmoOperation::Translate;
        }
    }

    if (!selectedPreviewLightIndex_.has_value() || *selectedPreviewLightIndex_ >= previewLighting_.lights.size()) {
        return interaction;
    }
    auto& light = previewLighting_.lights[*selectedPreviewLightIndex_];
    previewLightGizmoState_.operation = EditorTransformGizmoOperation::Translate;
    TransformData transform{};
    transform.position = light.position;
    transform.scale = {1.0f, 1.0f, 1.0f};
    const EditorTransformGizmoResult result = previewLightGizmo_.DrawTransform(
        transform, camera, previewLightGizmoState_, EditorViewportRect{originX, originY, width, height});
    interaction.gizmoInteracting = result.interacting;
    if (result.changed) {
        light.position = result.transform.position;
        MarkPreviewLightingChanged();
    }
#else
    (void)originX;
    (void)originY;
    (void)width;
    (void)height;
#endif
    return interaction;
}

} // namespace HIKARI::EDITOR
