#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorViewportPresentation.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Lighting/HIKARI_LightingAuthoringModel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <unordered_set>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

namespace EDITOR {
namespace DOCUMENT_SCENE {

#if defined(HIKARI_WITH_EDITOR)
void DrawViewportDebugOptions(ViewportOverlayState& overlays, ViewportPerformanceState& performance) {
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugGrid").c_str(), &overlays.showGrid);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugGridFloor").c_str(), &overlays.showGridFloor);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugAxis").c_str(), &overlays.showAxis);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugLights").c_str(), &overlays.showLights);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugLabels").c_str(), &overlays.showProbeLabels);
    ImGui::Checkbox(TEXT::Localize("ui.viewport.debugXRayGizmos").c_str(), &overlays.showXRayGizmos);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.viewport.debugLightingShapes").c_str());
    ImGui::Separator();
    ImGui::Checkbox(TEXT::Localize("ui.viewport.disableSsaoInEditor").c_str(),
                    &performance.disableSsaoInEditorViewport);
}

struct RenderDebugViewOption {
    RenderDebugView view = RenderDebugView::None;
    const char* key = "ui.debugView.lit";
};

constexpr RenderDebugViewOption kRenderDebugViewOptions[] = {
    {RenderDebugView::None, "ui.debugView.lit"},
    {RenderDebugView::BaseColor, "ui.debugView.baseColor"},
    {RenderDebugView::Normal, "ui.debugView.normal"},
    {RenderDebugView::Tangent, "ui.debugView.tangent"},
    {RenderDebugView::Roughness, "ui.debugView.roughness"},
    {RenderDebugView::Metallic, "ui.debugView.metallic"},
    {RenderDebugView::Occlusion, "ui.debugView.occlusion"},
    {RenderDebugView::Shadow, "ui.debugView.shadow"},
    {RenderDebugView::NdotL, "ui.debugView.ndotl"},
    {RenderDebugView::Emissive, "ui.debugView.emissive"},
    {RenderDebugView::DirectDiffuse, "ui.debugView.directDiffuse"},
    {RenderDebugView::DirectionalSpecular, "ui.debugView.directionalSpecular"},
    {RenderDebugView::LocalSpecular, "ui.debugView.localSpecular"},
    {RenderDebugView::IndirectDiffuse, "ui.debugView.indirectDiffuse"},
    {RenderDebugView::IndirectSpecular, "ui.debugView.indirectSpecular"},
    {RenderDebugView::SceneDepth, "ui.debugView.sceneDepth"},
    {RenderDebugView::SceneColor, "ui.debugView.sceneColor"},
    {RenderDebugView::MotionVectors, "ui.debugView.motionVectors"},
    {RenderDebugView::TemporalHistoryWeight, "ui.debugView.taaHistoryWeight"},
    {RenderDebugView::TemporalDepthRejection, "ui.debugView.taaDepthRejection"},
    {RenderDebugView::TemporalReactiveMask, "ui.debugView.taaReactiveMask"},
    {RenderDebugView::TemporalTransparencyMask, "ui.debugView.taaTransparencyMask"},
    {RenderDebugView::TemporalDisocclusion, "ui.debugView.taaDisocclusion"},
    {RenderDebugView::TemporalInvalidDepthMotion, "ui.debugView.invalidDepthMotion"},
    {RenderDebugView::VolumetricScattering, "ui.debugView.volumetricScattering"},
    {RenderDebugView::VolumetricTransmittance, "ui.debugView.volumetricTransmittance"},
    {RenderDebugView::VolumetricDepthSlice, "ui.debugView.volumetricDepthSlice"},
    {RenderDebugView::ReflectionProviderWeight, "ui.debugView.reflectionProviderWeight"},
    {RenderDebugView::IrradianceProviderWeight, "ui.debugView.irradianceProviderWeight"},
    {RenderDebugView::LightingFallbackWeight, "ui.debugView.lightingFallbackWeight"},
    {RenderDebugView::SurfaceLightmap, "ui.debugView.surfaceLightmap"},
    {RenderDebugView::SsrConfidence, "ui.debugView.ssrConfidence"},
    {RenderDebugView::SsrHit, "ui.debugView.ssrHit"},
    {RenderDebugView::MeshletId, "ui.debugView.meshletId"},
    {RenderDebugView::ClusterId, "ui.debugView.clusterId"},
    {RenderDebugView::SurfaceId, "ui.debugView.surfaceId"},
    {RenderDebugView::LodLevel, "ui.debugView.lodLevel"},
    {RenderDebugView::LodHeat, "ui.debugView.lodHeat"},
    {RenderDebugView::DrawBucket, "ui.debugView.drawBucket"},
};

int RenderDebugViewOptionIndex(RenderDebugView view) {
    for (int i = 0; i < static_cast<int>(std::size(kRenderDebugViewOptions)); ++i) {
        if (kRenderDebugViewOptions[i].view == view) {
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
    if (ImGui::BeginCombo(label, TEXT::Localize(kRenderDebugViewOptions[currentIndex].key).c_str(),
                          ImGuiComboFlags_NoArrowButton)) {
        ImGui::SeparatorText(TEXT::Localize("ui.debugView.shading").c_str());
        for (int i = 0; i < static_cast<int>(std::size(kRenderDebugViewOptions)); ++i) {
            const RenderDebugView option = kRenderDebugViewOptions[i].view;
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
            if (ImGui::Selectable(TEXT::Localize(kRenderDebugViewOptions[i].key).c_str(), selected)) {
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
        debugView.renderView = kRenderDebugViewOptions[selectedIndex].view;
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

void DrawPointLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
    constexpr float kRadius = 8.0f;
    drawList->AddCircleFilled(center, kRadius, color, 12);
    drawList->AddCircle(center, kRadius + 2.5f, IM_COL32(255, 248, 220, 245), 16, 2.0f);
    drawList->AddLine(ImVec2(center.x - 15.0f, center.y), ImVec2(center.x - 10.5f, center.y), color, 2.0f);
    drawList->AddLine(ImVec2(center.x + 10.5f, center.y), ImVec2(center.x + 15.0f, center.y), color, 2.0f);
    drawList->AddLine(ImVec2(center.x, center.y - 15.0f), ImVec2(center.x, center.y - 10.5f), color, 2.0f);
    drawList->AddLine(ImVec2(center.x, center.y + 10.5f), ImVec2(center.x, center.y + 15.0f), color, 2.0f);
    drawList->AddText(ImVec2(center.x - 3.5f, center.y - 6.5f), IM_COL32(24, 21, 13, 255), "P");
}

void DrawSpotLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
    const ImVec2 top{center.x, center.y - 10.0f};
    const ImVec2 left{center.x - 9.0f, center.y + 9.0f};
    const ImVec2 right{center.x + 9.0f, center.y + 9.0f};
    drawList->AddTriangleFilled(top, left, right, color);
    drawList->AddTriangle(top, left, right, IM_COL32(255, 248, 214, 230), 2.0f);
    drawList->AddText(ImVec2(center.x - 3.5f, center.y - 5.5f), IM_COL32(24, 21, 13, 245), "S");
}

void DrawDirectionalLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
    constexpr float kRadius = 9.0f;
    drawList->AddCircleFilled(center, kRadius, color, 16);
    drawList->AddCircle(center, kRadius + 2.5f, IM_COL32(255, 248, 214, 245), 20, 2.0f);
    for (int i = 0; i < 8; ++i) {
        const float angle = (static_cast<float>(i) / 8.0f) * 6.28318530718f;
        const ImVec2 from{center.x + std::cos(angle) * 13.0f, center.y + std::sin(angle) * 13.0f};
        const ImVec2 to{center.x + std::cos(angle) * 19.0f, center.y + std::sin(angle) * 19.0f};
        drawList->AddLine(from, to, color, 2.0f);
    }
}

void DrawDirectionalLightArrow(ImDrawList* drawList, const ImVec2& center, const ImVec2& projectedDirection,
                               ImU32 color) {
    ImVec2 direction{projectedDirection.x - center.x, projectedDirection.y - center.y};
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length <= 0.5f) {
        direction = {0.0f, 1.0f};
    } else {
        direction.x /= length;
        direction.y /= length;
    }
    const ImVec2 normal{-direction.y, direction.x};
    const ImVec2 start{center.x + direction.x * 22.0f, center.y + direction.y * 22.0f};
    const ImVec2 end{start.x + direction.x * 56.0f, start.y + direction.y * 56.0f};
    drawList->AddLine(start, end, IM_COL32(8, 10, 14, 235), 6.0f);
    drawList->AddLine(start, end, color, 3.0f);
    const ImVec2 left{end.x - direction.x * 12.0f + normal.x * 7.0f, end.y - direction.y * 12.0f + normal.y * 7.0f};
    const ImVec2 right{end.x - direction.x * 12.0f - normal.x * 7.0f, end.y - direction.y * 12.0f - normal.y * 7.0f};
    drawList->AddTriangleFilled(end, left, right, color);
    drawList->AddTriangle(end, left, right, IM_COL32(255, 252, 235, 245), 1.5f);
}

void DrawLightOverlayIcons(DocumentSceneBase& scene, const ViewportOverlayState& overlays, const ImVec2& viewportOrigin,
                           const ImVec2& viewportSize) {
    if (!overlays.showLights || !scene.GetSceneEnvironment().showLightDebug) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const Camera3D& camera = scene.GetCamera();
    std::unordered_set<uint64_t> hiddenLightObjectIds{};
    for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
        if (!object.enabled || !object.editorVisible) {
            hiddenLightObjectIds.insert(object.id.value);
        }
    }
    constexpr size_t kMaxLightIcons = 64u;
    constexpr size_t kMaxLightLabels = 12u;
    size_t iconCount = 0u;
    size_t labelCount = 0u;
    for (const std::unique_ptr<GameObject>& object : scene.GetWorld().GetObjects()) {
        if (!object || iconCount >= kMaxLightIcons) {
            continue;
        }
        if (hiddenLightObjectIds.contains(object->GetDocumentId().value)) {
            continue;
        }
        const LightComponent* light = object->GetComponent<LightComponent>();
        if (light == nullptr || !light->IsEnabled()) {
            continue;
        }

        const MATH::Mat4 worldMatrix = object->GetTransform().GetWorldMatrix();
        const MATH::Vec4 worldOrigin = worldMatrix.TransformPoint({0.0f, 0.0f, 0.0f, 1.0f});
        const MATH::Vec3 worldPosition{worldOrigin.x, worldOrigin.y, worldOrigin.z};
        ImVec2 screen{};
        if (!ProjectWorldToViewport(camera, worldPosition, viewportOrigin, viewportSize, screen)) {
            continue;
        }

        const ImU32 color = ToImGuiLightColor(light->GetColor(), 235);
        const MATH::Vec3 worldForward =
            MATH::Normalize({worldMatrix.m[2][0], worldMatrix.m[2][1], worldMatrix.m[2][2]});
        switch (light->GetLightType()) {
        case LightComponentType::Directional: {
            DrawDirectionalLightIcon(drawList, screen, color);
            ImVec2 projectedDirection{screen.x, screen.y + 1.0f};
            (void)ProjectWorldToViewport(camera, worldPosition + worldForward * 2.0f, viewportOrigin, viewportSize,
                                         projectedDirection);
            DrawDirectionalLightArrow(drawList, screen, projectedDirection, color);
            break;
        }
        case LightComponentType::Spot: DrawSpotLightIcon(drawList, screen, color); break;
        case LightComponentType::Point:
        default: DrawPointLightIcon(drawList, screen, color); break;
        }
        ++iconCount;
        if (!overlays.showProbeLabels || labelCount >= kMaxLightLabels) {
            continue;
        }
        char label[144]{};
        if (light->GetLightType() == LightComponentType::Directional) {
            const MATH::Vec3 rotation = LIGHTING_AUTHORING::DirectionalLightEulerDegreesFromDirection(worldForward);
            std::snprintf(label, sizeof(label), TEXT::Localize("ui.viewport.lightLabelDirectional").c_str(),
                          object->GetName().c_str(), rotation.x, rotation.y, light->GetIntensity());
        } else if (light->GetLightType() == LightComponentType::Spot) {
            std::snprintf(label, sizeof(label), TEXT::Localize("ui.viewport.lightLabelSpot").c_str(),
                          object->GetName().c_str(), light->GetRange(), light->GetOuterConeAngleDegrees());
        } else {
            std::snprintf(label, sizeof(label), TEXT::Localize("ui.viewport.lightLabelPoint").c_str(),
                          object->GetName().c_str(), light->GetRange(), light->GetIntensity());
        }
        DrawViewportLabel(drawList, screen, label, color);
        ++labelCount;
    }
}

#endif

} // namespace DOCUMENT_SCENE
} // namespace EDITOR

} // namespace HIKARI
