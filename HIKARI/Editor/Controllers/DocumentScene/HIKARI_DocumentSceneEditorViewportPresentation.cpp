#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorViewportPresentation.h"

#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace EDITOR {
        namespace DOCUMENT_SCENE {

#if defined(HIKARI_WITH_EDITOR)
        const char* LightProbeVolumeOverlayModeName(LightProbeVolumeOverlayMode mode) {
            switch (mode) {
            case LightProbeVolumeOverlayMode::BoundsOnly: return "Bounds Only";
            case LightProbeVolumeOverlayMode::SampledPoints: return "Sampled Points";
            case LightProbeVolumeOverlayMode::AllPoints: return "All Points";
            case LightProbeVolumeOverlayMode::Off:
            default:
                return "Off";
            }
        }

        const char* ReflectionProbeEditTargetName(ReflectionProbeEditTarget target) {
            switch (target) {
            case ReflectionProbeEditTarget::InfluenceBox: return "Influence Box";
            case ReflectionProbeEditTarget::ProjectionBox: return "Projection Box";
            case ReflectionProbeEditTarget::ProbePosition:
            default:
                return "Probe Center";
            }
        }

        bool DrawLightProbeVolumeModeCombo(LightProbeVolumeOverlayMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Light Probe Volume Mode", LightProbeVolumeOverlayModeName(mode))) {
                const LightProbeVolumeOverlayMode modes[] = {
                    LightProbeVolumeOverlayMode::Off,
                    LightProbeVolumeOverlayMode::BoundsOnly,
                    LightProbeVolumeOverlayMode::SampledPoints,
                    LightProbeVolumeOverlayMode::AllPoints,
                };
                for (LightProbeVolumeOverlayMode candidate : modes) {
                    if (ImGui::Selectable(
                            LightProbeVolumeOverlayModeName(candidate),
                            mode == candidate)) {
                        mode = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool DrawReflectionProbeEditTargetCombo(ReflectionProbeEditTarget& target) {
            bool changed = false;
            if (ImGui::BeginCombo("Reflection Probe Edit", ReflectionProbeEditTargetName(target))) {
                const ReflectionProbeEditTarget targets[] = {
                    ReflectionProbeEditTarget::ProbePosition,
                    ReflectionProbeEditTarget::InfluenceBox,
                    ReflectionProbeEditTarget::ProjectionBox,
                };
                for (ReflectionProbeEditTarget candidate : targets) {
                    if (ImGui::Selectable(
                            ReflectionProbeEditTargetName(candidate),
                            target == candidate)) {
                        target = candidate;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        void DrawViewportDebugOptions(
            ViewportOverlayState& overlays,
            ViewportPerformanceState& performance) {

            ImGui::Checkbox("Grid", &overlays.showGrid);
            ImGui::Checkbox("Axis", &overlays.showAxis);
            ImGui::Checkbox("Lights", &overlays.showLights);
            ImGui::Checkbox("Reflection Probe", &overlays.showReflectionProbe);
            ImGui::Checkbox("Light Probe Volume", &overlays.showLightProbeVolume);
            DrawLightProbeVolumeModeCombo(overlays.lightProbeVolumeMode);
            ImGui::Checkbox("Debug Labels", &overlays.showProbeLabels);
            ImGui::Checkbox("XRay Gizmos", &overlays.showXRayGizmos);
            ImGui::Separator();
            ImGui::Checkbox("Edit Reflection Probe", &overlays.editReflectionProbe);
            DrawReflectionProbeEditTargetCombo(overlays.reflectionProbeEditTarget);
            ImGui::Separator();
            ImGui::Checkbox("Disable SSAO In Editor", &performance.disableSsaoInEditorViewport);
        }

        struct RenderDebugViewOption {
            RenderDebugView view = RenderDebugView::None;
            const char* label = "Lit";
        };

        constexpr RenderDebugViewOption kRenderDebugViewOptions[] = {
            { RenderDebugView::None, "Lit" },
            { RenderDebugView::BaseColor, "Base Color" },
            { RenderDebugView::Normal, "Normal" },
            { RenderDebugView::Tangent, "Tangent" },
            { RenderDebugView::Roughness, "Roughness" },
            { RenderDebugView::Metallic, "Metallic" },
            { RenderDebugView::Occlusion, "Occlusion" },
            { RenderDebugView::Shadow, "Shadow" },
            { RenderDebugView::NdotL, "NdotL" },
            { RenderDebugView::Emissive, "Emissive" },
            { RenderDebugView::SceneDepth, "Scene Depth" },
            { RenderDebugView::SceneColor, "Scene Color" },
            { RenderDebugView::MotionVectors, "Motion Vectors" },
            { RenderDebugView::TemporalHistoryWeight, "TAA History Weight" },
            { RenderDebugView::TemporalDepthRejection, "TAA Depth Rejection" },
            { RenderDebugView::TemporalReactiveMask, "TAA Reactive Mask" },
            { RenderDebugView::TemporalTransparencyMask, "TAA Transparency Mask" },
            { RenderDebugView::TemporalDisocclusion, "TAA Disocclusion" },
            { RenderDebugView::TemporalInvalidDepthMotion, "Invalid Depth / Motion" },
            { RenderDebugView::VolumetricScattering, "Volumetric Scattering" },
            { RenderDebugView::VolumetricTransmittance, "Volumetric Transmittance" },
            { RenderDebugView::VolumetricDepthSlice, "Volumetric Depth Slice" },
            { RenderDebugView::MeshletId, "Meshlet ID" },
            { RenderDebugView::ClusterId, "Cluster ID" },
            { RenderDebugView::SurfaceId, "Surface ID" },
            { RenderDebugView::LodLevel, "LOD Level" },
            { RenderDebugView::LodHeat, "LOD Heat" },
            { RenderDebugView::DrawBucket, "Draw Bucket" },
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
            if (ImGui::BeginCombo(label, kRenderDebugViewOptions[currentIndex].label, ImGuiComboFlags_NoArrowButton)) {
                ImGui::SeparatorText("Shading");
                for (int i = 0; i < static_cast<int>(std::size(kRenderDebugViewOptions)); ++i) {
                    const RenderDebugView option = kRenderDebugViewOptions[i].view;
                    if (option == RenderDebugView::SceneDepth) {
                        ImGui::SeparatorText("Screen Space");
                    }
                    if (option == RenderDebugView::MotionVectors) {
                        ImGui::SeparatorText("Temporal");
                    }
                    if (option == RenderDebugView::VolumetricScattering) {
                        ImGui::SeparatorText("Volumetric");
                    }
                    if (option == RenderDebugView::MeshletId) {
                        ImGui::SeparatorText("Meshlet");
                    }
                    const bool selected = (i == selectedIndex);
                    if (ImGui::Selectable(kRenderDebugViewOptions[i].label, selected)) {
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

        bool ProjectWorldToViewport(
            const Camera3D& camera,
            const MATH::Vec3& worldPosition,
            const ImVec2& viewportOrigin,
            const ImVec2& viewportSize,
            ImVec2& outScreenPosition) {

            const MATH::Vec4 clip = camera.GetViewProj().TransformPoint({
                worldPosition.x,
                worldPosition.y,
                worldPosition.z,
                1.0f
            });
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
            return outScreenPosition.x >= viewportOrigin.x &&
                outScreenPosition.x <= viewportOrigin.x + viewportSize.x &&
                outScreenPosition.y >= viewportOrigin.y &&
                outScreenPosition.y <= viewportOrigin.y + viewportSize.y;
        }

        void DrawViewportLabel(
            ImDrawList* drawList,
            const ImVec2& position,
            const char* text,
            ImU32 color) {

            if (drawList == nullptr || text == nullptr || text[0] == '\0') {
                return;
            }

            const ImVec2 padding{ 6.0f, 4.0f };
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            const ImVec2 min{ position.x + 8.0f, position.y - 8.0f };
            const ImVec2 max{ min.x + textSize.x + padding.x * 2.0f, min.y + textSize.y + padding.y * 2.0f };
            drawList->AddRectFilled(min, max, IM_COL32(9, 13, 18, 205), 3.0f);
            drawList->AddRect(min, max, color, 3.0f, 0, 1.0f);
            drawList->AddText(ImVec2(min.x + padding.x, min.y + padding.y), color, text);
        }

        void DrawReflectionProbeLabels(
            DocumentSceneBase& scene,
            const ViewportOverlayState& overlays,
            const ImVec2& viewportOrigin,
            const ImVec2& viewportSize) {

            const SceneEnvironment& environment = scene.GetSceneEnvironment();
            const ReflectionProbeSettings& probe = environment.reflectionProbe;
            if (!overlays.showReflectionProbe || !overlays.showProbeLabels || !probe.enabled) {
                return;
            }

            const REFLECTION::ReflectionProbeRuntimeData& runtimeProbe = REFLECTION::GetActiveProbe();
            const RENDER3D::LIGHTING::SceneLightingRuntimeData& lighting =
                RENDER3D::LIGHTING::GetLastLightingRuntimeData();
            const char* runtimeState = runtimeProbe.valid
                ? (lighting.source == RENDER3D::LIGHTING::LightingRuntimeSource::BakedRuntime ? "Baked" : "Authoring")
                : "Invalid";

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            ImVec2 screen{};
            if (ProjectWorldToViewport(scene.GetCamera(), probe.position, viewportOrigin, viewportSize, screen)) {
                drawList->AddCircleFilled(screen, 4.0f, IM_COL32(97, 230, 168, 230));
                char label[160]{};
                std::snprintf(
                    label,
                    sizeof(label),
                    "Reflection Probe\n%s\nInfluence: %s\nProjection: %s",
                    runtimeState,
                    probe.influenceShape == ReflectionProbeInfluenceShape::Box ? "Box" : "Sphere",
                    probe.projectionShape == ReflectionProbeProjectionShape::Box ? "Box" : "Infinite");
                DrawViewportLabel(drawList, screen, label, IM_COL32(150, 236, 205, 235));
            }

            if (probe.influenceShape == ReflectionProbeInfluenceShape::Box &&
                ProjectWorldToViewport(scene.GetCamera(), probe.influenceBoxCenter, viewportOrigin, viewportSize, screen)) {
                drawList->AddRectFilled(
                    ImVec2(screen.x - 3.0f, screen.y - 3.0f),
                    ImVec2(screen.x + 3.0f, screen.y + 3.0f),
                    IM_COL32(94, 199, 255, 230));
                DrawViewportLabel(drawList, screen, "Influence", IM_COL32(94, 199, 255, 230));
            }

            if (probe.projectionShape == ReflectionProbeProjectionShape::Box &&
                ProjectWorldToViewport(scene.GetCamera(), probe.projectionBoxCenter, viewportOrigin, viewportSize, screen)) {
                drawList->AddRectFilled(
                    ImVec2(screen.x - 3.0f, screen.y - 3.0f),
                    ImVec2(screen.x + 3.0f, screen.y + 3.0f),
                    IM_COL32(255, 209, 102, 230));
                DrawViewportLabel(drawList, screen, "Projection Proxy", IM_COL32(255, 209, 102, 230));
            }
        }

        ImU32 ToImGuiLightColor(const MATH::Vec3& color, int alpha) {
            const auto toByte = [](float value) -> int {
                return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
            };
            return IM_COL32(toByte(color.x), toByte(color.y), toByte(color.z), alpha);
        }

        void DrawPointLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
            constexpr float kRadius = 6.0f;
            drawList->AddCircleFilled(center, kRadius, color, 12);
            drawList->AddCircle(center, kRadius + 2.0f, IM_COL32(255, 244, 204, 230), 12, 1.2f);
            drawList->AddLine(ImVec2(center.x - 11.0f, center.y), ImVec2(center.x - 7.5f, center.y), color, 1.2f);
            drawList->AddLine(ImVec2(center.x + 7.5f, center.y), ImVec2(center.x + 11.0f, center.y), color, 1.2f);
            drawList->AddLine(ImVec2(center.x, center.y - 11.0f), ImVec2(center.x, center.y - 7.5f), color, 1.2f);
            drawList->AddLine(ImVec2(center.x, center.y + 7.5f), ImVec2(center.x, center.y + 11.0f), color, 1.2f);
            drawList->AddText(ImVec2(center.x - 3.5f, center.y - 6.5f), IM_COL32(24, 21, 13, 245), "P");
        }

        void DrawDirectionalLightIcon(ImDrawList* drawList, const ImVec2& center, ImU32 color) {
            constexpr float kRadius = 7.0f;
            drawList->AddCircleFilled(center, kRadius, color, 16);
            drawList->AddCircle(center, kRadius + 2.0f, IM_COL32(255, 248, 214, 230), 16, 1.2f);
            for (int i = 0; i < 8; ++i) {
                const float angle = (static_cast<float>(i) / 8.0f) * 6.28318530718f;
                const ImVec2 from{
                    center.x + std::cos(angle) * 10.0f,
                    center.y + std::sin(angle) * 10.0f
                };
                const ImVec2 to{
                    center.x + std::cos(angle) * 14.0f,
                    center.y + std::sin(angle) * 14.0f
                };
                drawList->AddLine(from, to, color, 1.2f);
            }
        }

        void DrawLightOverlayIcons(
            DocumentSceneBase& scene,
            const ViewportOverlayState& overlays,
            const ImVec2& viewportOrigin,
            const ImVec2& viewportSize) {

            const SceneEnvironment& environment = scene.GetSceneEnvironment();
            if (!overlays.showLights || !environment.showLightDebug) {
                return;
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const Camera3D& camera = scene.GetCamera();

            if (environment.directional.enabled) {
                const MATH::Vec3 origin{ 0.0f, 1.5f, 0.0f };
                ImVec2 screen{};
                if (ProjectWorldToViewport(camera, origin, viewportOrigin, viewportSize, screen)) {
                    const ImU32 color = ToImGuiLightColor(environment.directional.color, 235);
                    DrawDirectionalLightIcon(drawList, screen, color);
                    if (overlays.showProbeLabels) {
                        DrawViewportLabel(drawList, screen, "Directional Light", color);
                    }
                }
            }

            if (!environment.showPointLightMarkers) {
                return;
            }

            constexpr size_t kMaxPointLightIcons = 32u;
            constexpr size_t kMaxPointLightLabels = 8u;
            size_t iconCount = 0u;
            size_t labelCount = 0u;
            for (size_t i = 0; i < environment.pointLights.size(); ++i) {
                const PointLight& light = environment.pointLights[i];
                if (!light.enabled || light.range <= 0.0f) {
                    continue;
                }
                if (iconCount >= kMaxPointLightIcons) {
                    break;
                }

                ImVec2 screen{};
                if (!ProjectWorldToViewport(camera, light.position, viewportOrigin, viewportSize, screen)) {
                    continue;
                }

                const ImU32 color = ToImGuiLightColor(light.color, 235);
                DrawPointLightIcon(drawList, screen, color);
                ++iconCount;
                if (overlays.showProbeLabels && labelCount < kMaxPointLightLabels) {
                    char label[96]{};
                    std::snprintf(
                        label,
                        sizeof(label),
                        "Point Light %zu\nRange %.1f / Int %.1f",
                        i,
                        light.range,
                        light.intensity);
                    DrawViewportLabel(drawList, screen, label, color);
                    ++labelCount;
                }
            }
        }

        void ClampReflectionProbeBoxSize(MATH::Vec3& size) {
            size.x = (std::max)(0.001f, size.x);
            size.y = (std::max)(0.001f, size.y);
            size.z = (std::max)(0.001f, size.z);
        }

        bool CanEditReflectionProbeTarget(
            const ReflectionProbeSettings& probe,
            ReflectionProbeEditTarget target) {

            if (!probe.enabled) {
                return false;
            }
            if (target == ReflectionProbeEditTarget::InfluenceBox) {
                return probe.influenceShape == ReflectionProbeInfluenceShape::Box;
            }
            if (target == ReflectionProbeEditTarget::ProjectionBox) {
                return probe.projectionShape == ReflectionProbeProjectionShape::Box;
            }
            return true;
        }

        TransformData BuildReflectionProbeEditTransform(
            const ReflectionProbeSettings& probe,
            ReflectionProbeEditTarget target) {

            TransformData transform{};
            transform.scale = { 1.0f, 1.0f, 1.0f };
            if (target == ReflectionProbeEditTarget::InfluenceBox) {
                transform.position = probe.influenceBoxCenter;
                transform.scale = probe.influenceBoxSize;
                return transform;
            }
            if (target == ReflectionProbeEditTarget::ProjectionBox) {
                transform.position = probe.projectionBoxCenter;
                transform.scale = probe.projectionBoxSize;
                return transform;
            }
            transform.position = probe.position;
            return transform;
        }

        EditorTransformGizmoState BuildReflectionProbeGizmoState(
            const EditorTransformGizmoState& source,
            ReflectionProbeEditTarget target) {

            EditorTransformGizmoState state = source;
            if (target == ReflectionProbeEditTarget::ProbePosition ||
                state.operation == EditorTransformGizmoOperation::Rotate) {
                state.operation = EditorTransformGizmoOperation::Translate;
            }
            return state;
        }

        void ApplyReflectionProbeEditTransform(
            ReflectionProbeSettings& probe,
            ReflectionProbeEditTarget target,
            const EDITOR::EditorTransformGizmoResult& result,
            EditorTransformGizmoOperation operation) {

            if (target == ReflectionProbeEditTarget::InfluenceBox) {
                probe.influenceBoxCenter = result.transform.position;
                if (operation == EditorTransformGizmoOperation::Scale) {
                    probe.influenceBoxSize = result.transform.scale;
                    ClampReflectionProbeBoxSize(probe.influenceBoxSize);
                }
                return;
            }
            if (target == ReflectionProbeEditTarget::ProjectionBox) {
                probe.projectionBoxCenter = result.transform.position;
                if (operation == EditorTransformGizmoOperation::Scale) {
                    probe.projectionBoxSize = result.transform.scale;
                    ClampReflectionProbeBoxSize(probe.projectionBoxSize);
                }
                return;
            }
            probe.position = result.transform.position;
        }
#endif

        } // namespace DOCUMENT_SCENE
    } // namespace EDITOR

} // namespace HIKARI
