#include <algorithm>
#include <cmath>
#include <optional>

#include "HIKARI_TerrainKitEditorCommon.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

constexpr EditorWorkspaceColor kSplineColor{0.35f, 0.95f, 0.95f, 1.0f};
constexpr EditorWorkspaceColor kHandleColor{1.0f, 0.95f, 0.3f, 1.0f};
constexpr float kLiftMeters = 0.4f;
// 画面の上で点を掴める大きさ（カメラからの距離の割合）。
constexpr float kPickFraction = 0.02f;
constexpr float kMinimumDragMeters = 0.2f;

// 曲線の層の前後を持つ撤销。瓦片は変わった範囲だけ焼き直す。
class SplineUndo final : public IEditorUndoEntry {
public:
    SplineUndo(HeightLayer before, HeightLayer after) : before_(std::move(before)), after_(std::move(after)) {}
    void Undo() override { (void)ApplySplineLayer(before_); }
    void Redo() override { (void)ApplySplineLayer(after_); }

private:
    HeightLayer before_;
    HeightLayer after_;
};

const HeightLayer* SelectedSpline(const EditorShared& shared) {
    const TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        return nullptr;
    }
    const HeightLayer* layer = store.Data().FindLayer(shared.selectedLayer);
    return layer != nullptr && layer->kind == HeightLayerKind::Spline ? layer : nullptr;
}

class SplineTool final : public IEditorViewportTool {
public:
    explicit SplineTool(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {}
    const char* GetToolId() const noexcept override { return kSplineToolId; }
    const char* GetDisplayName() const noexcept override { return "Terrain Road and River"; }
    void OnDeactivate() override { Finish(); }

    void Update(const EditorViewportInput& input, IEditorViewportCanvas& canvas) override {
        TerrainStore& store = TerrainStore::Get();
        if (shared_->lakeFillArmed) {
            UpdateLakeFill(input, canvas, store);
            return;
        }
        const HeightLayer* layer = SelectedSpline(*shared_);
        if (layer == nullptr) {
            canvas.DrawHint("Road and River: select or add a road, river or ramp layer in the Terrain window.",
                            kSplineColor);
            Finish();
            return;
        }
        canvas.DrawHint("Road and River: click adds a point, drag moves it, Alt + click removes it, wheel changes its "
                        "height",
                        kSplineColor);
        DrawSpline(*layer, input, canvas);
        const float origin[3]{input.rayOrigin.x, input.rayOrigin.y, input.rayOrigin.z};
        const float direction[3]{input.rayDirection.x, input.rayDirection.y, input.rayDirection.z};
        float hit[3]{};
        const bool hasHit = input.pointerInViewport && store.Raycast(origin, direction, 8000.0f, hit);
        if (hasHit && input.primaryPressed && !dragging_.has_value()) {
            Press(*layer, input, hit);
        } else if (dragging_.has_value() && input.primaryDown && hasHit) {
            Drag(*layer, hit);
        }
        if (dragging_.has_value() && !input.primaryDown) {
            Finish();
        }
        // 上の処理で層の配列が差し替わるので読み直す。
        layer = SelectedSpline(*shared_);
        if (layer != nullptr && input.wheel != 0.0f && !input.control && hasHit) {
            AdjustHeight(*layer, input, hit);
        }
    }

private:
    void UpdateLakeFill(const EditorViewportInput& input, IEditorViewportCanvas& canvas, const TerrainStore& store) {
        canvas.DrawHint("Fill Lake: click inside a basin. Water rises to where it would spill over.", kSplineColor);
        float hit[3]{};
        if (!input.primaryPressed || !RayToGround(input, store, hit)) {
            return;
        }
        shared_->lakeFillArmed = false;
        uint32_t layer = 0u;
        std::string error{};
        if (FillLakeAt(*shared_, hit[0], hit[2], layer, error)) {
            shared_->selectedLayer = layer;
        } else {
            Report(false, error);
        }
    }

    std::optional<size_t> NearestPoint(const HeightLayer& layer, const float hit[3],
                                       const EditorViewportInput& input) const {
        const float pick = (std::max)(
            2.0f, std::hypot(hit[0] - input.rayOrigin.x, hit[1] - input.rayOrigin.y, hit[2] - input.rayOrigin.z) *
                      kPickFraction);
        std::optional<size_t> nearest{};
        float best = pick;
        for (size_t index = 0u; index < layer.spline.points.size(); ++index) {
            const MATH::Vec3& point = layer.spline.points[index].position;
            const float distance = std::hypot(point.x - hit[0], point.z - hit[2]);
            if (distance < best) {
                best = distance;
                nearest = index;
            }
        }
        return nearest;
    }

    void DrawSpline(const HeightLayer& layer, const EditorViewportInput& input, IEditorViewportCanvas& canvas) const {
        const PreparedSpline spline = PrepareSpline(layer.spline);
        constexpr size_t kStride = 2u;
        for (size_t index = kStride; index < spline.sections.size(); index += kStride) {
            const SplineSection& a = spline.sections[index - kStride];
            const SplineSection& b = spline.sections[index];
            canvas.DrawLine({a.x, a.y + kLiftMeters, a.z}, {b.x, b.y + kLiftMeters, b.z}, kSplineColor, 2.0f);
        }
        for (const SplinePoint& point : layer.spline.points) {
            const float distance =
                std::hypot(point.position.x - input.rayOrigin.x, point.position.y - input.rayOrigin.y,
                           point.position.z - input.rayOrigin.z);
            canvas.DrawCircle({point.position.x, point.position.y + kLiftMeters, point.position.z},
                              (std::max)(1.0f, distance * kPickFraction * 0.5f), kHandleColor, 2.0f);
        }
    }

    void Press(const HeightLayer& layer, const EditorViewportInput& input, const float hit[3]) {
        std::string error{};
        const std::optional<size_t> nearest = NearestPoint(layer, hit, input);
        HeightLayer next = layer;
        if (input.alt) {
            if (nearest.has_value()) {
                next.spline.points.erase(next.spline.points.begin() + static_cast<std::ptrdiff_t>(*nearest));
                Report(ChangeSplineLayer(*shared_, next, error), error);
            }
            return;
        }
        before_ = layer;
        if (nearest.has_value()) {
            dragging_ = *nearest;
            return;
        }
        SplinePoint point{};
        // 湖の水面は最初の点の高さで決まる。
        const bool joinLevel = layer.spline.kind == SplineKind::Lake && !layer.spline.points.empty();
        point.position = MATH::Vec3{hit[0], joinLevel ? layer.spline.points.front().position.y : hit[1], hit[2]};
        point.widthMeters = layer.spline.points.empty() ? DefaultSplineWidth(layer.spline.kind)
                                                        : layer.spline.points.back().widthMeters;
        next.spline.points.push_back(point);
        dragging_ = next.spline.points.size() - 1u;
        Report(ApplySplineLayer(next), "The terrain layer could not be changed.");
    }

    void Drag(const HeightLayer& layer, const float hit[3]) {
        if (*dragging_ >= layer.spline.points.size()) {
            return;
        }
        const MATH::Vec3 current = layer.spline.points[*dragging_].position;
        if (std::hypot(current.x - hit[0], current.z - hit[2]) < kMinimumDragMeters) {
            return;
        }
        // 下の地形に対する高さを保つ（自分で掘った面に吸い付かない）。
        HeightLayer next = layer;
        const std::optional<float> from = HeightBelowLayer(layer.id, current.x, current.z);
        const std::optional<float> to = HeightBelowLayer(layer.id, hit[0], hit[2]);
        const float lift =
            layer.spline.kind != SplineKind::Lake && from.has_value() && to.has_value() ? *to - *from : 0.0f;
        next.spline.points[*dragging_].position = MATH::Vec3{hit[0], current.y + lift, hit[2]};
        Report(ApplySplineLayer(next), "The terrain layer could not be changed.");
    }

    void AdjustHeight(const HeightLayer& layer, const EditorViewportInput& input, const float hit[3]) {
        const std::optional<size_t> target = dragging_.has_value() ? dragging_ : NearestPoint(layer, hit, input);
        if (!target.has_value() || *target >= layer.spline.points.size()) {
            return;
        }
        HeightLayer next = layer;
        // 湖はどの点を回しても水面（最初の点）が動く。
        const size_t moved = layer.spline.kind == SplineKind::Lake ? 0u : *target;
        next.spline.points[moved].position.y += (input.wheel > 0.0f ? 0.5f : -0.5f) * (input.shift ? 5.0f : 1.0f);
        std::string error{};
        if (dragging_.has_value()) {
            Report(ApplySplineLayer(next), "The terrain layer could not be changed.");
        } else {
            Report(ChangeSplineLayer(*shared_, next, error), error);
        }
    }

    void Finish() {
        if (dragging_.has_value() && before_.has_value()) {
            const HeightLayer* layer =
                TerrainStore::Get().IsOpen() ? TerrainStore::Get().Data().FindLayer(before_->id) : nullptr;
            if (layer != nullptr && !(*layer == *before_) && shared_->services != nullptr) {
                shared_->services->RecordUndo("Edit Terrain Spline", std::make_unique<SplineUndo>(*before_, *layer));
            }
        }
        dragging_.reset();
        before_.reset();
    }

    void Report(bool ok, const std::string& error) const {
        if (!ok && shared_->commands != nullptr) {
            shared_->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, error);
        }
    }

    std::shared_ptr<EditorShared> shared_;
    std::optional<size_t> dragging_{};
    std::optional<HeightLayer> before_{};
};

} // namespace

float DefaultSplineWidth(SplineKind kind) {
    switch (kind) {
    case SplineKind::Road: return 8.0f;
    case SplineKind::River: return 18.0f;
    case SplineKind::Ramp: return 10.0f;
    case SplineKind::Lake: return 1.0f; // 幅は使わない
    }
    return 8.0f;
}

HeightLayer MakeSplineLayer(SplineKind kind) {
    HeightLayer layer{};
    layer.kind = HeightLayerKind::Spline;
    layer.blend = HeightBlendMode::Replace;
    layer.spline.kind = kind;
    switch (kind) {
    case SplineKind::Road:
        layer.name = "Road";
        layer.spline.surface = 8u;
        layer.spline.edgeSurface = 7u;
        break;
    case SplineKind::River:
        layer.name = "River";
        layer.spline.falloffMeters = 16.0f;
        layer.spline.edgeMeters = 5.0f;
        layer.spline.depthMeters = 3.0f;
        layer.spline.wetMeters = 8.0f;
        layer.spline.surface = 8u;
        layer.spline.edgeSurface = 3u;
        break;
    case SplineKind::Ramp:
        layer.name = "Ramp";
        layer.spline.edgeMeters = 0.0f;
        layer.spline.falloffMeters = 10.0f;
        layer.spline.paint = false;
        break;
    case SplineKind::Lake:
        layer.name = "Lake";
        layer.spline.falloffMeters = 24.0f;
        layer.spline.edgeMeters = 6.0f;
        layer.spline.depthMeters = 6.0f;
        layer.spline.wetMeters = 10.0f;
        layer.spline.flowSpeed = 0.12f;
        layer.spline.clarityMeters = 2.5f;
        layer.spline.surface = 8u;
        layer.spline.edgeSurface = 3u;
        break;
    }
    return layer;
}

std::optional<float> HeightBelowLayer(uint32_t layerId, float worldX, float worldZ) {
    const TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        return std::nullopt;
    }
    const TerrainData& data = store.Data();
    const size_t index = data.LayerIndex(layerId);
    const std::vector<HeightLayer>& layers = data.Stack().layers;
    // 層がまだ無ければ（これから足す）全部の合成、一番下の層なら何も無い地面。
    if (index == 0u) {
        return 0.0f;
    }
    const uint32_t through = index == TerrainData::kAppend ? 0u : layers[index - 1u].id;
    const HeightStackEvaluator evaluator(data);
    if (!evaluator.Valid()) {
        return std::nullopt;
    }
    const double spacing = SampleSpacing(data.Grid());
    const SampleRect rect{static_cast<int64_t>(std::llround(worldX / spacing)),
                          static_cast<int64_t>(std::llround(worldZ / spacing)), 1u, 1u};
    std::vector<float> height{};
    if (!evaluator.EvaluateRect(rect, height, nullptr, through)) {
        return std::nullopt;
    }
    return height.front();
}

bool ApplySplineLayer(const HeightLayer& next) {
    TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        return false;
    }
    TerrainData& data = store.Data();
    const HeightLayer* current = data.FindLayer(next.id);
    if (current == nullptr || current->kind != HeightLayerKind::Spline) {
        return false;
    }
    const bool sameBlend = current->enabled == next.enabled && current->opacity == next.opacity &&
                           current->blend == next.blend && current->masked == next.masked;
    const PreparedSpline before = PrepareSpline(current->spline);
    if (!data.SetLayer(next)) {
        return false;
    }
    const PreparedSpline after = PrepareSpline(data.FindLayer(next.id)->spline);
    const TerrainGrid& grid = data.Grid();
    SampleRect rect = SplineChangedRect(before, after, grid);
    if (!sameBlend) {
        const SampleRect first = SplineAffectedRect(before, grid);
        const SampleRect second = SplineAffectedRect(after, grid);
        rect = UnionRects(first, second);
    }
    store.RebakeAfterSplineChange(rect);
    return true;
}

bool ChangeSplineLayer(EditorShared& shared, const HeightLayer& next, std::string& error) {
    const TerrainStore& store = TerrainStore::Get();
    const HeightLayer* current = store.IsOpen() ? store.Data().FindLayer(next.id) : nullptr;
    if (current == nullptr || current->kind != HeightLayerKind::Spline) {
        error = "Unknown terrain spline layer.";
        return false;
    }
    HeightLayer before = *current;
    if (!ApplySplineLayer(next)) {
        error = "The terrain spline could not be changed.";
        return false;
    }
    if (shared.services != nullptr) {
        shared.services->RecordUndo("Edit Terrain Spline",
                                    std::make_unique<SplineUndo>(std::move(before), *store.Data().FindLayer(next.id)));
    }
    return true;
}

bool FillLakeAt(EditorShared& shared, float worldX, float worldZ, uint32_t& outLayer, std::string& error) {
    constexpr float kMinimumLakeArea = 300.0f;
    const TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        error = "The terrain is not open.";
        return false;
    }
    const TerrainAnalysis coarse = CollectTerrainCoarseHeights(store);
    LakeFill fill{};
    if (!FindLakeFill(coarse.heights, coarse.width, coarse.height, coarse.originX, coarse.originZ, coarse.cellMeters,
                      worldX, worldZ, kMinimumLakeArea, fill)) {
        error = "There is no basin here: water would run off. Add a lake layer and draw it instead; it digs its own "
                "basin.";
        return false;
    }
    HeightLayer layer = MakeSplineLayer(SplineKind::Lake);
    layer.spline.depthMeters = 0.0f; // 窪地はもうあるので掘らない
    for (const MATH::Vec2& point : fill.shore) {
        layer.spline.points.push_back(SplinePoint{MATH::Vec3{point.x, fill.level, point.y}, 1.0f});
    }
    return AddTerrainLayer(shared, std::move(layer), TerrainData::kAppend, outLayer, error);
}

std::unique_ptr<IEditorViewportTool> MakeSplineTool(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<SplineTool>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
