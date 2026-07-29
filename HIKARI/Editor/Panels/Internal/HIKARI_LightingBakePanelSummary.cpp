#include "Editor/Panels/Internal/HIKARI_LightingBakePanelSummary.h"

#include <algorithm>

#include "Assets/Lighting/HIKARI_LightingBakePolicy.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::LIGHTING_BAKE_PANEL {

#if defined(HIKARI_WITH_EDITOR)
    namespace {

        uint32_t CountEnabledItems(
            const TOOLS::BAKING::LightingBakeStatus& status,
            TOOLS::BAKING::LightingBakeItemKind kind) {

            return static_cast<uint32_t>(std::count_if(
                status.items.begin(),
                status.items.end(),
                [kind](
                    const TOOLS::BAKING::
                        LightingBakeItemStatus& item) {
                    return item.enabled &&
                        item.kind == kind;
                }));
        }

    } // namespace
#endif

    void DrawWorkflowSummary(
        const TOOLS::BAKING::LightingBakeStatus& status,
        const EditorThemePalette& palette) {
#if defined(HIKARI_WITH_EDITOR)
        const uint32_t reflectionCount =
            CountEnabledItems(
                status,
                TOOLS::BAKING::LightingBakeItemKind::
                    ReflectionProbe);
        const uint32_t irradianceCount =
            CountEnabledItems(
                status,
                TOOLS::BAKING::LightingBakeItemKind::
                    IrradianceVolume);
        const uint32_t lightmapCount =
            CountEnabledItems(
                status,
                TOOLS::BAKING::LightingBakeItemKind::
                    LightmapObject);
        const GFX::RAYTRACING::DxrCapabilities dxr =
            GFX::RAYTRACING::QueryDxrCapabilities(
                GFX::GetCurrentContext().device);

        ImGui::SeparatorText("Lighting Foundation");
        if (ImGui::BeginTable(
                "LightingFoundationSummary",
                4,
                ImGuiTableFlags_SizingStretchSame |
                    ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("DIFFUSE GI");
            ImGui::Text(
                "%u Irradiance Volume%s",
                irradianceCount,
                irradianceCount == 1u ? "" : "s");
            ImGui::TextDisabled(
                "Fallback: Sky / Ambient");

            ImGui::TableNextColumn();
            ImGui::TextDisabled("SURFACE GI");
            ImGui::Text(
                "%u Lightmap Target%s",
                lightmapCount,
                lightmapCount == 1u ? "" : "s");
            ImGui::TextDisabled(
                dxr.SupportsInlineRayQuery()
                    ? "DXR Path Tracing Ready"
                    : "DXR unavailable - Probe fallback");
            ImGui::TextDisabled(
                "%u px | %u spp | %u bounces",
                ASSETS::LIGHTING::
                    kSurfaceLightmapDefaultResolution,
                ASSETS::LIGHTING::
                    kSurfaceLightmapDefaultSampleCount,
                ASSETS::LIGHTING::
                    kSurfaceLightmapDefaultBounceCount);

            ImGui::TableNextColumn();
            ImGui::TextDisabled("SPECULAR");
            ImGui::Text(
                "%u Reflection Probe%s",
                reflectionCount,
                reflectionCount == 1u ? "" : "s");
            ImGui::TextDisabled(
                "Fallback: Sky IBL");

            ImGui::TableNextColumn();
            ImGui::TextDisabled("BAKE STATE");
            if (!status.valid) {
                StatusBadge(
                    "Unavailable",
                    EditorStatusTone::Error);
            } else if (status.dirtyCount > 0u) {
                StatusBadge(
                    "Needs Bake",
                    EditorStatusTone::Warning);
            } else {
                StatusBadge(
                    "Ready",
                    EditorStatusTone::Ready);
            }
            ImGui::TextDisabled(
                "Bake only capture changes");
            ImGui::EndTable();
        }

        if (!status.valid) {
            ImGui::TextColored(
                palette.warning,
                "Save the scene before creating persistent lighting outputs.");
        } else if (status.dirtyCount > 0u) {
            ImGui::TextColored(
                palette.warning,
                "%u provider%s changed. Runtime keeps the last valid "
                "result or its environment fallback until Bake Dirty completes.",
                status.dirtyCount,
                status.dirtyCount == 1u ? "" : "s");
        } else if (status.totalCount > 0u) {
            ImGui::TextColored(
                palette.success,
                "Local GI and reflection data are ready for runtime blending.");
        }
        ImGui::TextDisabled(
            "Surface Lightmaps require Static opaque models with valid UV1. "
            "They replace only diffuse indirect lighting; specular remains "
            "SSR > Local Probe > Sky IBL.");
#else
        (void)status;
        (void)palette;
#endif
    }

} // namespace HIKARI::EDITOR::LIGHTING_BAKE_PANEL
