#include "Editor/Shell/HIKARI_EditorDockLayout.h"
#include "Editor/Workspaces/TerrainSurface/HIKARI_TerrainSurfaceWorkspaceController.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/History/HIKARI_SceneComponentPropertiesHistoryCommand.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
namespace {
EditorGlyph TerrainToolGlyph(EditorTerrainToolMode mode) {
    switch (mode) {
    case EditorTerrainToolMode::Raise: return EditorGlyph::TerrainRaise;
    case EditorTerrainToolMode::Lower: return EditorGlyph::TerrainLower;
    case EditorTerrainToolMode::Smooth: return EditorGlyph::TerrainSmooth;
    case EditorTerrainToolMode::Flatten: return EditorGlyph::TerrainFlatten;
    case EditorTerrainToolMode::Mound: return EditorGlyph::TerrainMound;
    case EditorTerrainToolMode::Pit: return EditorGlyph::TerrainPit;
    // Ridge/Terrace reuse the existing sculpt glyph language until
    // the editor glyph atlas receives dedicated icons.
    case EditorTerrainToolMode::Ridge: return EditorGlyph::TerrainMound;
    case EditorTerrainToolMode::Terrace: return EditorGlyph::TerrainFlatten;
    case EditorTerrainToolMode::Shaft: return EditorGlyph::TerrainPit;
    case EditorTerrainToolMode::Hole: return EditorGlyph::TerrainPit;
    case EditorTerrainToolMode::CliffOpening: return EditorGlyph::TerrainPit;
    case EditorTerrainToolMode::Ramp: return EditorGlyph::TerrainFlatten;
    case EditorTerrainToolMode::Noise: return EditorGlyph::TerrainMound;
    }
    return EditorGlyph::TerrainRaise;
}

constexpr EditorTerrainToolMode kSculptTools[] = {
    EditorTerrainToolMode::Raise,   EditorTerrainToolMode::Lower,        EditorTerrainToolMode::Smooth,
    EditorTerrainToolMode::Flatten, EditorTerrainToolMode::Mound,        EditorTerrainToolMode::Pit,
    EditorTerrainToolMode::Ridge,   EditorTerrainToolMode::Terrace,      EditorTerrainToolMode::Shaft,
    EditorTerrainToolMode::Hole,    EditorTerrainToolMode::CliffOpening, EditorTerrainToolMode::Ramp,
    EditorTerrainToolMode::Noise,
};

void ApplyTerrainSettingsPatch(nlohmann::json& properties, const nlohmann::json& patch) {
    if (!patch.is_object()) {
        return;
    }
    if (!properties.is_object()) {
        properties = nlohmann::json::object();
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        properties[it.key()] = it.value();
    }
}
} // namespace
#endif

void TerrainSurfaceWorkspaceController::DrawDockSpace(bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
    ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
        return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImGui::GetID("HIKARI_TerrainSurfaceDockSpace_v3");
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr || resetDefaultDockLayout) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID centerNode = dockspaceId;
        ImGuiID leftNode = 0u;
        ImGuiID rightNode = 0u;
        ImGuiID rightTop = 0u;
        ImGuiID rightBottom = 0u;
        ImGui::DockBuilderSplitNode(centerNode, ImGuiDir_Left, 0.22f, &leftNode, &centerNode);
        ImGui::DockBuilderSplitNode(centerNode, ImGuiDir_Right, 0.29f, &rightNode, &centerNode);
        ImGui::DockBuilderSplitNode(rightNode, ImGuiDir_Up, 0.48f, &rightTop, &rightBottom);

        ImGui::DockBuilderDockWindow(TEXT::LocalizeWindow("ui.window.gameView", "TerrainSurface/GameView").c_str(),
                                     centerNode);
        ImGui::DockBuilderDockWindow((TEXT::Localize("ui.terrain.tools") + "###TerrainSurface/Tools").c_str(),
                                     leftNode);
        ImGui::DockBuilderDockWindow((TEXT::Localize("ui.terrain.presets") + "###TerrainSurface/Presets").c_str(),
                                     rightTop);
        ImGui::DockBuilderDockWindow((TEXT::Localize("ui.terrain.details") + "###TerrainSurface/Details").c_str(),
                                     rightBottom);
        ImGui::DockBuilderFinish(dockspaceId);
    }
    HIKARI::EDITOR::DrawWorkspaceDockSpace(dockspaceId, viewport);
#else
    (void)resetDefaultDockLayout;
#endif
}

TerrainSurfaceWorkspaceResult TerrainSurfaceWorkspaceController::Draw(DocumentSceneBase& scene,
                                                                      EditorContext& context) {
#if defined(HIKARI_WITH_EDITOR)
    TerrainSurfaceWorkspaceResult result{};
    TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
    const std::vector<TERRAIN::SurfacePreset>& layers = TERRAIN::SurfaceLayerRegistry::Get().Layers();

    GameObject* selected = context.terrainTool.targetObjectId.value != 0u
                               ? scene.GetWorld().FindObject(context.terrainTool.targetObjectId)
                               : nullptr;
    if (selected == nullptr || selected->GetComponent<TerrainSurfaceComponent>() == nullptr) {
        GameObject* activeSelection = context.selection.selectedObject;
        if (activeSelection != nullptr && activeSelection->GetComponent<TerrainSurfaceComponent>() != nullptr) {
            selected = activeSelection;
            context.terrainTool.targetObjectId = activeSelection->GetDocumentId();
        } else {
            selected = nullptr;
            context.terrainTool.targetObjectId = {};
        }
    }
    TerrainSurfaceComponent* terrain =
        selected != nullptr ? selected->GetComponent<TerrainSurfaceComponent>() : nullptr;
    SceneComponentData* documentComponent = nullptr;
    if (selected != nullptr && terrain != nullptr) {
        result.objectId = selected->GetDocumentId();
        SceneObjectData* documentObject = FindSceneObjectData(scene.GetSceneDocument(), result.objectId);
        if (documentObject != nullptr) {
            for (SceneComponentData& component : documentObject->components) {
                if (component.type == "TerrainSurfaceComponent") {
                    documentComponent = &component;
                    result.componentId = component.id;
                    break;
                }
            }
        }
    }
    const bool canAuthor = terrain != nullptr && documentComponent != nullptr && result.objectId.value != 0u &&
                           result.componentId.value != 0u;
    if (!canAuthor || context.terrainTool.proceduralPreviewObjectId != result.objectId ||
        (terrain != nullptr && context.terrainTool.proceduralPreviewTerrainRevision != terrain->GetRevision())) {
        context.terrainTool.proceduralPreview = {};
        context.terrainTool.proceduralPreviewRequested = false;
        context.terrainTool.proceduralPreviewObjectId = {};
        context.terrainTool.proceduralPreviewTerrainRevision = 0u;
    }
    result.terrainTargetObjectId = result.objectId;
    if (canAuthor) {
        // Runtime component has already resolved schema migrations and
        // defaults. Serializing its small settings view gives Undo a
        // complete patch without copying terrain chunk arrays.
        terrain->SerializeSettings(result.beforePatch);
    }
    bool runtimeComponentChanged = false;
    bool discreteFillTransaction = false;
    bool settingsEditActive = false;

    // 左: ブラシツール
    if (ImGui::Begin((TEXT::Localize("ui.terrain.tools") + "###TerrainSurface/Tools").c_str())) {
        const float pageWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        const auto pageButton = [&](EditorTerrainWorkspacePage page, const char* labelKey) {
            const bool selectedPage = context.terrainTool.page == page;
            if (selectedPage) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.70f, 0.92f));
            }
            const bool pressed = ImGui::Button(
                (TEXT::Localize(labelKey) + "##TerrainPage/" + std::to_string(static_cast<uint32_t>(page))).c_str(),
                ImVec2(pageWidth, 0.0f));
            if (selectedPage) {
                ImGui::PopStyleColor();
            }
            if (pressed) {
                context.terrainTool.page = page;
                if (page == EditorTerrainWorkspacePage::Sculpt) {
                    context.terrainTool.paint = EditorTerrainPaintMode::None;
                } else if (page == EditorTerrainWorkspacePage::Paint &&
                           context.terrainTool.paint == EditorTerrainPaintMode::None) {
                    context.terrainTool.paint = EditorTerrainPaintMode::Sand;
                } else if (page == EditorTerrainWorkspacePage::Place || page == EditorTerrainWorkspacePage::Foliage ||
                           page == EditorTerrainWorkspacePage::Generate) {
                    context.terrainTool.active = false;
                }
            }
        };
        pageButton(EditorTerrainWorkspacePage::Sculpt, "ui.terrain.pageSculpt");
        ImGui::SameLine();
        pageButton(EditorTerrainWorkspacePage::Paint, "ui.terrain.pagePaint");
        pageButton(EditorTerrainWorkspacePage::Foliage, "ui.terrain.pageFoliage");
        ImGui::SameLine();
        pageButton(EditorTerrainWorkspacePage::Place, "ui.terrain.pagePlace");
        pageButton(EditorTerrainWorkspacePage::Generate, "ui.terrain.pageGenerate");

        ImGui::Spacing();
        if (selected != nullptr) {
            ImGui::TextDisabled("%s: %s", TEXT::Localize("ui.terrain.activeTarget").c_str(),
                                selected->GetName().c_str());
        } else {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.selectTerrain").c_str());
        }

        if (context.terrainTool.page == EditorTerrainWorkspacePage::Sculpt) {
            ImGui::SeparatorText(TEXT::Localize("ui.terrain.tool").c_str());
            const float toolWidth = ImGui::GetFrameHeight() * 1.6f;
            static const char* kToolIds[] = {"R##t", "L##t", "S##t", "F##t", "M##t", "P##t", "G##t",
                                             "T##t", "H##t", "O##t", "C##t", "A##t", "N##t"};
            static const char* kToolNameKeys[] = {
                "ui.terrain.toolRaise", "ui.terrain.toolLower", "ui.terrain.toolSmooth",       "ui.terrain.toolFlatten",
                "ui.terrain.toolMound", "ui.terrain.toolPit",   "ui.terrain.toolRidge",        "ui.terrain.toolTerrace",
                "ui.terrain.toolShaft", "ui.terrain.toolHole",  "ui.terrain.toolCliffOpening", "ui.terrain.toolRamp",
                "ui.terrain.toolNoise"};
            for (std::size_t i = 0; i < std::size(kSculptTools); ++i) {
                const EditorTerrainToolMode mode = kSculptTools[i];
                const bool isActive = context.terrainTool.active && context.terrainTool.mode == mode &&
                                      context.terrainTool.paint == EditorTerrainPaintMode::None;
                const std::string localizedToolName = TEXT::Localize(kToolNameKeys[i]);
                if (i == 4 && !ImGui::CollapsingHeader(
                                  TEXT::LocalizeWindow("editor.workflow.moreTools", "moreSculptTools").c_str()))
                    break;
                if (i && i != 4)
                    NextToolbarItem(toolWidth);
                if (IconToggleButton(TerrainToolGlyph(mode), kToolIds[i], isActive, {toolWidth, toolWidth},
                                     localizedToolName.c_str())) {
                    if (context.terrainTool.mode != mode) {
                        context.terrainTool.rampAnchorSet = false;
                        context.terrainTool.sampleHeightRequested = false;
                    }
                    context.terrainTool.mode = mode;
                    context.terrainTool.paint = EditorTerrainPaintMode::None;
                    context.terrainTool.active = true;
                }
            }
            const auto modeIndex = static_cast<size_t>(context.terrainTool.mode);
            if (modeIndex < std::size(kToolNameKeys)) {
                ImGui::Spacing();
                ImGui::TextUnformatted(TEXT::Localize(kToolNameKeys[modeIndex]).c_str());
            }
        }

        if (context.terrainTool.page == EditorTerrainWorkspacePage::Paint) {
            ImGui::SeparatorText(TEXT::Localize("ui.terrain.paintPreset").c_str());
            if (ImGui::RadioButton(TEXT::Localize("ui.terrain.none").c_str(),
                                   context.terrainTool.paint == EditorTerrainPaintMode::None)) {
                context.terrainTool.paint = EditorTerrainPaintMode::None;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton(TEXT::Localize("ui.terrain.erase").c_str(),
                                   context.terrainTool.paint == EditorTerrainPaintMode::Erase)) {
                context.terrainTool.paint = EditorTerrainPaintMode::Erase;
                context.terrainTool.active = true;
            }
            // preset 名は id そのものではなくローカライズ表示にする。
            static const struct {
                const char* id;
                EditorTerrainPaintMode mode;
                const char* labelKey;
            } kPaintPresets[] = {
                {"sand", EditorTerrainPaintMode::Sand, "ui.terrain.sand"},
                {"snow", EditorTerrainPaintMode::Snow, "ui.terrain.snow"},
                {"dirt", EditorTerrainPaintMode::Dirt, "ui.terrain.dirt"},
                {"grass", EditorTerrainPaintMode::Grass, "ui.terrain.grass"},
                {"rock", EditorTerrainPaintMode::Rock, "ui.terrain.rock"},
            };
            int paintCol = 0;
            for (const auto& entry : kPaintPresets) {
                if (TERRAIN::SurfaceLayerRegistry::Get().Find(entry.id) == nullptr) {
                    continue;
                }
                if (paintCol++ > 0) {
                    ImGui::SameLine();
                }
                if (ImGui::RadioButton(TEXT::Localize(entry.labelKey).c_str(),
                                       context.terrainTool.paint == entry.mode)) {
                    context.terrainTool.paint = entry.mode;
                    context.terrainTool.active = true;
                }
            }
        }

        if (context.terrainTool.page == EditorTerrainWorkspacePage::Sculpt ||
            context.terrainTool.page == EditorTerrainWorkspacePage::Paint) {
            ImGui::SeparatorText(TEXT::Localize("ui.terrain.brush").c_str());
            if (terrain != nullptr) {
                TERRAIN::TerrainBrush brush = terrain->GetBrush();
                bool brushChanged = false;
                if (context.terrainTool.sampledHeightReady) {
                    brush.flattenHeight = context.terrainTool.sampledHeight;
                    context.terrainTool.sampledHeightReady = false;
                    brushChanged = true;
                }
                brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.radius").c_str(), &brush.radius, 0.1f,
                                                   100.0f, "%.2f m");
                brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.strength").c_str(), &brush.strength, 0.0f,
                                                   TERRAIN::kTerrainBrushMaximumStrength, "%.3f");
                ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.strengthPresets").c_str());
                constexpr float kStrengthPresets[]{0.25f, 1.0f, 4.0f, 8.0f};
                constexpr const char* kStrengthPresetLabels[]{
                    "0.25##TerrainStrength/0",
                    "1##TerrainStrength/1",
                    "4##TerrainStrength/2",
                    "8##TerrainStrength/3",
                };
                for (std::size_t presetIndex = 0u; presetIndex < std::size(kStrengthPresets); ++presetIndex) {
                    if (presetIndex > 0u) {
                        ImGui::SameLine();
                    }
                    const float preset = kStrengthPresets[presetIndex];
                    if (ImGui::SmallButton(kStrengthPresetLabels[presetIndex])) {
                        brush.strength = preset;
                        brushChanged = true;
                    }
                }
                brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.hardness").c_str(), &brush.hardness, 0.0f,
                                                   1.0f, "%.3f");
                brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.falloff").c_str(), &brush.falloff, 0.0f,
                                                   1.0f, "%.3f");
                brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.ridgeDirection").c_str(),
                                                   &brush.ridgeDirectionDegrees, -180.0f, 180.0f, "%.1f");
                brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.ridgeSharpness").c_str(),
                                                   &brush.ridgeSharpness, 0.05f, 1.0f, "%.3f");
                brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.terraceStepHeight").c_str(),
                                                   &brush.terraceStepHeight, 0.05f, 8.0f, "%.2f m");
                if (context.terrainTool.mode == EditorTerrainToolMode::Flatten) {
                    ImGui::SeparatorText(TEXT::Localize("ui.terrain.flattenSettings").c_str());
                    brushChanged |= ImGui::InputFloat(TEXT::Localize("ui.terrain.flattenHeight").c_str(),
                                                      &brush.flattenHeight, 0.1f, 1.0f, "%.3f m");
                    if (ImGui::Button(TEXT::Localize("ui.terrain.sampleHeight").c_str(), ImVec2(-1.0f, 0.0f))) {
                        context.terrainTool.sampleHeightRequested = true;
                        context.terrainTool.active = true;
                    }
                } else if (context.terrainTool.mode == EditorTerrainToolMode::Shaft) {
                    ImGui::SeparatorText(TEXT::Localize("ui.terrain.shaftSettings").c_str());
                    brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.shaftDepth").c_str(),
                                                       &brush.shaftDepth, 0.05f, 64.0f, "%.2f m");
                    brushChanged |=
                        ImGui::SliderFloat(TEXT::Localize("ui.terrain.shaftWallWidth").c_str(), &brush.shaftWallWidth,
                                           0.05f, (std::max)(brush.radius, 0.05f), "%.2f m");
                } else if (context.terrainTool.mode == EditorTerrainToolMode::CliffOpening) {
                    ImGui::SeparatorText(TEXT::Localize("ui.terrain.openingSettings").c_str());
                    ImGui::SliderFloat(TEXT::Localize("ui.terrain.openingWidth").c_str(),
                                       &context.terrainTool.openingWidth, 0.10f, 64.0f, "%.2f m");
                    ImGui::SliderFloat(TEXT::Localize("ui.terrain.openingHeight").c_str(),
                                       &context.terrainTool.openingHeight, 0.10f, 64.0f, "%.2f m");
                    ImGui::SliderFloat(TEXT::Localize("ui.terrain.openingDepth").c_str(),
                                       &context.terrainTool.openingDepth, 0.10f, 64.0f, "%.2f m");
                    const float maximumOpeningFeather =
                        (std::min)({context.terrainTool.openingWidth, context.terrainTool.openingHeight,
                                    context.terrainTool.openingDepth}) *
                        0.5f;
                    ImGui::SliderFloat(TEXT::Localize("ui.terrain.openingFeather").c_str(),
                                       &context.terrainTool.openingFeather, 0.0f, maximumOpeningFeather, "%.2f m");
                    ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.openingHint").c_str());
                } else if (context.terrainTool.mode == EditorTerrainToolMode::Ramp) {
                    ImGui::SeparatorText(TEXT::Localize("ui.terrain.rampSettings").c_str());
                    ImGui::SliderFloat(TEXT::Localize("ui.terrain.rampHalfWidth").c_str(),
                                       &context.terrainTool.rampHalfWidth, 0.05f, 64.0f, "%.2f m");
                    ImGui::SliderFloat(TEXT::Localize("ui.terrain.rampShoulderWidth").c_str(),
                                       &context.terrainTool.rampShoulderWidth, 0.0f, 64.0f, "%.2f m");
                    ImGui::SliderFloat(TEXT::Localize("ui.terrain.rampBlend").c_str(),
                                       &context.terrainTool.rampBlendAmount, 0.0f, 1.0f, "%.3f");
                    ImGui::TextWrapped("%s",
                                       TEXT::Localize(context.terrainTool.rampAnchorSet ? "ui.terrain.rampChooseEnd"
                                                                                        : "ui.terrain.rampChooseStart")
                                           .c_str());
                    if (context.terrainTool.rampAnchorSet &&
                        ImGui::Button(TEXT::Localize("ui.terrain.cancelRamp").c_str(), ImVec2(-1.0f, 0.0f))) {
                        context.terrainTool.rampAnchorSet = false;
                    }
                } else if (context.terrainTool.mode == EditorTerrainToolMode::Noise) {
                    ImGui::SeparatorText(TEXT::Localize("ui.terrain.noiseSettings").c_str());
                    brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.noiseAmplitude").c_str(),
                                                       &brush.noiseAmplitude, 0.0f, 4.0f, "%.3f");
                    brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.noiseScale").c_str(),
                                                       &brush.noiseScale, 0.05f, 128.0f, "%.2f m");
                    uint32_t noiseSeed = brush.noiseSeed;
                    if (ImGui::InputScalar(TEXT::Localize("ui.terrain.noiseSeed").c_str(), ImGuiDataType_U32,
                                           &noiseSeed)) {
                        brush.noiseSeed = noiseSeed;
                        brushChanged = true;
                    }
                } else if (context.terrainTool.mode == EditorTerrainToolMode::Hole) {
                    ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.holeHint").c_str());
                }
                if (brushChanged) {
                    if (canAuthor) {
                        settingsEditActive = settingsEditActive || ImGui::GetActiveID() != 0u;
                        terrain->SetBrush(brush);
                        runtimeComponentChanged = true;
                    }
                }
            } else {
                ImGui::TextDisabled(TEXT::Localize("ui.terrain.selectTerrain").c_str());
            }
            ImGui::Spacing();
            ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.brushHint").c_str());
        }

        if (context.terrainTool.page == EditorTerrainWorkspacePage::Foliage) {
            ImGui::SeparatorText(TEXT::Localize("ui.terrain.grass").c_str());
            ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.foliageHint").c_str());
            if (terrain != nullptr) {
                const TERRAIN::TerrainGrassSettings& grass = terrain->GetGrassSettings();
                ImGui::Text("%s: %u", TEXT::Localize("ui.terrain.placementBudget").c_str(), grass.maxClumps);
                ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.foliageDetailsHint").c_str());
            }
        }

        if (context.terrainTool.page == EditorTerrainWorkspacePage::Place) {
            ImGui::SeparatorText(TEXT::Localize("ui.terrain.placeObjects").c_str());
            ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.placeHint").c_str());
            ImGui::SliderFloat(TEXT::Localize("ui.terrain.placementOffset").c_str(),
                               &context.terrainTool.placementOffset, -2.0f, 2.0f, "%.3f m");
            const std::size_t selectedProps = context.selection.GetSelectedObjectCount();
            ImGui::TextDisabled("%s: %zu", TEXT::Localize("ui.terrain.selectedObjects").c_str(), selectedProps);
            const bool canPlace = canAuthor && selectedProps > 0u;
            ImGui::BeginDisabled(!canPlace);
            if (ImGui::Button(TEXT::Localize("ui.terrain.placeOnSurface").c_str(),
                              ImVec2(-1.0f, ImGui::GetFrameHeight() * 1.35f))) {
                result.placeSelectionRequested = true;
                result.placementOffset = context.terrainTool.placementOffset;
            }
            ImGui::EndDisabled();
        }

        if (context.terrainTool.page == EditorTerrainWorkspacePage::Generate) {
            ImGui::SeparatorText(TEXT::Localize("ui.terrain.procedural").c_str());
            TERRAIN::TerrainProceduralSpec& spec = context.terrainTool.procedural;
            spec = TERRAIN::SanitizeTerrainProceduralSpec(spec);
            bool proceduralChanged = false;
            static const struct {
                TERRAIN::TerrainProceduralMode mode;
                const char* key;
            } kProceduralModes[] = {
                {TERRAIN::TerrainProceduralMode::NoiseHills, "ui.terrain.proceduralNoiseHills"},
                {TERRAIN::TerrainProceduralMode::IslandCoast, "ui.terrain.proceduralIslandCoast"},
                {TERRAIN::TerrainProceduralMode::Ridge, "ui.terrain.proceduralRidge"},
                {TERRAIN::TerrainProceduralMode::BasinCrater, "ui.terrain.proceduralBasinCrater"},
                {TERRAIN::TerrainProceduralMode::Dunes, "ui.terrain.proceduralDunes"},
            };
            int modeIndex = static_cast<int>(spec.mode);
            std::vector<std::string> modeLabels{};
            std::vector<const char*> modeItems{};
            modeLabels.reserve(std::size(kProceduralModes));
            modeItems.reserve(std::size(kProceduralModes));
            for (std::size_t i = 0; i < std::size(kProceduralModes); ++i) {
                modeLabels.push_back(TEXT::Localize(kProceduralModes[i].key));
                modeItems.push_back(modeLabels.back().c_str());
            }
            if (ImGui::Combo(TEXT::Localize("ui.terrain.proceduralMode").c_str(), &modeIndex, modeItems.data(),
                             static_cast<int>(modeItems.size()))) {
                spec.mode =
                    kProceduralModes[std::clamp(modeIndex, 0, static_cast<int>(std::size(kProceduralModes)) - 1)].mode;
                proceduralChanged = true;
            }
            static const struct {
                TERRAIN::TerrainProceduralBlendMode mode;
                const char* key;
            } kBlendModes[] = {
                {TERRAIN::TerrainProceduralBlendMode::Add, "ui.terrain.proceduralBlendAdd"},
                {TERRAIN::TerrainProceduralBlendMode::Replace, "ui.terrain.proceduralBlendReplace"},
                {TERRAIN::TerrainProceduralBlendMode::Max, "ui.terrain.proceduralBlendMax"},
            };
            int blendIndex = static_cast<int>(spec.blendMode);
            std::vector<std::string> blendLabels{};
            std::vector<const char*> blendItems{};
            blendLabels.reserve(std::size(kBlendModes));
            blendItems.reserve(std::size(kBlendModes));
            for (std::size_t i = 0; i < std::size(kBlendModes); ++i) {
                blendLabels.push_back(TEXT::Localize(kBlendModes[i].key));
                blendItems.push_back(blendLabels.back().c_str());
            }
            if (ImGui::Combo(TEXT::Localize("ui.terrain.proceduralBlend").c_str(), &blendIndex, blendItems.data(),
                             static_cast<int>(blendItems.size()))) {
                spec.blendMode =
                    kBlendModes[std::clamp(blendIndex, 0, static_cast<int>(std::size(kBlendModes)) - 1)].mode;
                proceduralChanged = true;
            }
            uint64_t seed = spec.seed;
            if (ImGui::InputScalar(TEXT::Localize("ui.terrain.proceduralSeed").c_str(), ImGuiDataType_U64, &seed)) {
                spec.seed = seed;
                proceduralChanged = true;
            }
            proceduralChanged = ImGui::DragFloat(TEXT::Localize("ui.terrain.proceduralCenterX").c_str(), &spec.centerX,
                                                 0.05f, -8192.0f, 8192.0f, "%.2f") ||
                                proceduralChanged;
            proceduralChanged = ImGui::DragFloat(TEXT::Localize("ui.terrain.proceduralCenterZ").c_str(), &spec.centerZ,
                                                 0.05f, -8192.0f, 8192.0f, "%.2f") ||
                                proceduralChanged;
            ImGui::BeginDisabled(!context.terrainTool.hasHit || terrain == nullptr);
            if (ImGui::Button(TEXT::Localize("ui.terrain.proceduralUseHitCenter").c_str())) {
                const MATH::Vec3 localHit = terrain->WorldToLocal(context.terrainTool.hitPoint);
                spec.centerX = localHit.x;
                spec.centerZ = localHit.z;
                proceduralChanged = true;
            }
            ImGui::EndDisabled();
            proceduralChanged = ImGui::DragFloat(TEXT::Localize("ui.terrain.proceduralExtentX").c_str(), &spec.extentX,
                                                 0.05f, 0.5f, 2048.0f, "%.2f m") ||
                                proceduralChanged;
            proceduralChanged = ImGui::DragFloat(TEXT::Localize("ui.terrain.proceduralExtentZ").c_str(), &spec.extentZ,
                                                 0.05f, 0.5f, 2048.0f, "%.2f m") ||
                                proceduralChanged;
            proceduralChanged = ImGui::DragFloat(TEXT::Localize("ui.terrain.proceduralAmplitude").c_str(),
                                                 &spec.amplitude, 0.01f, -64.0f, 64.0f, "%.3f m") ||
                                proceduralChanged;
            const bool baseHeightEnabled = spec.blendMode != TERRAIN::TerrainProceduralBlendMode::Add;
            ImGui::BeginDisabled(!baseHeightEnabled);
            proceduralChanged = ImGui::DragFloat(TEXT::Localize("ui.terrain.proceduralBaseHeight").c_str(),
                                                 &spec.baseHeight, 0.01f, -64.0f, 64.0f, "%.3f m") ||
                                proceduralChanged;
            ImGui::EndDisabled();
            if (!baseHeightEnabled) {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.proceduralBaseHeightAddHint").c_str());
            }
            proceduralChanged = ImGui::DragFloat(TEXT::Localize("ui.terrain.proceduralScale").c_str(), &spec.scale,
                                                 0.05f, 0.25f, 512.0f, "%.2f m") ||
                                proceduralChanged;
            proceduralChanged = ImGui::SliderFloat(TEXT::Localize("ui.terrain.proceduralEdgeFalloff").c_str(),
                                                   &spec.edgeFalloff, 0.01f, 1.0f, "%.3f") ||
                                proceduralChanged;
            proceduralChanged = ImGui::SliderFloat(TEXT::Localize("ui.terrain.proceduralBlendAmount").c_str(),
                                                   &spec.blendAmount, 0.0f, 1.0f, "%.3f") ||
                                proceduralChanged;
            proceduralChanged = ImGui::SliderFloat(TEXT::Localize("ui.terrain.proceduralDirection").c_str(),
                                                   &spec.directionDegrees, -180.0f, 180.0f, "%.1f deg") ||
                                proceduralChanged;
            int octaves = static_cast<int>(spec.octaves);
            if (ImGui::SliderInt(TEXT::Localize("ui.terrain.proceduralOctaves").c_str(), &octaves, 1, 8)) {
                spec.octaves = static_cast<uint32_t>(octaves);
                proceduralChanged = true;
            }
            if (proceduralChanged) {
                context.terrainTool.proceduralPreview = {};
                context.terrainTool.proceduralPreviewRequested = false;
                context.terrainTool.proceduralPreviewObjectId = {};
                context.terrainTool.proceduralPreviewTerrainRevision = 0u;
            }
            if (ImGui::Button(TEXT::Localize("ui.terrain.proceduralPreview").c_str(),
                              ImVec2(-1.0f, ImGui::GetFrameHeight() * 1.15f))) {
                context.terrainTool.proceduralPreviewRequested = true;
            }
            if (terrain != nullptr && context.terrainTool.proceduralPreviewRequested) {
                context.terrainTool.proceduralPreview = TERRAIN::PreviewTerrainProcedural(terrain->GetSurface(), spec);
                context.terrainTool.proceduralPreviewObjectId = result.objectId;
                context.terrainTool.proceduralPreviewTerrainRevision = terrain->GetRevision();
                context.terrainTool.proceduralPreviewRequested = false;
            }
            if (terrain != nullptr && context.terrainTool.proceduralPreview.budgetExceeded) {
                ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.proceduralBudgetExceeded").c_str());
            } else if (terrain != nullptr && context.terrainTool.proceduralPreview.valid) {
                const TERRAIN::TerrainProceduralPreview& preview = context.terrainTool.proceduralPreview;
                if (preview.valid) {
                    ImGui::Text("%s: %u / %u, %s: %llu", TEXT::Localize("ui.terrain.proceduralPreviewChunks").c_str(),
                                preview.affectedChunks, preview.newChunks,
                                TEXT::Localize("ui.terrain.proceduralPreviewVertices").c_str(),
                                static_cast<unsigned long long>(preview.affectedVertices));
                    ImGui::TextDisabled("%s: %.2f .. %.2f",
                                        TEXT::Localize("ui.terrain.proceduralPreviewHeight").c_str(),
                                        preview.minimumGeneratedHeight, preview.maximumGeneratedHeight);
                }
            } else {
                ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.proceduralNoPreview").c_str());
            }
            const bool canGenerate = canAuthor && context.terrainTool.proceduralPreview.valid;
            ImGui::BeginDisabled(!canGenerate);
            if (ImGui::Button(TEXT::Localize("ui.terrain.proceduralApply").c_str(),
                              ImVec2(-1.0f, ImGui::GetFrameHeight() * 1.35f))) {
                terrain->BeginGeometryEditBatch();
                TERRAIN::TerrainProceduralPreview appliedPreview{};
                const bool applied = terrain->ApplyProceduralTerrain(spec, appliedPreview);
                (void)terrain->EndGeometryEditBatch();
                nlohmann::json beforeDelta{};
                nlohmann::json afterDelta{};
                const bool hasDelta = terrain->ConsumeGeometryEditDelta(beforeDelta, afterDelta);
                if (applied && hasDelta && ApplyTerrainSurfaceEditDelta(documentComponent->properties, afterDelta)) {
                    result.beforePatch = std::move(beforeDelta);
                    result.afterPatch = std::move(afterDelta);
                    result.componentChanged = true;
                    result.terrainDelta = true;
                    result.historyLabel = TEXT::Localize("ui.terrain.proceduralHistory");
                    context.terrainTool.proceduralPreview = appliedPreview;
                    context.terrainTool.proceduralPreviewObjectId = result.objectId;
                    context.terrainTool.proceduralPreviewTerrainRevision = terrain->GetRevision();
                } else if (applied) {
                    // Runtime mutation is provisional until the keyed
                    // document delta is admitted. Restore the document's
                    // authoritative state if history serialization fails.
                    terrain->Deserialize(documentComponent->properties);
                    context.terrainTool.proceduralPreview = {};
                    context.terrainTool.proceduralPreviewObjectId = {};
                    context.terrainTool.proceduralPreviewTerrainRevision = 0u;
                }
                discreteFillTransaction = true;
            }
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.proceduralHint").c_str());
        }
    }
    ImGui::End();

    // 右上: オブジェクト + プリセット
    if (ImGui::Begin((TEXT::Localize("ui.terrain.presets") + "###TerrainSurface/Presets").c_str())) {
        ImGui::SeparatorText(TEXT::Localize("ui.terrain.objects").c_str());
        scene.GetWorld().ForEachObjectWith<TerrainSurfaceComponent>([&](GameObject& object, TerrainSurfaceComponent&) {
            const bool isSelected = context.terrainTool.targetObjectId == object.GetDocumentId();
            if (ImGui::Selectable(object.GetName().c_str(), isSelected)) {
                if (context.terrainTool.targetObjectId != object.GetDocumentId()) {
                    context.terrainTool.rampAnchorSet = false;
                    context.terrainTool.sampleHeightRequested = false;
                }
                context.terrainTool.targetObjectId = object.GetDocumentId();
            }
        });

        if (terrain != nullptr) {
            const TERRAIN::TerrainSurface& surface = terrain->GetSurface();
            const std::size_t chunkCount = surface.chunks.size();
            const std::size_t verticesPerChunk = TERRAIN::TerrainChunkVertexCount(terrain->GetChunkCells());
            const double authoredArea =
                static_cast<double>(chunkCount) * terrain->GetChunkWorldSize() * terrain->GetChunkWorldSize();
            ImGui::SeparatorText(TEXT::Localize("ui.terrain.statistics").c_str());
            ImGui::Text("%s: %zu", TEXT::Localize("ui.terrain.chunkCount").c_str(), chunkCount);
            ImGui::Text("%s: %zu", TEXT::Localize("ui.terrain.vertexCount").c_str(), chunkCount * verticesPerChunk);
            ImGui::Text(TEXT::Localize("editor.ui.s.1f.m2.86e2ad").c_str(),
                        TEXT::Localize("ui.terrain.authoredArea").c_str(), authoredArea);
            ImGui::TextDisabled(TEXT::Localize("editor.ui.s.u.x.u.3f.m.32ccae").c_str(),
                                TEXT::Localize("ui.terrain.chunkResolution").c_str(), terrain->GetChunkCells(),
                                terrain->GetChunkCells(), surface.chunkCellSize);
        }

        ImGui::SeparatorText(TEXT::Localize("ui.terrain.surfacePresets").c_str());
        ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.presetHint").c_str());
        if (terrain != nullptr) {
            const std::string current = terrain->GetSurface().presetId;
            for (const TERRAIN::SurfacePreset& layer : layers) {
                const bool isCurrent = current == layer.id;
                const char* labelKey = layer.id == "sand"    ? "ui.terrain.sand"
                                       : layer.id == "snow"  ? "ui.terrain.snow"
                                       : layer.id == "dirt"  ? "ui.terrain.dirt"
                                       : layer.id == "grass" ? "ui.terrain.grass"
                                       : layer.id == "rock"  ? "ui.terrain.rock"
                                                             : "ui.terrain.surfacePreset";
                const std::string label = TEXT::Localize(labelKey) + "##TerrainPreset/" + layer.id;
                if (ImGui::Selectable(label.c_str(), isCurrent, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (canAuthor) {
                        // Preset の click は地形全体の材質 fill とする。
                        // 大きい chunk 配列の全コピーはせず、component の
                        // keyed edit journal から差分だけを history へ渡す。
                        terrain->BeginGeometryEditBatch();
                        const bool fillChanged = terrain->FillLayer(layer.id);
                        (void)terrain->EndGeometryEditBatch();
                        nlohmann::json beforeDelta{};
                        nlohmann::json afterDelta{};
                        const bool hasDelta = terrain->ConsumeGeometryEditDelta(beforeDelta, afterDelta);
                        if (fillChanged && hasDelta &&
                            ApplyTerrainSurfaceEditDelta(documentComponent->properties, afterDelta)) {
                            result.beforePatch = std::move(beforeDelta);
                            result.afterPatch = std::move(afterDelta);
                            result.componentChanged = true;
                            result.terrainDelta = true;
                            result.historyLabel = TEXT::Localize("ui.terrain.fillLayerHistory");
                        }
                        // A no-op fill is still a discrete command
                        // boundary: close any prior slider merge and do
                        // not let the button press inherit it.
                        discreteFillTransaction = true;
                    }
                }
            }
        } else {
            ImGui::TextDisabled(TEXT::Localize("ui.terrain.selectTerrain").c_str());
        }
    }
    ImGui::End();

    // 右下: 詳細
    if (ImGui::Begin((TEXT::Localize("ui.terrain.details") + "###TerrainSurface/Details").c_str())) {
        if (terrain != nullptr) {
            ImGuiInspectorBuilder builder;
            ImGui::BeginDisabled(!canAuthor);
            const bool surfaceChanged = terrain->BuildSurfaceSettingsInspector(
                builder, context.terrainTool.page == EditorTerrainWorkspacePage::Foliage);
            TERRAIN::DeformParams deform = terrain->GetSurface().deform;
            bool deformChanged = false;
            if (ImGui::CollapsingHeader(TEXT::Localize("ui.terrain.deform").c_str())) {
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.maxDepth").c_str(), &deform.maxDepth,
                                                    0.0f, 2.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.stampRadius").c_str(),
                                                    &deform.stampRadius, 0.05f, 2.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.rimStrength").c_str(),
                                                    &deform.rimStrength, 0.0f, 1.0f, "%.3f");
                int responseScale = static_cast<int>(deform.responseResolutionScale);
                if (ImGui::SliderInt(TEXT::Localize("ui.terrain.responseResolutionScale").c_str(), &responseScale, 1,
                                     4)) {
                    deform.responseResolutionScale = static_cast<uint32_t>(responseScale);
                    deformChanged = true;
                }
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.restDepthScale").c_str(),
                                                    &deform.restDepthScale, 0.0f, 1.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.minimumDepth").c_str(),
                                                    &deform.minimumDepth, 0.0f, 0.5f, "%.3f m");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.settleStartRatio").c_str(),
                                                    &deform.settleStartRatio, 0.0f, 1.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.settleSeconds").c_str(),
                                                    &deform.settleSeconds, 0.0f, 1.0f, "%.3f s");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.impactDepthScale").c_str(),
                                                    &deform.impactDepthScale, 0.0f, 0.2f, "%.3f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.rimWidthScale").c_str(),
                                                    &deform.rimWidthScale, 0.05f, 1.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.refill").c_str(),
                                                    &deform.refillPerSecond, 0.0f, 1.0f, "%.4f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.diffusion").c_str(),
                                                    &deform.diffusionPerSecond, 0.0f, 8.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.plasticity").c_str(), &deform.plasticity,
                                                    0.0f, 1.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.plasticRefill").c_str(),
                                                    &deform.plasticRefillPerSecond, 0.0f, 0.1f, "%.4f");
                deformChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.recoveryDelay").c_str(),
                                                    &deform.recoveryDelaySeconds, 0.0f, 30.0f, "%.2f s");
            }

            ImGui::EndDisabled();
            if (deformChanged && canAuthor)
                terrain->SetDeformParams(deform);
            if (surfaceChanged || deformChanged) {
                runtimeComponentChanged = true;
                settingsEditActive |= ImGui::GetActiveID() != 0u;
            }
        } else {
            ImGui::TextDisabled(TEXT::Localize("ui.terrain.selectTerrain").c_str());
        }
    }
    ImGui::End();
    // Whole-layer fill is a semantic button command, not a continuous
    // slider stroke. Keep it out of the workspace merge group so Undo
    // and the AI operation each remain one discrete transaction.
    result.editActive = canAuthor && !discreteFillTransaction && settingsEditActive;
    if (runtimeComponentChanged && canAuthor && !result.terrainDelta) {
        // Slider/grass/stylization は settings-only patch を保ち、巨大な
        // chunk array の複製と runtime Deserialize churn を避ける。
        terrain->SerializeSettings(result.afterPatch);
        result.componentChanged = result.afterPatch != result.beforePatch;
        if (result.componentChanged) {
            ApplyTerrainSettingsPatch(documentComponent->properties, result.afterPatch);
        }
    }
    return result;
#else
    (void)scene;
    (void)context;
    return {};
#endif
}

} // namespace HIKARI::EDITOR
