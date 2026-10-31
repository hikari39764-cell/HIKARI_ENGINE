// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <utility>

#include "Authoring/HIKARI_FieldInspector.h"

namespace HIKARI {

void TerrainSurfaceComponent::BuildInspector(IInspectorBuilder& builder) {
    // chunk を除いた設定だけを複製して編集する。反映は設定ごとの setter を通す（値が同じなら何もしない）。
    TERRAIN::TerrainSurfaceParams next = surface_;
    if (!InspectFields(builder, next, TERRAIN::kTerrainSurfaceFields)) {
        return;
    }
    if (next.presetId != surface_.presetId) {
        // 表面の preset を替えると、全 chunk の層を塗り替える。
        FillLayer(next.presetId);
    }
    SetShorelineParams(next.shoreline);
    SetStylizationSettings(next.stylization);
    SetGrassSettings(next.grass);
    const bool brushChanged = next.brush != surface_.brush;
    SetBrush(next.brush);
    const bool heightScaleChanged = next.heightScale != surface_.heightScale;
    surface_.heightScale = next.heightScale;
    if (brushChanged || heightScaleChanged) {
        NotifyRenderStateDirty();
    }
}

bool TerrainSurfaceComponent::BuildSurfaceSettingsInspector(IInspectorBuilder& builder, bool expandGrass) {
    bool anyChanged = false;
    if (builder.Section("Shoreline", true)) {
        TERRAIN::ShorelineParams next = surface_.shoreline;
        if (InspectFields(builder, next, TERRAIN::kShorelineFields)) {
            SetShorelineParams(next);
            anyChanged = true;
        }
    }
    if (builder.Section("Stylization", true)) {
        TERRAIN::TerrainStylizationSettings next = surface_.stylization;
        if (InspectFields(builder, next, TERRAIN::kTerrainStylizationFields)) {
            SetStylizationSettings(next);
            anyChanged = true;
        }
    }
    if (builder.Section("Grass", expandGrass)) {
        TERRAIN::TerrainGrassSettings next = surface_.grass;
        if (InspectFields(builder, next, TERRAIN::kTerrainGrassFields)) {
            SetGrassSettings(next);
            anyChanged = true;
        }
    }
    return anyChanged;
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
