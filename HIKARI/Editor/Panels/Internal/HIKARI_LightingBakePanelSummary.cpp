#include "Editor/Panels/Internal/HIKARI_LightingBakePanelSummary.h"

#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>

#include "Scene/HIKARI_SceneDocument.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Tools/Baking/HIKARI_LightingBakeService.h"

#include "imgui.h"

namespace HIKARI::EDITOR::LIGHTING_BAKE_PANEL {

namespace {

uint32_t CountEnabledItems(const TOOLS::BAKING::LightingBakeStatus& status, TOOLS::BAKING::LightingBakeItemKind kind) {
    return static_cast<uint32_t>(std::count_if(
        status.items.begin(), status.items.end(),
        [kind](const TOOLS::BAKING::LightingBakeItemStatus& item) { return item.enabled && item.kind == kind; }));
}

} // namespace

void DrawWorkflowSummary(const TOOLS::BAKING::LightingBakeStatus& status, const SceneLightingBakeSettings& quality,
                         const EditorThemePalette& palette) {
    const uint32_t reflectionCount = CountEnabledItems(status, TOOLS::BAKING::LightingBakeItemKind::ReflectionProbe);
    const uint32_t irradianceCount = CountEnabledItems(status, TOOLS::BAKING::LightingBakeItemKind::IrradianceVolume);
    const uint32_t lightmapCount = CountEnabledItems(status, TOOLS::BAKING::LightingBakeItemKind::LightmapObject);
    const GFX::RAYTRACING::DxrCapabilities dxr = GFX::RAYTRACING::QueryDxrCapabilities(GFX::GetCurrentContext().device);

    ImGui::SeparatorText(TEXT::Localize("ui.lightingBake.lightingFoundation").c_str());
    if (ImGui::BeginTable("LightingFoundationSummary", 4,
                          ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.diffuseGi").c_str());
        ImGui::Text(TEXT::Localize("ui.lightingBake.irradianceVolumeFormat").c_str(), irradianceCount,
                    irradianceCount == 1u ? "" : "s");
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.fallbackSkyAmbient").c_str());

        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.surfaceGi").c_str());
        ImGui::Text(TEXT::Localize("ui.lightingBake.lightmapTargetFormat").c_str(), lightmapCount,
                    lightmapCount == 1u ? "" : "s");
        ImGui::TextDisabled(dxr.SupportsInlineRayQuery() ? TEXT::Localize("ui.lightingBake.dxrReady").c_str()
                                                         : TEXT::Localize("ui.lightingBake.dxrUnavailable").c_str());
        ImGui::TextDisabled(TEXT::Localize("ui.lightingBake.lightmapBudgetFormat").c_str(), quality.sampleCount,
                            quality.bounceCount);

        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.specular").c_str());
        ImGui::Text(TEXT::Localize("ui.lightingBake.reflectionProbeFormat").c_str(), reflectionCount,
                    reflectionCount == 1u ? "" : "s");
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.fallbackSkyIbl").c_str());

        ImGui::TableNextColumn();
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.bakeState").c_str());
        if (!status.valid) {
            StatusBadge(TEXT::Localize("ui.lightingBake.unavailable").c_str(), EditorStatusTone::Error);
        } else if (status.dirtyCount > 0u) {
            StatusBadge(TEXT::Localize("ui.lightingBake.needsBake").c_str(), EditorStatusTone::Warning);
        } else {
            StatusBadge(TEXT::Localize("ui.lightingBake.readyBadge").c_str(), EditorStatusTone::Ready);
        }
        ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.bakeOnlyCaptures").c_str());
        ImGui::EndTable();
    }

    if (!status.valid) {
        ImGui::TextColored(palette.warning, "%s", TEXT::Localize("ui.lightingBake.saveSceneWarning").c_str());
    } else if (status.dirtyCount > 0u) {
        ImGui::TextColored(palette.warning, TEXT::Localize("ui.lightingBake.dirtyWarningFormat").c_str(),
                           status.dirtyCount, status.dirtyCount == 1u ? "" : "s");
    } else if (status.totalCount > 0u) {
        ImGui::TextColored(palette.success, "%s", TEXT::Localize("ui.lightingBake.readyNote").c_str());
    }
    if (status.noLightmapCount > 0u) {
        ImGui::PushStyleColor(ImGuiCol_Text, palette.warning);
        ImGui::TextWrapped(TEXT::Localize("ui.lightingBake.noLightmapSummaryFormat").c_str(), status.noLightmapCount,
                           status.uncoveredCount);
        ImGui::PopStyleColor();
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", TEXT::Localize("ui.lightingBake.surfaceLightmapHint").c_str());
    ImGui::PopTextWrapPos();
}

} // namespace HIKARI::EDITOR::LIGHTING_BAKE_PANEL
