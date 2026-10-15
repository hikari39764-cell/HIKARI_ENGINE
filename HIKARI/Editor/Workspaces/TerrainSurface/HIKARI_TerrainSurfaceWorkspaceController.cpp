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
#include <string_view>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

namespace HIKARI::EDITOR {

namespace {
struct SculptTool {
    EditorTerrainToolMode mode;
    EditorGlyph glyph;
    const char* id;
    const char* nameKey;
};

// 表の順序が UI の並び。Ridge 以降は専用アイコンが無いため既存の sculpt glyph を流用する。
constexpr SculptTool kSculptTools[] = {
    {EditorTerrainToolMode::Raise, EditorGlyph::TerrainRaise, "R##t", "ui.terrain.toolRaise"},
    {EditorTerrainToolMode::Lower, EditorGlyph::TerrainLower, "L##t", "ui.terrain.toolLower"},
    {EditorTerrainToolMode::Smooth, EditorGlyph::TerrainSmooth, "S##t", "ui.terrain.toolSmooth"},
    {EditorTerrainToolMode::Flatten, EditorGlyph::TerrainFlatten, "F##t", "ui.terrain.toolFlatten"},
    {EditorTerrainToolMode::Mound, EditorGlyph::TerrainMound, "M##t", "ui.terrain.toolMound"},
    {EditorTerrainToolMode::Pit, EditorGlyph::TerrainPit, "P##t", "ui.terrain.toolPit"},
    {EditorTerrainToolMode::Ridge, EditorGlyph::TerrainMound, "G##t", "ui.terrain.toolRidge"},
    {EditorTerrainToolMode::Terrace, EditorGlyph::TerrainFlatten, "T##t", "ui.terrain.toolTerrace"},
    {EditorTerrainToolMode::Shaft, EditorGlyph::TerrainPit, "H##t", "ui.terrain.toolShaft"},
    {EditorTerrainToolMode::Hole, EditorGlyph::TerrainPit, "O##t", "ui.terrain.toolHole"},
    {EditorTerrainToolMode::CliffOpening, EditorGlyph::TerrainPit, "C##t", "ui.terrain.toolCliffOpening"},
    {EditorTerrainToolMode::Ramp, EditorGlyph::TerrainFlatten, "A##t", "ui.terrain.toolRamp"},
    {EditorTerrainToolMode::Noise, EditorGlyph::TerrainMound, "N##t", "ui.terrain.toolNoise"},
};
// これより後ろのツールは「その他のツール」折りたたみに入れる。
constexpr std::size_t kPrimarySculptToolCount = 4u;

// 地形表面（砂・雪・土・草・岩）の表示名とペイントモード。preset 名は id ではなくローカライズ表示にする。
struct SurfaceEntry {
    const char* id;
    EditorTerrainPaintMode paint;
    const char* labelKey;
};

constexpr SurfaceEntry kSurfaceEntries[] = {
    {"sand", EditorTerrainPaintMode::Sand, "ui.terrain.sand"},
    {"snow", EditorTerrainPaintMode::Snow, "ui.terrain.snow"},
    {"dirt", EditorTerrainPaintMode::Dirt, "ui.terrain.dirt"},
    {"grass", EditorTerrainPaintMode::Grass, "ui.terrain.grass"},
    {"rock", EditorTerrainPaintMode::Rock, "ui.terrain.rock"},
};

const char* SurfaceLabelKey(std::string_view presetId) {
    for (const SurfaceEntry& entry : kSurfaceEntries) {
        if (presetId == entry.id) {
            return entry.labelKey;
        }
    }
    return "ui.terrain.surfacePreset";
}

template <typename Enum>
struct LabeledValue {
    Enum value;
    const char* key;
};

constexpr LabeledValue<TERRAIN::TerrainProceduralMode> kProceduralModes[] = {
    {TERRAIN::TerrainProceduralMode::NoiseHills, "ui.terrain.proceduralNoiseHills"},
    {TERRAIN::TerrainProceduralMode::IslandCoast, "ui.terrain.proceduralIslandCoast"},
    {TERRAIN::TerrainProceduralMode::Ridge, "ui.terrain.proceduralRidge"},
    {TERRAIN::TerrainProceduralMode::BasinCrater, "ui.terrain.proceduralBasinCrater"},
    {TERRAIN::TerrainProceduralMode::Dunes, "ui.terrain.proceduralDunes"},
};

constexpr LabeledValue<TERRAIN::TerrainProceduralBlendMode> kProceduralBlendModes[] = {
    {TERRAIN::TerrainProceduralBlendMode::Add, "ui.terrain.proceduralBlendAdd"},
    {TERRAIN::TerrainProceduralBlendMode::Replace, "ui.terrain.proceduralBlendReplace"},
    {TERRAIN::TerrainProceduralBlendMode::Max, "ui.terrain.proceduralBlendMax"},
};

struct DeformSlider {
    const char* key;
    float TERRAIN::DeformParams::* value;
    float minimum;
    float maximum;
    const char* format;
};

// responseResolutionScale（整数）はこの表の 3 行目の後に挿入する。
constexpr DeformSlider kDeformSliders[] = {
    {"ui.terrain.maxDepth", &TERRAIN::DeformParams::maxDepth, 0.0f, 2.0f, "%.3f"},
    {"ui.terrain.stampRadius", &TERRAIN::DeformParams::stampRadius, 0.05f, 2.0f, "%.3f"},
    {"ui.terrain.rimStrength", &TERRAIN::DeformParams::rimStrength, 0.0f, 1.0f, "%.3f"},
    {"ui.terrain.restDepthScale", &TERRAIN::DeformParams::restDepthScale, 0.0f, 1.0f, "%.3f"},
    {"ui.terrain.minimumDepth", &TERRAIN::DeformParams::minimumDepth, 0.0f, 0.5f, "%.3f m"},
    {"ui.terrain.settleStartRatio", &TERRAIN::DeformParams::settleStartRatio, 0.0f, 1.0f, "%.3f"},
    {"ui.terrain.settleSeconds", &TERRAIN::DeformParams::settleSeconds, 0.0f, 1.0f, "%.3f s"},
    {"ui.terrain.impactDepthScale", &TERRAIN::DeformParams::impactDepthScale, 0.0f, 0.2f, "%.3f"},
    {"ui.terrain.rimWidthScale", &TERRAIN::DeformParams::rimWidthScale, 0.05f, 1.0f, "%.3f"},
    {"ui.terrain.refill", &TERRAIN::DeformParams::refillPerSecond, 0.0f, 1.0f, "%.4f"},
    {"ui.terrain.diffusion", &TERRAIN::DeformParams::diffusionPerSecond, 0.0f, 8.0f, "%.3f"},
    {"ui.terrain.plasticity", &TERRAIN::DeformParams::plasticity, 0.0f, 1.0f, "%.3f"},
    {"ui.terrain.plasticRefill", &TERRAIN::DeformParams::plasticRefillPerSecond, 0.0f, 0.1f, "%.4f"},
    {"ui.terrain.recoveryDelay", &TERRAIN::DeformParams::recoveryDelaySeconds, 0.0f, 30.0f, "%.2f s"},
};
constexpr std::size_t kDeformSlidersBeforeResponseScale = 3u;

// 1 フレームの描画で各ウィンドウが共有する編集対象と結果。
struct TerrainDrawFrame {
    DocumentSceneBase& scene;
    EditorContext& context;
    TerrainSurfaceWorkspaceResult result{};
    GameObject* selected = nullptr;
    TerrainSurfaceComponent* terrain = nullptr;
    SceneComponentData* documentComponent = nullptr;
    bool canAuthor = false;
    bool runtimeComponentChanged = false;
    bool discreteFillTransaction = false;
    bool settingsEditActive = false;
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

void ResetProceduralPreview(EditorTerrainToolState& tool) {
    tool.proceduralPreview = {};
    tool.proceduralPreviewRequested = false;
    tool.proceduralPreviewObjectId = {};
    tool.proceduralPreviewTerrainRevision = 0u;
}

void ClearPendingToolInput(EditorTerrainToolState& tool) {
    tool.rampAnchorSet = false;
    tool.sampleHeightRequested = false;
}

template <typename Enum, std::size_t Count>
bool LabeledCombo(const char* labelKey, Enum& value, const LabeledValue<Enum> (&items)[Count]) {
    std::vector<std::string> labels{};
    std::vector<const char*> itemLabels{};
    labels.reserve(Count);
    itemLabels.reserve(Count);
    int index = 0;
    for (std::size_t i = 0; i < Count; ++i) {
        labels.push_back(TEXT::Localize(items[i].key));
        itemLabels.push_back(labels.back().c_str());
        if (items[i].value == value) {
            index = static_cast<int>(i);
        }
    }
    if (!ImGui::Combo(TEXT::Localize(labelKey).c_str(), &index, itemLabels.data(),
                      static_cast<int>(itemLabels.size()))) {
        return false;
    }
    value = items[std::clamp(index, 0, static_cast<int>(Count) - 1)].value;
    return true;
}

void ResolveTarget(TerrainDrawFrame& frame) {
    EditorTerrainToolState& tool = frame.context.terrainTool;
    GameObject* selected =
        tool.targetObjectId.value != 0u ? frame.scene.GetWorld().FindObject(tool.targetObjectId) : nullptr;
    if (selected == nullptr || selected->GetComponent<TerrainSurfaceComponent>() == nullptr) {
        GameObject* activeSelection = frame.context.selection.selectedObject;
        if (activeSelection != nullptr && activeSelection->GetComponent<TerrainSurfaceComponent>() != nullptr) {
            selected = activeSelection;
            tool.targetObjectId = activeSelection->GetDocumentId();
        } else {
            selected = nullptr;
            tool.targetObjectId = {};
        }
    }
    frame.selected = selected;
    frame.terrain = selected != nullptr ? selected->GetComponent<TerrainSurfaceComponent>() : nullptr;
    if (frame.terrain != nullptr) {
        frame.result.objectId = selected->GetDocumentId();
        if (SceneObjectData* documentObject =
                FindSceneObjectData(frame.scene.GetSceneDocument(), frame.result.objectId)) {
            for (SceneComponentData& component : documentObject->components) {
                if (component.type == "TerrainSurfaceComponent") {
                    frame.documentComponent = &component;
                    frame.result.componentId = component.id;
                    break;
                }
            }
        }
    }
    frame.canAuthor = frame.terrain != nullptr && frame.documentComponent != nullptr &&
                      frame.result.objectId.value != 0u && frame.result.componentId.value != 0u;
    if (!frame.canAuthor || tool.proceduralPreviewObjectId != frame.result.objectId ||
        (frame.terrain != nullptr && tool.proceduralPreviewTerrainRevision != frame.terrain->GetRevision())) {
        ResetProceduralPreview(tool);
    }
    frame.result.terrainTargetObjectId = frame.result.objectId;
    if (frame.canAuthor) {
        // Runtime component は既定値を解決済み。小さな settings view を直列化すれば、
        // chunk 配列を複製せずに Undo 用の完全な patch になる。
        frame.terrain->SerializeSettings(frame.result.beforePatch);
    }
}

// 地形全体の編集（fill / 生成）を keyed delta として document と履歴へ渡す。
// ボタン操作は独立した履歴境界にし、直前の slider 結合を引き継がない。
template <typename Edit>
bool CommitGeometryEdit(TerrainDrawFrame& frame, const char* historyKey, Edit&& edit) {
    TerrainSurfaceComponent& terrain = *frame.terrain;
    terrain.BeginGeometryEditBatch();
    const bool changed = edit();
    (void)terrain.EndGeometryEditBatch();
    nlohmann::json beforeDelta{};
    nlohmann::json afterDelta{};
    const bool hasDelta = terrain.ConsumeGeometryEditDelta(beforeDelta, afterDelta);
    frame.discreteFillTransaction = true;
    if (changed && hasDelta && ApplyTerrainSurfaceEditDelta(frame.documentComponent->properties, afterDelta)) {
        frame.result.beforePatch = std::move(beforeDelta);
        frame.result.afterPatch = std::move(afterDelta);
        frame.result.componentChanged = true;
        frame.result.terrainDelta = true;
        frame.result.historyLabel = TEXT::Localize(historyKey);
        return true;
    }
    if (changed) {
        // keyed delta が document に受理されるまで runtime の変更は仮。
        // 履歴へ渡せなかった場合は document の正本へ戻す。
        terrain.Deserialize(frame.documentComponent->properties);
    }
    return false;
}

void DrawPageButtons(EditorTerrainToolState& tool) {
    const float pageWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    const auto pageButton = [&](EditorTerrainWorkspacePage page, const char* labelKey) {
        const bool selectedPage = tool.page == page;
        if (selectedPage) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.70f, 0.92f));
        }
        const bool pressed = ImGui::Button(
            (TEXT::Localize(labelKey) + "##TerrainPage/" + std::to_string(static_cast<uint32_t>(page))).c_str(),
            ImVec2(pageWidth, 0.0f));
        if (selectedPage) {
            ImGui::PopStyleColor();
        }
        if (!pressed) {
            return;
        }
        tool.page = page;
        if (page == EditorTerrainWorkspacePage::Sculpt) {
            tool.paint = EditorTerrainPaintMode::None;
        } else if (page == EditorTerrainWorkspacePage::Paint && tool.paint == EditorTerrainPaintMode::None) {
            tool.paint = EditorTerrainPaintMode::Sand;
        } else if (page == EditorTerrainWorkspacePage::Place || page == EditorTerrainWorkspacePage::Foliage ||
                   page == EditorTerrainWorkspacePage::Generate) {
            tool.active = false;
        }
    };
    pageButton(EditorTerrainWorkspacePage::Sculpt, "ui.terrain.pageSculpt");
    ImGui::SameLine();
    pageButton(EditorTerrainWorkspacePage::Paint, "ui.terrain.pagePaint");
    pageButton(EditorTerrainWorkspacePage::Foliage, "ui.terrain.pageFoliage");
    ImGui::SameLine();
    pageButton(EditorTerrainWorkspacePage::Place, "ui.terrain.pagePlace");
    pageButton(EditorTerrainWorkspacePage::Generate, "ui.terrain.pageGenerate");
}

void DrawSculptTools(EditorTerrainToolState& tool) {
    ImGui::SeparatorText(TEXT::Localize("ui.terrain.tool").c_str());
    const float toolWidth = ImGui::GetFrameHeight() * 1.6f;
    for (std::size_t i = 0; i < std::size(kSculptTools); ++i) {
        const SculptTool& entry = kSculptTools[i];
        if (i == kPrimarySculptToolCount &&
            !ImGui::CollapsingHeader(TEXT::LocalizeWindow("editor.workflow.moreTools", "moreSculptTools").c_str())) {
            break;
        }
        if (i != 0u && i != kPrimarySculptToolCount) {
            NextToolbarItem(toolWidth);
        }
        const bool isActive = tool.active && tool.mode == entry.mode && tool.paint == EditorTerrainPaintMode::None;
        const std::string localizedToolName = TEXT::Localize(entry.nameKey);
        if (IconToggleButton(entry.glyph, entry.id, isActive, {toolWidth, toolWidth}, localizedToolName.c_str())) {
            if (tool.mode != entry.mode) {
                ClearPendingToolInput(tool);
            }
            tool.mode = entry.mode;
            tool.paint = EditorTerrainPaintMode::None;
            tool.active = true;
        }
    }
    const auto modeIndex = static_cast<std::size_t>(tool.mode);
    if (modeIndex < std::size(kSculptTools)) {
        ImGui::Spacing();
        ImGui::TextUnformatted(TEXT::Localize(kSculptTools[modeIndex].nameKey).c_str());
    }
}

void DrawPaintPresets(EditorTerrainToolState& tool) {
    ImGui::SeparatorText(TEXT::Localize("ui.terrain.paintPreset").c_str());
    if (ImGui::RadioButton(TEXT::Localize("ui.terrain.none").c_str(), tool.paint == EditorTerrainPaintMode::None)) {
        tool.paint = EditorTerrainPaintMode::None;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton(TEXT::Localize("ui.terrain.erase").c_str(), tool.paint == EditorTerrainPaintMode::Erase)) {
        tool.paint = EditorTerrainPaintMode::Erase;
        tool.active = true;
    }
    int column = 0;
    for (const SurfaceEntry& entry : kSurfaceEntries) {
        if (TERRAIN::SurfaceLayerRegistry::Get().Find(entry.id) == nullptr) {
            continue;
        }
        if (column++ > 0) {
            ImGui::SameLine();
        }
        if (ImGui::RadioButton(TEXT::Localize(entry.labelKey).c_str(), tool.paint == entry.paint)) {
            tool.paint = entry.paint;
            tool.active = true;
        }
    }
}

bool DrawStrengthPresets(TERRAIN::TerrainBrush& brush) {
    ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.strengthPresets").c_str());
    constexpr float kStrengthPresets[]{0.25f, 1.0f, 4.0f, 8.0f};
    constexpr const char* kStrengthPresetLabels[]{
        "0.25##TerrainStrength/0",
        "1##TerrainStrength/1",
        "4##TerrainStrength/2",
        "8##TerrainStrength/3",
    };
    bool changed = false;
    for (std::size_t presetIndex = 0u; presetIndex < std::size(kStrengthPresets); ++presetIndex) {
        if (presetIndex > 0u) {
            ImGui::SameLine();
        }
        if (ImGui::SmallButton(kStrengthPresetLabels[presetIndex])) {
            brush.strength = kStrengthPresets[presetIndex];
            changed = true;
        }
    }
    return changed;
}

// ツール固有の設定。brush に保存される値を変えた場合だけ true を返す。
bool DrawToolModeSettings(EditorTerrainToolState& tool, TERRAIN::TerrainBrush& brush) {
    bool brushChanged = false;
    switch (tool.mode) {
    case EditorTerrainToolMode::Flatten:
        ImGui::SeparatorText(TEXT::Localize("ui.terrain.flattenSettings").c_str());
        brushChanged |= ImGui::InputFloat(TEXT::Localize("ui.terrain.flattenHeight").c_str(), &brush.flattenHeight,
                                          0.1f, 1.0f, "%.3f m");
        if (ImGui::Button(TEXT::Localize("ui.terrain.sampleHeight").c_str(), ImVec2(-1.0f, 0.0f))) {
            tool.sampleHeightRequested = true;
            tool.active = true;
        }
        break;
    case EditorTerrainToolMode::Shaft:
        ImGui::SeparatorText(TEXT::Localize("ui.terrain.shaftSettings").c_str());
        brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.shaftDepth").c_str(), &brush.shaftDepth, 0.05f,
                                           64.0f, "%.2f m");
        brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.shaftWallWidth").c_str(), &brush.shaftWallWidth,
                                           0.05f, (std::max)(brush.radius, 0.05f), "%.2f m");
        break;
    case EditorTerrainToolMode::CliffOpening: {
        ImGui::SeparatorText(TEXT::Localize("ui.terrain.openingSettings").c_str());
        ImGui::SliderFloat(TEXT::Localize("ui.terrain.openingWidth").c_str(), &tool.openingWidth, 0.10f, 64.0f,
                           "%.2f m");
        ImGui::SliderFloat(TEXT::Localize("ui.terrain.openingHeight").c_str(), &tool.openingHeight, 0.10f, 64.0f,
                           "%.2f m");
        ImGui::SliderFloat(TEXT::Localize("ui.terrain.openingDepth").c_str(), &tool.openingDepth, 0.10f, 64.0f,
                           "%.2f m");
        const float maximumOpeningFeather =
            (std::min)({tool.openingWidth, tool.openingHeight, tool.openingDepth}) * 0.5f;
        ImGui::SliderFloat(TEXT::Localize("ui.terrain.openingFeather").c_str(), &tool.openingFeather, 0.0f,
                           maximumOpeningFeather, "%.2f m");
        ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.openingHint").c_str());
        break;
    }
    case EditorTerrainToolMode::Ramp:
        ImGui::SeparatorText(TEXT::Localize("ui.terrain.rampSettings").c_str());
        ImGui::SliderFloat(TEXT::Localize("ui.terrain.rampHalfWidth").c_str(), &tool.rampHalfWidth, 0.05f, 64.0f,
                           "%.2f m");
        ImGui::SliderFloat(TEXT::Localize("ui.terrain.rampShoulderWidth").c_str(), &tool.rampShoulderWidth, 0.0f, 64.0f,
                           "%.2f m");
        ImGui::SliderFloat(TEXT::Localize("ui.terrain.rampBlend").c_str(), &tool.rampBlendAmount, 0.0f, 1.0f, "%.3f");
        ImGui::TextWrapped(
            "%s",
            TEXT::Localize(tool.rampAnchorSet ? "ui.terrain.rampChooseEnd" : "ui.terrain.rampChooseStart").c_str());
        if (tool.rampAnchorSet && ImGui::Button(TEXT::Localize("ui.terrain.cancelRamp").c_str(), ImVec2(-1.0f, 0.0f))) {
            tool.rampAnchorSet = false;
        }
        break;
    case EditorTerrainToolMode::Noise: {
        ImGui::SeparatorText(TEXT::Localize("ui.terrain.noiseSettings").c_str());
        brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.noiseAmplitude").c_str(), &brush.noiseAmplitude,
                                           0.0f, 4.0f, "%.3f");
        brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.noiseScale").c_str(), &brush.noiseScale, 0.05f,
                                           128.0f, "%.2f m");
        uint32_t noiseSeed = brush.noiseSeed;
        if (ImGui::InputScalar(TEXT::Localize("ui.terrain.noiseSeed").c_str(), ImGuiDataType_U32, &noiseSeed)) {
            brush.noiseSeed = noiseSeed;
            brushChanged = true;
        }
        break;
    }
    case EditorTerrainToolMode::Hole: ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.holeHint").c_str()); break;
    default: break;
    }
    return brushChanged;
}

void DrawBrushSettings(TerrainDrawFrame& frame) {
    EditorTerrainToolState& tool = frame.context.terrainTool;
    ImGui::SeparatorText(TEXT::Localize("ui.terrain.brush").c_str());
    if (frame.terrain == nullptr) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.selectTerrain").c_str());
    } else {
        TERRAIN::TerrainBrush brush = frame.terrain->GetBrush();
        bool brushChanged = false;
        if (tool.sampledHeightReady) {
            brush.flattenHeight = tool.sampledHeight;
            tool.sampledHeightReady = false;
            brushChanged = true;
        }
        brushChanged |=
            ImGui::SliderFloat(TEXT::Localize("ui.terrain.radius").c_str(), &brush.radius, 0.1f, 100.0f, "%.2f m");
        brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.strength").c_str(), &brush.strength, 0.0f,
                                           TERRAIN::kTerrainBrushMaximumStrength, "%.3f");
        brushChanged |= DrawStrengthPresets(brush);
        brushChanged |=
            ImGui::SliderFloat(TEXT::Localize("ui.terrain.hardness").c_str(), &brush.hardness, 0.0f, 1.0f, "%.3f");
        brushChanged |=
            ImGui::SliderFloat(TEXT::Localize("ui.terrain.falloff").c_str(), &brush.falloff, 0.0f, 1.0f, "%.3f");
        brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.ridgeDirection").c_str(),
                                           &brush.ridgeDirectionDegrees, -180.0f, 180.0f, "%.1f");
        brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.ridgeSharpness").c_str(), &brush.ridgeSharpness,
                                           0.05f, 1.0f, "%.3f");
        brushChanged |= ImGui::SliderFloat(TEXT::Localize("ui.terrain.terraceStepHeight").c_str(),
                                           &brush.terraceStepHeight, 0.05f, 8.0f, "%.2f m");
        brushChanged |= DrawToolModeSettings(tool, brush);
        if (brushChanged && frame.canAuthor) {
            frame.settingsEditActive = frame.settingsEditActive || ImGui::GetActiveID() != 0u;
            frame.terrain->SetBrush(brush);
            frame.runtimeComponentChanged = true;
        }
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.brushHint").c_str());
}

void DrawFoliagePage(const TerrainDrawFrame& frame) {
    ImGui::SeparatorText(TEXT::Localize("ui.terrain.grass").c_str());
    ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.foliageHint").c_str());
    if (frame.terrain != nullptr) {
        const TERRAIN::TerrainGrassSettings& grass = frame.terrain->GetGrassSettings();
        ImGui::Text("%s: %u", TEXT::Localize("ui.terrain.placementBudget").c_str(), grass.maxClumps);
        ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.foliageDetailsHint").c_str());
    }
}

void DrawPlacePage(TerrainDrawFrame& frame) {
    EditorTerrainToolState& tool = frame.context.terrainTool;
    ImGui::SeparatorText(TEXT::Localize("ui.terrain.placeObjects").c_str());
    ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.placeHint").c_str());
    ImGui::SliderFloat(TEXT::Localize("ui.terrain.placementOffset").c_str(), &tool.placementOffset, -2.0f, 2.0f,
                       "%.3f m");
    const std::size_t selectedProps = frame.context.selection.GetSelectedObjectCount();
    ImGui::TextDisabled("%s: %zu", TEXT::Localize("ui.terrain.selectedObjects").c_str(), selectedProps);
    ImGui::BeginDisabled(!frame.canAuthor || selectedProps == 0u);
    if (ImGui::Button(TEXT::Localize("ui.terrain.placeOnSurface").c_str(),
                      ImVec2(-1.0f, ImGui::GetFrameHeight() * 1.35f))) {
        frame.result.placeSelectionRequested = true;
        frame.result.placementOffset = tool.placementOffset;
    }
    ImGui::EndDisabled();
}

bool DrawProceduralSpec(TERRAIN::TerrainProceduralSpec& spec, const EditorTerrainToolState& tool,
                        const TerrainSurfaceComponent* terrain) {
    bool changed = LabeledCombo("ui.terrain.proceduralMode", spec.mode, kProceduralModes);
    changed |= LabeledCombo("ui.terrain.proceduralBlend", spec.blendMode, kProceduralBlendModes);
    uint64_t seed = spec.seed;
    if (ImGui::InputScalar(TEXT::Localize("ui.terrain.proceduralSeed").c_str(), ImGuiDataType_U64, &seed)) {
        spec.seed = seed;
        changed = true;
    }
    const auto drag = [&changed](const char* key, float& value, float speed, float minimum, float maximum,
                                 const char* format) {
        changed |= ImGui::DragFloat(TEXT::Localize(key).c_str(), &value, speed, minimum, maximum, format);
    };
    const auto slider = [&changed](const char* key, float& value, float minimum, float maximum, const char* format) {
        changed |= ImGui::SliderFloat(TEXT::Localize(key).c_str(), &value, minimum, maximum, format);
    };
    drag("ui.terrain.proceduralCenterX", spec.centerX, 0.05f, -8192.0f, 8192.0f, "%.2f");
    drag("ui.terrain.proceduralCenterZ", spec.centerZ, 0.05f, -8192.0f, 8192.0f, "%.2f");
    ImGui::BeginDisabled(!tool.hasHit || terrain == nullptr);
    if (ImGui::Button(TEXT::Localize("ui.terrain.proceduralUseHitCenter").c_str())) {
        const MATH::Vec3 localHit = terrain->WorldToLocal(tool.hitPoint);
        spec.centerX = localHit.x;
        spec.centerZ = localHit.z;
        changed = true;
    }
    ImGui::EndDisabled();
    drag("ui.terrain.proceduralExtentX", spec.extentX, 0.05f, 0.5f, 2048.0f, "%.2f m");
    drag("ui.terrain.proceduralExtentZ", spec.extentZ, 0.05f, 0.5f, 2048.0f, "%.2f m");
    drag("ui.terrain.proceduralAmplitude", spec.amplitude, 0.01f, -64.0f, 64.0f, "%.3f m");
    const bool baseHeightEnabled = spec.blendMode != TERRAIN::TerrainProceduralBlendMode::Add;
    ImGui::BeginDisabled(!baseHeightEnabled);
    drag("ui.terrain.proceduralBaseHeight", spec.baseHeight, 0.01f, -64.0f, 64.0f, "%.3f m");
    ImGui::EndDisabled();
    if (!baseHeightEnabled) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.proceduralBaseHeightAddHint").c_str());
    }
    drag("ui.terrain.proceduralScale", spec.scale, 0.05f, 0.25f, 512.0f, "%.2f m");
    slider("ui.terrain.proceduralEdgeFalloff", spec.edgeFalloff, 0.01f, 1.0f, "%.3f");
    slider("ui.terrain.proceduralBlendAmount", spec.blendAmount, 0.0f, 1.0f, "%.3f");
    slider("ui.terrain.proceduralDirection", spec.directionDegrees, -180.0f, 180.0f, "%.1f deg");
    int octaves = static_cast<int>(spec.octaves);
    if (ImGui::SliderInt(TEXT::Localize("ui.terrain.proceduralOctaves").c_str(), &octaves, 1, 8)) {
        spec.octaves = static_cast<uint32_t>(octaves);
        changed = true;
    }
    return changed;
}

void DrawProceduralPreviewSummary(const EditorTerrainToolState& tool, const TerrainSurfaceComponent* terrain) {
    const TERRAIN::TerrainProceduralPreview& preview = tool.proceduralPreview;
    if (terrain != nullptr && preview.budgetExceeded) {
        ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.proceduralBudgetExceeded").c_str());
    } else if (terrain != nullptr && preview.valid) {
        ImGui::Text("%s: %u / %u, %s: %llu", TEXT::Localize("ui.terrain.proceduralPreviewChunks").c_str(),
                    preview.affectedChunks, preview.newChunks,
                    TEXT::Localize("ui.terrain.proceduralPreviewVertices").c_str(),
                    static_cast<unsigned long long>(preview.affectedVertices));
        ImGui::TextDisabled("%s: %.2f .. %.2f", TEXT::Localize("ui.terrain.proceduralPreviewHeight").c_str(),
                            preview.minimumGeneratedHeight, preview.maximumGeneratedHeight);
    } else {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.proceduralNoPreview").c_str());
    }
}

void DrawGeneratePage(TerrainDrawFrame& frame) {
    EditorTerrainToolState& tool = frame.context.terrainTool;
    TerrainSurfaceComponent* terrain = frame.terrain;
    ImGui::SeparatorText(TEXT::Localize("ui.terrain.procedural").c_str());
    TERRAIN::TerrainProceduralSpec& spec = tool.procedural;
    spec = TERRAIN::SanitizeTerrainProceduralSpec(spec);
    if (DrawProceduralSpec(spec, tool, terrain)) {
        ResetProceduralPreview(tool);
    }
    if (ImGui::Button(TEXT::Localize("ui.terrain.proceduralPreview").c_str(),
                      ImVec2(-1.0f, ImGui::GetFrameHeight() * 1.15f))) {
        tool.proceduralPreviewRequested = true;
    }
    if (terrain != nullptr && tool.proceduralPreviewRequested) {
        tool.proceduralPreview = TERRAIN::PreviewTerrainProcedural(terrain->GetSurface(), spec);
        tool.proceduralPreviewObjectId = frame.result.objectId;
        tool.proceduralPreviewTerrainRevision = terrain->GetRevision();
        tool.proceduralPreviewRequested = false;
    }
    DrawProceduralPreviewSummary(tool, terrain);
    ImGui::BeginDisabled(!frame.canAuthor || !tool.proceduralPreview.valid);
    if (ImGui::Button(TEXT::Localize("ui.terrain.proceduralApply").c_str(),
                      ImVec2(-1.0f, ImGui::GetFrameHeight() * 1.35f))) {
        TERRAIN::TerrainProceduralPreview appliedPreview{};
        bool applied = false;
        if (CommitGeometryEdit(frame, "ui.terrain.proceduralHistory", [&] {
                applied = terrain->ApplyProceduralTerrain(spec, appliedPreview);
                return applied;
            })) {
            tool.proceduralPreview = appliedPreview;
            tool.proceduralPreviewObjectId = frame.result.objectId;
            tool.proceduralPreviewTerrainRevision = terrain->GetRevision();
        } else if (applied) {
            ResetProceduralPreview(tool);
        }
    }
    ImGui::EndDisabled();
    ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.proceduralHint").c_str());
}

// 左: ページ切り替えとブラシツール
void DrawToolsWindow(TerrainDrawFrame& frame) {
    EditorTerrainToolState& tool = frame.context.terrainTool;
    if (ImGui::Begin((TEXT::Localize("ui.terrain.tools") + "###TerrainSurface/Tools").c_str())) {
        DrawPageButtons(tool);
        ImGui::Spacing();
        if (frame.selected != nullptr) {
            ImGui::TextDisabled("%s: %s", TEXT::Localize("ui.terrain.activeTarget").c_str(),
                                frame.selected->GetName().c_str());
        } else {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.selectTerrain").c_str());
        }
        switch (tool.page) {
        case EditorTerrainWorkspacePage::Sculpt:
            DrawSculptTools(tool);
            DrawBrushSettings(frame);
            break;
        case EditorTerrainWorkspacePage::Paint:
            DrawPaintPresets(tool);
            DrawBrushSettings(frame);
            break;
        case EditorTerrainWorkspacePage::Foliage: DrawFoliagePage(frame); break;
        case EditorTerrainWorkspacePage::Place: DrawPlacePage(frame); break;
        case EditorTerrainWorkspacePage::Generate: DrawGeneratePage(frame); break;
        }
    }
    ImGui::End();
}

void DrawTerrainStatistics(const TerrainSurfaceComponent& terrain) {
    const TERRAIN::TerrainSurface& surface = terrain.GetSurface();
    const std::size_t chunkCount = surface.chunks.size();
    const std::size_t verticesPerChunk = TERRAIN::TerrainChunkVertexCount(terrain.GetChunkCells());
    const double authoredArea =
        static_cast<double>(chunkCount) * terrain.GetChunkWorldSize() * terrain.GetChunkWorldSize();
    ImGui::SeparatorText(TEXT::Localize("ui.terrain.statistics").c_str());
    ImGui::Text("%s: %zu", TEXT::Localize("ui.terrain.chunkCount").c_str(), chunkCount);
    ImGui::Text("%s: %zu", TEXT::Localize("ui.terrain.vertexCount").c_str(), chunkCount * verticesPerChunk);
    ImGui::Text(TEXT::Localize("editor.ui.s.1f.m2.86e2ad").c_str(), TEXT::Localize("ui.terrain.authoredArea").c_str(),
                authoredArea);
    ImGui::TextDisabled(TEXT::Localize("editor.ui.s.u.x.u.3f.m.32ccae").c_str(),
                        TEXT::Localize("ui.terrain.chunkResolution").c_str(), terrain.GetChunkCells(),
                        terrain.GetChunkCells(), surface.chunkCellSize);
}

// 右上: 地形オブジェクト一覧と表面プリセット
void DrawPresetsWindow(TerrainDrawFrame& frame) {
    EditorTerrainToolState& tool = frame.context.terrainTool;
    if (ImGui::Begin((TEXT::Localize("ui.terrain.presets") + "###TerrainSurface/Presets").c_str())) {
        ImGui::SeparatorText(TEXT::Localize("ui.terrain.objects").c_str());
        frame.scene.GetWorld().ForEachObjectWith<TerrainSurfaceComponent>(
            [&](GameObject& object, TerrainSurfaceComponent&) {
                const bool isSelected = tool.targetObjectId == object.GetDocumentId();
                if (ImGui::Selectable(object.GetName().c_str(), isSelected)) {
                    if (tool.targetObjectId != object.GetDocumentId()) {
                        ClearPendingToolInput(tool);
                    }
                    tool.targetObjectId = object.GetDocumentId();
                }
            });
        if (frame.terrain != nullptr) {
            DrawTerrainStatistics(*frame.terrain);
        }

        ImGui::SeparatorText(TEXT::Localize("ui.terrain.surfacePresets").c_str());
        ImGui::TextWrapped("%s", TEXT::Localize("ui.terrain.presetHint").c_str());
        if (frame.terrain != nullptr) {
            const std::string current = frame.terrain->GetSurface().presetId;
            for (const TERRAIN::SurfacePreset& layer : TERRAIN::SurfaceLayerRegistry::Get().Layers()) {
                const std::string label = TEXT::Localize(SurfaceLabelKey(layer.id)) + "##TerrainPreset/" + layer.id;
                if (ImGui::Selectable(label.c_str(), current == layer.id, ImGuiSelectableFlags_AllowDoubleClick) &&
                    frame.canAuthor) {
                    // Preset の click は地形全体の材質 fill。大きい chunk 配列は複製せず、
                    // component の keyed edit journal から差分だけを履歴へ渡す。
                    CommitGeometryEdit(frame, "ui.terrain.fillLayerHistory",
                                       [&] { return frame.terrain->FillLayer(layer.id); });
                }
            }
        } else {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.selectTerrain").c_str());
        }
    }
    ImGui::End();
}

bool DrawDeformSettings(TERRAIN::DeformParams& deform) {
    if (!ImGui::CollapsingHeader(TEXT::Localize("ui.terrain.deform").c_str())) {
        return false;
    }
    bool changed = false;
    for (std::size_t i = 0; i < std::size(kDeformSliders); ++i) {
        if (i == kDeformSlidersBeforeResponseScale) {
            int responseScale = static_cast<int>(deform.responseResolutionScale);
            if (ImGui::SliderInt(TEXT::Localize("ui.terrain.responseResolutionScale").c_str(), &responseScale, 1, 4)) {
                deform.responseResolutionScale = static_cast<uint32_t>(responseScale);
                changed = true;
            }
        }
        const DeformSlider& slider = kDeformSliders[i];
        changed |= ImGui::SliderFloat(TEXT::Localize(slider.key).c_str(), &(deform.*slider.value), slider.minimum,
                                      slider.maximum, slider.format);
    }
    return changed;
}

// 右下: 表面設定と足跡などの変形パラメータ
void DrawDetailsWindow(TerrainDrawFrame& frame) {
    if (ImGui::Begin((TEXT::Localize("ui.terrain.details") + "###TerrainSurface/Details").c_str())) {
        if (frame.terrain != nullptr) {
            ImGuiInspectorBuilder builder;
            ImGui::BeginDisabled(!frame.canAuthor);
            const bool surfaceChanged = frame.terrain->BuildSurfaceSettingsInspector(
                builder, frame.context.terrainTool.page == EditorTerrainWorkspacePage::Foliage);
            TERRAIN::DeformParams deform = frame.terrain->GetSurface().deform;
            const bool deformChanged = DrawDeformSettings(deform);
            ImGui::EndDisabled();
            if (deformChanged && frame.canAuthor) {
                frame.terrain->SetDeformParams(deform);
            }
            if (surfaceChanged || deformChanged) {
                frame.runtimeComponentChanged = true;
                frame.settingsEditActive |= ImGui::GetActiveID() != 0u;
            }
        } else {
            ImGui::TextDisabled("%s", TEXT::Localize("ui.terrain.selectTerrain").c_str());
        }
    }
    ImGui::End();
}

void FinishSettingsPatch(TerrainDrawFrame& frame) {
    // 地形全体の fill は連続した slider 操作ではなく意味を持つボタン命令。
    // workspace の結合グループから外し、Undo と AI 操作をそれぞれ 1 トランザクションに保つ。
    frame.result.editActive = frame.canAuthor && !frame.discreteFillTransaction && frame.settingsEditActive;
    if (!frame.runtimeComponentChanged || !frame.canAuthor || frame.result.terrainDelta) {
        return;
    }
    // Slider/grass/stylization は settings-only patch を保ち、巨大な
    // chunk array の複製と runtime Deserialize churn を避ける。
    frame.terrain->SerializeSettings(frame.result.afterPatch);
    frame.result.componentChanged = frame.result.afterPatch != frame.result.beforePatch;
    if (frame.result.componentChanged) {
        ApplyTerrainSettingsPatch(frame.documentComponent->properties, frame.result.afterPatch);
    }
}
} // namespace

void TerrainSurfaceWorkspaceController::DrawDockSpace(bool resetDefaultDockLayout) const {
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
}

TerrainSurfaceWorkspaceResult TerrainSurfaceWorkspaceController::Draw(DocumentSceneBase& scene,
                                                                      EditorContext& context) {
    TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
    TerrainDrawFrame frame{scene, context};
    ResolveTarget(frame);
    DrawToolsWindow(frame);
    DrawPresetsWindow(frame);
    DrawDetailsWindow(frame);
    FinishSettingsPatch(frame);
    return frame.result;
}

} // namespace HIKARI::EDITOR
