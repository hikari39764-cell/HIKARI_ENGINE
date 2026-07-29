#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorViewportPresentation.h"

#include "Render3D/Lighting/HIKARI_SceneLightingRuntimeData.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

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
        void DrawViewportDebugOptions(
            ViewportOverlayState& overlays,
            ViewportPerformanceState& performance) {

            ImGui::Checkbox("Grid", &overlays.showGrid);
            ImGui::Checkbox("Axis", &overlays.showAxis);
            ImGui::Checkbox("Lights", &overlays.showLights);
            ImGui::Checkbox("Debug Labels", &overlays.showProbeLabels);
            ImGui::Checkbox("XRay Gizmos", &overlays.showXRayGizmos);
            ImGui::TextDisabled(
                "Lighting shapes are controlled by component gizmo providers.");
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
            { RenderDebugView::ReflectionProviderWeight, "Reflection Provider Weight" },
            { RenderDebugView::IrradianceProviderWeight, "Irradiance Provider Weight" },
            { RenderDebugView::LightingFallbackWeight, "Lighting Fallback Weight" },
            { RenderDebugView::SsrConfidence, "SSR Confidence" },
            { RenderDebugView::SsrHit, "SSR Hit" },
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
                    if (option == RenderDebugView::ReflectionProviderWeight) {
                        ImGui::SeparatorText("Lighting Providers");
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

        void DrawSpotLightIcon(
            ImDrawList* drawList,
            const ImVec2& center,
            ImU32 color) {

            const ImVec2 top{ center.x, center.y - 8.0f };
            const ImVec2 left{ center.x - 7.0f, center.y + 7.0f };
            const ImVec2 right{ center.x + 7.0f, center.y + 7.0f };
            drawList->AddTriangleFilled(top, left, right, color);
            drawList->AddTriangle(
                top,
                left,
                right,
                IM_COL32(255, 248, 214, 230),
                1.4f);
            drawList->AddText(
                ImVec2(center.x - 3.5f, center.y - 5.5f),
                IM_COL32(24, 21, 13, 245),
                "S");
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

            if (!overlays.showLights ||
                !scene.GetSceneEnvironment().showLightDebug) {
                return;
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const Camera3D& camera = scene.GetCamera();
            std::unordered_set<uint64_t> hiddenLightObjectIds{};
            for (const SceneObjectData& object :
                 scene.GetSceneDocument().objects) {
                if (!object.enabled || !object.editorVisible) {
                    hiddenLightObjectIds.insert(object.id.value);
                }
            }
            constexpr size_t kMaxLightIcons = 64u;
            constexpr size_t kMaxLightLabels = 12u;
            size_t iconCount = 0u;
            size_t labelCount = 0u;
            for (const std::unique_ptr<GameObject>& object :
                 scene.GetWorld().GetObjects()) {
                if (!object || iconCount >= kMaxLightIcons) {
                    continue;
                }
                if (hiddenLightObjectIds.contains(
                        object->GetDocumentId().value)) {
                    continue;
                }
                const LightComponent* light =
                    object->GetComponent<LightComponent>();
                if (light == nullptr || !light->IsEnabled()) {
                    continue;
                }

                const MATH::Mat4 worldMatrix =
                    object->GetTransform().GetWorldMatrix();
                const MATH::Vec4 worldOrigin =
                    worldMatrix.TransformPoint(
                        { 0.0f, 0.0f, 0.0f, 1.0f });
                const MATH::Vec3 worldPosition{
                    worldOrigin.x,
                    worldOrigin.y,
                    worldOrigin.z
                };
                ImVec2 screen{};
                if (!ProjectWorldToViewport(
                        camera,
                        worldPosition,
                        viewportOrigin,
                        viewportSize,
                        screen)) {
                    continue;
                }

                const ImU32 color =
                    ToImGuiLightColor(light->GetColor(), 235);
                switch (light->GetLightType()) {
                case LightComponentType::Directional:
                    DrawDirectionalLightIcon(drawList, screen, color);
                    break;
                case LightComponentType::Spot:
                    DrawSpotLightIcon(drawList, screen, color);
                    break;
                case LightComponentType::Point:
                default:
                    DrawPointLightIcon(drawList, screen, color);
                    break;
                }
                ++iconCount;
                if (!overlays.showProbeLabels ||
                    labelCount >= kMaxLightLabels) {
                    continue;
                }
                char label[144]{};
                if (light->GetLightType() ==
                    LightComponentType::Directional) {
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%s\nDirectional / Int %.1f",
                        object->GetName().c_str(),
                        light->GetIntensity());
                } else if (light->GetLightType() ==
                           LightComponentType::Spot) {
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%s\nRange %.1f / Cone %.1f deg",
                        object->GetName().c_str(),
                        light->GetRange(),
                        light->GetOuterConeAngleDegrees());
                } else {
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%s\nRange %.1f / Int %.1f",
                        object->GetName().c_str(),
                        light->GetRange(),
                        light->GetIntensity());
                }
                DrawViewportLabel(drawList, screen, label, color);
                ++labelCount;
            }
        }

#endif

        } // namespace DOCUMENT_SCENE
    } // namespace EDITOR

} // namespace HIKARI
