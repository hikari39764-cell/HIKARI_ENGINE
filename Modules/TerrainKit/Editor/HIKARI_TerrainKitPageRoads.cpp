#include <algorithm>
#include <string>

#include "Core/Text/HIKARI_HumanizeKey.h"
#include "HIKARI_TerrainKitWorkspacePages.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

// 道・川・湖の曲線の層を扱う頁。
class RoadsPage final : public TerrainPage {
public:
    using TerrainPage::TerrainPage;

    const char* Name() const override { return "Roads and Water"; }
    const char* Help() const override { return "Draw roads, rivers, ramps and lakes in the view."; }
    const char* ToolId() const override { return kSplineToolId; }

    void DrawList(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        const auto layers = store.Data().Stack().layers;
        for (size_t reversed = layers.size(); reversed > 0u; --reversed) {
            const HeightLayer& layer = layers[reversed - 1u];
            if (layer.kind == HeightLayerKind::Spline &&
                ui.Selectable("road" + std::to_string(layer.id), layer.name, shared_->selectedLayer == layer.id)) {
                shared_->selectedLayer = layer.id;
            }
        }
        ui.Separator();
        std::string error{};
        for (const auto& entry : kSplineKindNames) {
            if (ui.Button(std::string("add") + entry.name, "New " + TEXT::HumanizeKey(entry.name))) {
                uint32_t id = 0u;
                if (AddTerrainLayer(*shared_, MakeSplineLayer(entry.value), TerrainData::kAppend, id, error)) {
                    shared_->selectedLayer = id;
                }
            }
            ui.SameLine();
        }
        ui.Text("");
        if (ui.Button("lakeFill", "Fill Basin With Water")) {
            shared_->lakeFillArmed = true;
        }
        ui.Tooltip("Click inside a basin in the view. Water rises to where it would spill over.");
        Report(error);
    }

    void DrawProperties(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        const HeightLayer* selected = store.Data().FindLayer(shared_->selectedLayer);
        if (selected == nullptr || selected->kind != HeightLayerKind::Spline) {
            ui.Text("Pick a road or river on the left, or make a new one.");
            return;
        }
        HeightLayer next = *selected;
        ui.Text(next.name + "   " + std::to_string(next.spline.points.size()) + " points");
        if (DrawSpline(ui, store, next.spline)) {
            std::string error{};
            if (!ChangeSplineLayer(*shared_, next, error)) {
                Report(error);
            }
        }
    }

private:
    bool DrawSpline(IEditorWorkspaceUi& ui, const TerrainStore& store, SplineLayerSettings& spline) {
        bool changed = false;
        float width = spline.points.empty() ? DefaultSplineWidth(spline.kind) : spline.points.front().widthMeters;
        if (ui.SliderFloat("splineWidth", "Width (m)", &width, 0.5f, 512.0f)) {
            for (SplinePoint& point : spline.points) {
                point.widthMeters = width;
            }
            changed = true;
        }
        changed |= ui.SliderFloat("splineFalloff", "Falloff (m)", &spline.falloffMeters, 0.0f, 256.0f);
        changed |= ui.SliderFloat("splineEdge", "Edge Width (m)", &spline.edgeMeters, 0.0f, 64.0f);
        changed |= ui.Checkbox("splinePaint", "Paint Surface", &spline.paint);
        if (spline.paint) {
            changed |= DrawSurfaceChoice(ui, "splineSurface", "Surface", store, spline.surface);
            changed |= DrawSurfaceChoice(ui, "splineEdgeSurface", "Edge Surface", store, spline.edgeSurface);
        }
        if (spline.kind == SplineKind::River || spline.kind == SplineKind::Lake) {
            changed |= ui.SliderFloat("riverDepth", "Depth (m)", &spline.depthMeters, 0.0f, 64.0f);
            changed |= ui.SliderFloat("riverWet", "Wet Bank (m)", &spline.wetMeters, 0.0f, 64.0f);
            changed |= ui.SliderFloat("riverSpeed", "Flow Speed (m/s)", &spline.flowSpeed, 0.0f, 10.0f);
            changed |= ui.SliderFloat("riverClarity", "Clarity (m)", &spline.clarityMeters, 0.1f, 64.0f);
        }
        if (spline.kind == SplineKind::Lake) {
            if (!spline.points.empty()) {
                changed |= ui.InputFloat("lakeLevel", "Water Level (m)", &spline.points.front().position.y);
            }
            changed |= ui.SliderFloat("lakeShelf", "Shore Shelf (m)", &spline.shelfMeters, 0.0f, 64.0f);
            changed |= ui.SliderFloat("lakeNoise", "Shore Noise (m)", &spline.shoreNoiseMeters, 0.0f, 32.0f);
        }
        return changed;
    }
};

} // namespace

std::unique_ptr<TerrainPage> MakeRoadsPage(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<RoadsPage>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
