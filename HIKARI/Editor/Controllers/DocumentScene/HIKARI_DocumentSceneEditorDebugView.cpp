#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"
#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorViewportPresentation.h"

#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    using EDITOR::DOCUMENT_SCENE::DrawRenderDebugViewCombo;
    using EDITOR::DOCUMENT_SCENE::DrawViewportDebugOptions;

    void DocumentSceneEditorController::DrawDebugViewWindow(DocumentSceneBase& scene, bool& open) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin(
                TEXT::LocalizeWindow(
                    "ui.window.debugView", "Debug View").c_str(),
                &open)) {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText(
            TEXT::Localize("ui.debugView.globalView").c_str());
        DrawRenderDebugViewCombo("##DebugViewMode", context_.viewportDebug, 220.0f);
        ImGui::SameLine();
        if (EDITOR::ActionButton(
                TEXT::Localize("ui.debugView.lit").c_str(),
                "DebugViewLit",
                context_.viewportDebug.renderView == RenderDebugView::None
                    ? EDITOR::EditorButtonTone::Primary
                    : EDITOR::EditorButtonTone::Neutral)) {
            context_.viewportDebug.renderView = RenderDebugView::None;
        }
        ImGui::Checkbox(
            TEXT::Localize("ui.debugView.showLegend").c_str(),
            &context_.viewportDebug.showLegend);
        ImGui::Checkbox(
            TEXT::Localize("ui.debugView.freezeCullingCamera").c_str(),
            &context_.viewportDebug.freezeCullingCamera);

        if (context_.viewportDebug.renderView == RenderDebugView::None) {
            ImGui::TextDisabled(
                "%s",
                TEXT::Localize("ui.debugView.normalShaded").c_str());
        } else {
            EDITOR::StatusText(
                ToString(context_.viewportDebug.renderView),
                EDITOR::EditorStatusTone::Ready);
            ImGui::TextDisabled(
                IsGeometryRenderDebugView(context_.viewportDebug.renderView)
                    ? TEXT::Localize(
                        "ui.debugView.meshletGenerated").c_str()
                    : IsScreenSpaceRenderDebugView(context_.viewportDebug.renderView)
                    ? TEXT::Localize(
                        "ui.debugView.screenSpaceModes").c_str()
                    : TEXT::Localize(
                        "ui.debugView.shadingModesReuse").c_str());
            ImGui::TextDisabled(
                "%s",
                TEXT::Localize(
                    "ui.debugView.temporalBypassed").c_str());
        }

        ImGui::SeparatorText(
            TEXT::Localize("ui.debugView.shading").c_str());
        if (ImGui::BeginTable("DebugViewShadingModes", 3, ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView shadingModes[] = {
                RenderDebugView::BaseColor,
                RenderDebugView::Normal,
                RenderDebugView::Tangent,
                RenderDebugView::LightingOnly,
                RenderDebugView::Roughness,
                RenderDebugView::Metallic,
                RenderDebugView::Occlusion,
                RenderDebugView::Shadow,
                RenderDebugView::NdotL,
                RenderDebugView::Emissive,
                RenderDebugView::SceneDepth,
            };
            for (RenderDebugView mode : shadingModes) {
                ImGui::TableNextColumn();
                const bool selected = context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(ToString(mode), ToString(mode), selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText(
            TEXT::Localize("ui.debugView.screenSpace").c_str());
        if (ImGui::BeginTable("DebugViewScreenSpaceModes", 3, ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView screenSpaceModes[] = {
                RenderDebugView::SceneColor,
                RenderDebugView::MotionVectors,
                RenderDebugView::TemporalHistoryWeight,
                RenderDebugView::TemporalDepthRejection,
                RenderDebugView::TemporalReactiveMask,
                RenderDebugView::TemporalTransparencyMask,
                RenderDebugView::TemporalDisocclusion,
                RenderDebugView::TemporalInvalidDepthMotion,
                RenderDebugView::SsrConfidence,
                RenderDebugView::SsrHit,
            };
            for (RenderDebugView mode : screenSpaceModes) {
                ImGui::TableNextColumn();
                const bool selected = context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(ToString(mode), ToString(mode), selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText(
            TEXT::Localize("ui.debugView.volumetric").c_str());
        if (ImGui::BeginTable("DebugViewVolumetricModes", 3, ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView volumetricModes[] = {
                RenderDebugView::VolumetricScattering,
                RenderDebugView::VolumetricTransmittance,
                RenderDebugView::VolumetricDepthSlice,
            };
            for (RenderDebugView mode : volumetricModes) {
                ImGui::TableNextColumn();
                const bool selected = context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(ToString(mode), ToString(mode), selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText(
            TEXT::Localize("ui.debugView.lightingProviders").c_str());
        if (ImGui::BeginTable(
                "DebugViewLightingProviderModes",
                3,
                ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView lightingProviderModes[] = {
                RenderDebugView::ReflectionProviderWeight,
                RenderDebugView::IrradianceProviderWeight,
                RenderDebugView::LightingFallbackWeight,
                RenderDebugView::SurfaceLightmap,
            };
            for (RenderDebugView mode : lightingProviderModes) {
                ImGui::TableNextColumn();
                const bool selected =
                    context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(
                        ToString(mode),
                        ToString(mode),
                        selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText(
            TEXT::Localize("ui.debugView.meshlet").c_str());
        if (ImGui::BeginTable("DebugViewMeshletModes", 3, ImGuiTableFlags_SizingStretchSame)) {
            const RenderDebugView meshletModes[] = {
                RenderDebugView::MeshletId,
                RenderDebugView::ClusterId,
                RenderDebugView::SurfaceId,
                RenderDebugView::LodLevel,
                RenderDebugView::LodHeat,
                RenderDebugView::DrawBucket,
            };
            for (RenderDebugView mode : meshletModes) {
                ImGui::TableNextColumn();
                const bool selected = context_.viewportDebug.renderView == mode;
                if (EDITOR::ModeButton(ToString(mode), ToString(mode), selected)) {
                    context_.viewportDebug.renderView = mode;
                }
            }
            ImGui::EndTable();
        }

        if (context_.viewportDebug.showLegend) {
            ImGui::SeparatorText(
                TEXT::Localize("ui.debugView.lodLegend").c_str());
            const ImVec4 lodColors[] = {
                { 0.95f, 0.20f, 0.18f, 1.0f },
                { 0.95f, 0.70f, 0.16f, 1.0f },
                { 0.38f, 0.86f, 0.30f, 1.0f },
                { 0.18f, 0.72f, 0.96f, 1.0f },
                { 0.55f, 0.38f, 0.95f, 1.0f },
            };
            const char* labels[] = { "LOD0", "LOD1", "LOD2", "LOD3", "LOD4+" };
            for (int i = 0; i < 5; ++i) {
                ImGui::ColorButton(labels[i], lodColors[i], ImGuiColorEditFlags_NoTooltip, ImVec2(18.0f, 18.0f));
                ImGui::SameLine();
                ImGui::TextUnformatted(labels[i]);
                if (i < 4) {
                    ImGui::SameLine();
                }
            }
        }

        if (ImGui::CollapsingHeader(
                TEXT::Localize(
                    "ui.debugView.viewportTools").c_str())) {
            DrawViewportDebugOptions(context_.overlays, context_.viewportPerformance);
        }
        if (ImGui::CollapsingHeader(
                TEXT::Localize(
                    "ui.debugView.debugCamera").c_str())) {
            debugCameraPanel_.DrawContents(scene.GetDebugCamera());
        }

        ImGui::End();
#else
        (void)scene;
        (void)open;
#endif
    }

} // namespace HIKARI
