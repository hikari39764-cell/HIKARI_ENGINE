#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorViewportPresentation.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <unordered_set>

#include "imgui.h"

namespace HIKARI {

namespace EDITOR {
namespace DOCUMENT_SCENE {

void DrawViewportDebugOptions(ViewportOverlayState& overlays, ViewportPerformanceState& performance) {
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugGrid").c_str(), &overlays.showGrid);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugGridFloor").c_str(), &overlays.showGridFloor);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugAxis").c_str(), &overlays.showAxis);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugLights").c_str(), &overlays.showLights);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugCameras").c_str(), &overlays.showCameras);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugLabels").c_str(), &overlays.showProbeLabels);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugXRayGizmos").c_str(), &overlays.showXRayGizmos);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.cameraPreview").c_str(), &overlays.showCameraPreview);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.viewport.debugLightingShapes").c_str());
    ImGui::Separator();
    ImGui::Checkbox(TEXT::Localize("ui.viewport.disableSsaoInEditor").c_str(),
                    &performance.disableSsaoInEditorViewport);
}

// 視口の選択肢の並び。表示名は ui.debugView.<名前>（名前は kRenderDebugViewNames）。
constexpr RenderDebugView kRenderDebugViewOptions[] = {
    RenderDebugView::None,
    RenderDebugView::BaseColor,
    RenderDebugView::Normal,
    RenderDebugView::Tangent,
    RenderDebugView::Roughness,
    RenderDebugView::Metallic,
    RenderDebugView::Occlusion,
    RenderDebugView::Shadow,
    RenderDebugView::NdotL,
    RenderDebugView::Emissive,
    RenderDebugView::DirectDiffuse,
    RenderDebugView::DirectionalSpecular,
    RenderDebugView::LocalSpecular,
    RenderDebugView::IndirectDiffuse,
    RenderDebugView::IndirectSpecular,
    RenderDebugView::PathTraced,
    RenderDebugView::PathTracedIndirectDiffuse,
    RenderDebugView::SceneDepth,
    RenderDebugView::SceneColor,
    RenderDebugView::MotionVectors,
    RenderDebugView::TemporalHistoryWeight,
    RenderDebugView::TemporalDepthRejection,
    RenderDebugView::TemporalReactiveMask,
    RenderDebugView::TemporalTransparencyMask,
    RenderDebugView::TemporalDisocclusion,
    RenderDebugView::TemporalInvalidDepthMotion,
    RenderDebugView::VolumetricScattering,
    RenderDebugView::VolumetricTransmittance,
    RenderDebugView::VolumetricDepthSlice,
    RenderDebugView::ReflectionProviderWeight,
    RenderDebugView::IrradianceProviderWeight,
    RenderDebugView::LightingFallbackWeight,
    RenderDebugView::SurfaceLightmap,
    RenderDebugView::SsrConfidence,
    RenderDebugView::SsrHit,
    RenderDebugView::MeshletId,
    RenderDebugView::ClusterId,
    RenderDebugView::SurfaceId,
    RenderDebugView::LodLevel,
    RenderDebugView::LodHeat,
    RenderDebugView::DrawBucket,
};

std::string RenderDebugViewLabel(RenderDebugView view) {
    return TEXT::Localize(std::string("ui.debugView.") + TEXT::EnumToName(kRenderDebugViewNames, view));
}

int RenderDebugViewOptionIndex(RenderDebugView view) {
    for (int i = 0; i < static_cast<int>(std::size(kRenderDebugViewOptions)); ++i) {
        if (kRenderDebugViewOptions[i] == view) {
            return i;
        }
    }
    return 0;
}

bool DrawRenderDebugViewCombo(const char* label, ViewportDebugViewState& debugView, float width) {
    const int currentIndex = RenderDebugViewOptionIndex(debugView.renderView);
    int selectedIndex = currentIndex;
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label, RenderDebugViewLabel(kRenderDebugViewOptions[currentIndex]).c_str(),
                          ImGuiComboFlags_NoArrowButton)) {
        ImGui::SeparatorText(TEXT::Localize("ui.debugView.shading").c_str());
        for (int i = 0; i < static_cast<int>(std::size(kRenderDebugViewOptions)); ++i) {
            const RenderDebugView option = kRenderDebugViewOptions[i];
            if (option == RenderDebugView::SceneDepth) {
                ImGui::SeparatorText(TEXT::Localize("ui.debugView.screenSpace").c_str());
            }
            if (option == RenderDebugView::MotionVectors) {
                ImGui::SeparatorText(TEXT::Localize("ui.debugView.temporal").c_str());
            }
            if (option == RenderDebugView::VolumetricScattering) {
                ImGui::SeparatorText(TEXT::Localize("ui.debugView.volumetric").c_str());
            }
            if (option == RenderDebugView::ReflectionProviderWeight) {
                ImGui::SeparatorText(TEXT::Localize("ui.debugView.lightingProviders").c_str());
            }
            if (option == RenderDebugView::MeshletId) {
                ImGui::SeparatorText(TEXT::Localize("ui.debugView.meshlet").c_str());
            }
            const bool selected = (i == selectedIndex);
            if (ImGui::Selectable(RenderDebugViewLabel(option).c_str(), selected)) {
                selectedIndex = i;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (changed) {
        debugView.renderView = kRenderDebugViewOptions[selectedIndex];
    }
    return changed;
}

bool ProjectWorldToViewport(const Camera3D& camera, const MATH::Vec3& worldPosition, const ImVec2& viewportOrigin,
                            const ImVec2& viewportSize, ImVec2& outScreenPosition) {
    const MATH::Vec4 clip =
        camera.GetViewProj().TransformPoint({worldPosition.x, worldPosition.y, worldPosition.z, 1.0f});
    if (std::fabs(clip.w) <= 1.0e-5f) {
        return false;
    }

    const float invW = 1.0f / clip.w;
    const float ndcX = clip.x * invW;
    const float ndcY = clip.y * invW;
    const float ndcZ = clip.z * invW;
    if (ndcZ < 0.0f || ndcZ > 1.0f) {
        return false;
    }

    outScreenPosition.x = viewportOrigin.x + (ndcX * 0.5f + 0.5f) * viewportSize.x;
    outScreenPosition.y = viewportOrigin.y + (-ndcY * 0.5f + 0.5f) * viewportSize.y;
    return outScreenPosition.x >= viewportOrigin.x && outScreenPosition.x <= viewportOrigin.x + viewportSize.x &&
           outScreenPosition.y >= viewportOrigin.y && outScreenPosition.y <= viewportOrigin.y + viewportSize.y;
}

void DrawViewportLabel(ImDrawList* drawList, const ImVec2& position, const char* text, ImU32 color) {
    if (drawList == nullptr || text == nullptr || text[0] == '\0') {
        return;
    }

    const ImVec2 padding{6.0f, 4.0f};
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 min{position.x + 8.0f, position.y - 8.0f};
    const ImVec2 max{min.x + textSize.x + padding.x * 2.0f, min.y + textSize.y + padding.y * 2.0f};
    drawList->AddRectFilled(min, max, IM_COL32(9, 13, 18, 205), 3.0f);
    drawList->AddRect(min, max, color, 3.0f, 0, 1.0f);
    drawList->AddText(ImVec2(min.x + padding.x, min.y + padding.y), color, text);
}

ImU32 ToImGuiLightColor(const MATH::Vec3& color, int alpha) {
    const auto toByte = [](float value) -> int { return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f); };
    return IM_COL32(toByte(color.x), toByte(color.y), toByte(color.z), alpha);
}

namespace {
constexpr float kIconHitRadius = 13.0f;
constexpr ImU32 kIconSelectionColor = IM_COL32(255, 209, 102, 255);

ImU32 FadeColor(ImU32 color, float fade) {
    const float alpha = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFu) * std::clamp(fade, 0.0f, 1.0f);
    return (color & ~IM_COL32_A_MASK) | (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
}

void DrawPointLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color, float scale) {
    const float radius = 6.0f * scale;
    drawList->AddCircleFilled(center, radius, color, 12);
    drawList->AddCircle(center, radius + 2.0f * scale, color, 16, 1.5f);
}

void DrawSpotLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color, float scale) {
    drawList->AddTriangleFilled({center.x, center.y - 7.5f * scale}, {center.x - 7.0f * scale, center.y + 6.5f * scale},
                                {center.x + 7.0f * scale, center.y + 6.5f * scale}, color);
}

void DrawCameraIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color, float scale) {
    const ImVec2 bodyMin{center.x - 7.0f * scale, center.y - 4.5f * scale};
    const ImVec2 bodyMax{center.x + 3.0f * scale, center.y + 4.5f * scale};
    drawList->AddRectFilled(bodyMin, bodyMax, color, 1.5f * scale);
    drawList->AddTriangleFilled({bodyMax.x, center.y}, {center.x + 8.0f * scale, center.y - 4.0f * scale},
                                {center.x + 8.0f * scale, center.y + 4.0f * scale}, color);
}

// 物の前（ワールドの向き）を画面に写した向き。前が画面の外や真正面なら 0。
MATH::Vec2 ProjectedDirection(const Camera3D& camera, const MATH::Vec3& position, const MATH::Vec3& forward,
                              const ImVec2& origin, const ImVec2& size, const ImVec2& screen) {
    ImVec2 tip{};
    if (!ProjectWorldToViewport(camera, position + forward, origin, size, tip)) {
        return {};
    }
    const float dx = tip.x - screen.x, dy = tip.y - screen.y, length = std::sqrt(dx * dx + dy * dy);
    return length > 1.0f ? MATH::Vec2{dx / length, dy / length} : MATH::Vec2{};
}

} // namespace

std::vector<ViewportObjectIcon> CollectObjectOverlayIcons(DocumentSceneBase& scene, const ViewportOverlayState& overlays,
                                                          const ImVec2& viewportOrigin, const ImVec2& viewportSize) {
    std::vector<ViewportObjectIcon> icons{};
    if (!overlays.showLights && !overlays.showCameras) {
        return icons;
    }
    const Camera3D& camera = scene.GetCamera();
    std::unordered_set<uint64_t> hiddenObjectIds{};
    for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
        if (!object.enabled || !object.editorVisible) {
            hiddenObjectIds.insert(object.id.value);
        }
    }
    constexpr size_t kMaxIcons = 160u;
    for (const std::unique_ptr<GameObject>& object : scene.GetWorld().GetObjects()) {
        if (!object || icons.size() >= kMaxIcons || hiddenObjectIds.contains(object->GetDocumentId().value)) {
            continue;
        }
        const LightComponent* light = overlays.showLights ? object->GetComponent<LightComponent>() : nullptr;
        const CameraComponent* cameraComponent = overlays.showCameras ? object->GetComponent<CameraComponent>() : nullptr;
        const bool isLight = light != nullptr && light->IsEnabled();
        if (!isLight && (cameraComponent == nullptr || !cameraComponent->IsEnabled())) {
            continue;
        }
        const MATH::Mat4 world = object->GetTransform().GetWorldMatrix();
        const MATH::Vec3 position{world.m[3][0], world.m[3][1], world.m[3][2]};
        ImVec2 screen{};
        if (!ProjectWorldToViewport(camera, position, viewportOrigin, viewportSize, screen)) {
            continue;
        }
        const MATH::Vec3 forward = MATH::Normalize({world.m[2][0], world.m[2][1], world.m[2][2]});
        ViewportObjectIcon icon{};
        icon.objectId = object->GetDocumentId();
        icon.screen = {screen.x, screen.y};
        // 12 m までは濃く、45 m で最も薄く。
        const MATH::Vec3 toIcon = position - camera.GetPosition();
        icon.fade = std::clamp(1.0f - (std::sqrt(MATH::Dot(toIcon, toIcon)) - 12.0f) / 33.0f, 0.0f, 1.0f);
        char label[144]{};
        if (isLight) {
            using RENDER3D::LIGHTING::SceneLightType;
            const bool spot = light->GetLightType() == SceneLightType::Spot;
            icon.kind = spot ? ViewportObjectIcon::Kind::SpotLight : ViewportObjectIcon::Kind::PointLight;
            icon.color = ToImGuiLightColor(light->GetColor(), 235);
            if (light->GetLightType() != SceneLightType::Point) {
                icon.direction = ProjectedDirection(camera, position, forward, viewportOrigin, viewportSize, screen);
            }
            if (spot) {
                std::snprintf(label, sizeof(label), TEXT::Localize("ui.viewport.lightLabelSpot").c_str(),
                              object->GetName().c_str(), light->GetRange(), light->GetOuterConeAngleDegrees());
            } else {
                std::snprintf(label, sizeof(label), TEXT::Localize("ui.viewport.lightLabelPoint").c_str(),
                              object->GetName().c_str(), light->GetRange(), light->GetIntensity());
            }
        } else {
            icon.kind = ViewportObjectIcon::Kind::Camera;
            icon.color = IM_COL32(101, 217, 255, 235);
            icon.direction = ProjectedDirection(camera, position, forward, viewportOrigin, viewportSize, screen);
            std::snprintf(label, sizeof(label), "%s", object->GetName().c_str());
        }
        icon.label = label;
        icons.push_back(std::move(icon));
    }
    return icons;
}

std::vector<SceneViewportIconTarget> ObjectIconTargets(const std::vector<ViewportObjectIcon>& icons) {
    std::vector<SceneViewportIconTarget> targets{};
    targets.reserve(icons.size());
    for (const ViewportObjectIcon& icon : icons) {
        targets.push_back({icon.objectId, icon.screen, kIconHitRadius});
    }
    return targets;
}

void DrawObjectOverlayIcons(const std::vector<ViewportObjectIcon>& icons, const ViewportOverlayState& overlays,
                            const std::vector<SceneObjectId>& selected, const ImVec2& mouse) {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    // 指している印は一つだけ（重なれば中心の近い方。点選と同じ決め方）。
    const ViewportObjectIcon* hovered = nullptr;
    float hoveredDistance = kIconHitRadius * kIconHitRadius;
    for (const ViewportObjectIcon& icon : icons) {
        const float dx = mouse.x - icon.screen.x, dy = mouse.y - icon.screen.y;
        if (dx * dx + dy * dy <= hoveredDistance) {
            hoveredDistance = dx * dx + dy * dy;
            hovered = &icon;
        }
    }
    for (const ViewportObjectIcon& icon : icons) {
        const bool isSelected = std::find(selected.begin(), selected.end(), icon.objectId) != selected.end();
        const bool emphasized = isSelected || &icon == hovered;
        // 遠い印は薄く小さく。選んだ印と指している印は近くと同じ濃さで描く。
        const float fade = emphasized ? 1.0f : 0.35f + 0.65f * icon.fade;
        const float scale = emphasized ? 1.15f : 0.75f + 0.25f * icon.fade;
        const ImU32 color = FadeColor(icon.color, fade);
        const ImVec2 center{icon.screen.x, icon.screen.y};
        if (icon.direction.x != 0.0f || icon.direction.y != 0.0f) {
            const float length = 14.0f * scale;
            drawList->AddLine(center, {center.x + icon.direction.x * length, center.y + icon.direction.y * length}, color,
                              1.5f);
        }
        switch (icon.kind) {
        case ViewportObjectIcon::Kind::SpotLight: DrawSpotLightIcon(drawList, center, color, scale); break;
        case ViewportObjectIcon::Kind::Camera: DrawCameraIcon(drawList, center, color, scale); break;
        case ViewportObjectIcon::Kind::PointLight: DrawPointLightIcon(drawList, center, color, scale); break;
        }
        if (isSelected) {
            drawList->AddCircle(center, 11.0f * scale, kIconSelectionColor, 20, 2.0f);
        }
        if (emphasized && overlays.showProbeLabels) {
            DrawViewportLabel(drawList, center, icon.label.c_str(), color);
        }
    }
}

} // namespace DOCUMENT_SCENE
} // namespace EDITOR

} // namespace HIKARI
