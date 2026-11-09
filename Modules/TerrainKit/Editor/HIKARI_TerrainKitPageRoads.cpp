#include <algorithm>
#include <string>
#include <string_view>

#include <json.hpp>

#include "Core/Text/HIKARI_HumanizeKey.h"
#include "HIKARI/Project/WaterSource.h"
#include "HIKARI_TerrainKitWorkspacePages.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

// 砂浜：海の高さから上へこの高さまでを砂にする（水の下も砂。草が水の中に生えない）。
constexpr float kBeachAboveSeaMeters = 2.5f;
// 海の高さを引いて変える間は一つの取り消しにまとめる。
constexpr int kSeaLevelMergeGroup = 0x5ea1;

// 引擎の操作の失敗を状態の行へ出す。
class StatusResult final : public IEditorOperationResultSink {
public:
    explicit StatusResult(IEditorWorkspaceCommands& commands) : commands_(commands) {}
    void Receive(bool ok, std::string_view, std::string_view message, std::string_view) override {
        if (!ok) {
            commands_.PublishStatus(EditorWorkspaceStatusSeverity::Error, message);
        }
    }

private:
    IEditorWorkspaceCommands& commands_;
};

// 道・川・湖の曲線の層と海を扱う頁。
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
        if (ui.Section("waterSea", "Sea")) {
            DrawSea(ui, store);
        }
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
    void Execute(std::string_view operation, const nlohmann::json& parameters) {
        if (shared_->commands == nullptr) {
            return;
        }
        StatusResult result(*shared_->commands);
        shared_->commands->ExecuteOperation(operation, parameters.dump(), result);
    }

    // 海は引擎の水面（相机を追う海）。地形はその高さを読んで、砂浜と道案内に使う。
    void DrawSea(IEditorWorkspaceUi& ui, TerrainStore& store) {
        GameProjectOcean ocean{};
        const bool hasSea = store.World().FindOcean(ocean);
        if (hasSea) {
            float level = ocean.level;
            if (ui.InputFloat("seaLevel", "Sea Level (m)", &level) && ocean.sceneObjectId != 0u) {
                Execute("scene.set_transform", {{"objectId", ocean.sceneObjectId},
                                                {"position", {0.0f, level, 0.0f}},
                                                {"mergeGroup", kSeaLevelMergeGroup}});
            }
            seaLevel_ = level;
        } else {
            ui.InputFloat("newSeaLevel", "Sea Level (m)", &seaLevel_);
            if (ui.Button("seaAdd", "Add Sea")) {
                // 水面は引擎の Water 功能の部品。項目で切ってあれば先に入れる（入っていれば何も変わらない）。
                Execute("project.features.edit", {{"enable", {"Water"}}});
                Execute("scene.create_water", {{"name", "Sea"},
                                               {"geometryMode", "camera_centered_ocean"},
                                               {"position", {0.0f, seaLevel_, 0.0f}}});
            }
            ui.Tooltip("A sea that reaches the horizon. Ground below the sea level is under water.");
        }
        if (ui.Button("seaBeach", "Add Beach Rule")) {
            AddBeachRule(store);
        }
        ui.Tooltip("Paints sand from the sea floor up to a little above the sea, so no grass grows in the water.");
    }

    void AddBeachRule(TerrainStore& store) {
        const auto& slots = store.Surfaces().Slots();
        const auto sand = std::find(slots.begin(), slots.end(), std::string("Sand"));
        if (sand == slots.end()) {
            Report("Add the Sand surface to a slot on the Surface page first.");
            return;
        }
        SurfaceRuleSet rules = store.SurfaceRules();
        SurfaceRule beach{};
        beach.name = "Beach";
        beach.surface = static_cast<uint32_t>(sand - slots.begin());
        beach.maxHeightMeters = seaLevel_ + kBeachAboveSeaMeters;
        beach.heightFadeMeters = 1.5f;
        beach.maxSlopeDegrees = 30.0f;
        // 規則は後ろほど上に重なる。草地の規則より後ろに置く。
        rules.rules.push_back(beach);
        ChangeSurfaceRules(*shared_, std::move(rules));
    }

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

    float seaLevel_ = 0.0f;
};

} // namespace

std::unique_ptr<TerrainPage> MakeRoadsPage(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<RoadsPage>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
