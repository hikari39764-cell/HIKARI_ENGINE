#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <numbers>
#include <optional>

#include "HIKARI_TerrainKitEditorCommon.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

constexpr EditorWorkspaceColor kBrushColor{1.0f, 0.82f, 0.25f, 1.0f};
constexpr EditorWorkspaceColor kMaskColor{0.35f, 0.85f, 1.0f, 1.0f};
constexpr EditorWorkspaceColor kStampColor{0.55f, 1.0f, 0.55f, 1.0f};
constexpr EditorWorkspaceColor kPaintColor{1.0f, 0.55f, 0.85f, 1.0f};

} // namespace

void DrawGroundCircle(IEditorViewportCanvas& canvas, const TerrainStore& store, const float center[3], float radius,
                      EditorWorkspaceColor color) {
    constexpr int kSegments = 72;
    GameProjectFloat3 previous{};
    for (int index = 0; index <= kSegments; ++index) {
        const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / kSegments;
        const float x = center[0] + std::cos(angle) * radius;
        const float z = center[2] + std::sin(angle) * radius;
        const GameProjectFloat3 point{x, store.HeightAt(x, z).value_or(center[1]) + 0.2f, z};
        if (index > 0) {
            canvas.DrawLine(previous, point, color, 2.0f);
        }
        previous = point;
    }
}

bool RayToGround(const EditorViewportInput& input, const TerrainStore& store, float hit[3]) {
    const float origin[3]{input.rayOrigin.x, input.rayOrigin.y, input.rayOrigin.z};
    const float direction[3]{input.rayDirection.x, input.rayDirection.y, input.rayDirection.z};
    return input.pointerInViewport && store.Raycast(origin, direction, 8000.0f, hit);
}

void AdjustBrushRadius(const EditorViewportInput& input, float& radius) {
    if (input.control && input.wheel != 0.0f) {
        radius = std::clamp(radius * (input.wheel > 0.0f ? 1.15f : 0.87f), 1.0f, 2048.0f);
    }
}

namespace {

// 押している間、毎フレーム適用する工具の共通部分。一筆で撤销一件。
class StrokeTool : public IEditorViewportTool {
public:
    explicit StrokeTool(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {}
    void OnDeactivate() override { FinishStroke(); }

    void Update(const EditorViewportInput& input, IEditorViewportCanvas& canvas) override {
        TerrainStore& store = TerrainStore::Get();
        canvas.DrawHint(Hint(), Color());
        if (!store.IsOpen()) {
            return;
        }
        AdjustBrushRadius(input, Radius());
        float hit[3]{};
        const bool hasHit = RayToGround(input, store, hit);
        if (hasHit) {
            DrawGroundCircle(canvas, store, hit, Radius(), Color());
        }
        if (input.primaryPressed && hasHit && recorder_ == nullptr) {
            recorder_ = std::make_unique<TerrainEditRecorder>(store.Data());
            firstPoint_ = MATH::Vec2{hit[0], hit[2]};
            lastPoint_ = firstPoint_;
            if (AppliesWhileDragging()) {
                ApplyTimed(store, *lastPoint_, input);
            }
        } else if (recorder_ != nullptr && input.primaryDown && hasHit) {
            if (AppliesWhileDragging()) {
                ApplyTimed(store, MATH::Vec2{hit[0], hit[2]}, input);
            } else {
                lastPoint_ = MATH::Vec2{hit[0], hit[2]};
            }
        }
        if (recorder_ != nullptr && !AppliesWhileDragging() && firstPoint_.has_value() && lastPoint_.has_value()) {
            const GameProjectFloat3 from{firstPoint_->x, store.HeightAt(firstPoint_->x, firstPoint_->y).value_or(0.0f),
                                         firstPoint_->y};
            const GameProjectFloat3 to{lastPoint_->x, store.HeightAt(lastPoint_->x, lastPoint_->y).value_or(0.0f),
                                       lastPoint_->y};
            canvas.DrawLine(from, to, Color(), 2.0f);
        }
        if (recorder_ != nullptr && !input.primaryDown) {
            if (!AppliesWhileDragging() && firstPoint_.has_value() && lastPoint_.has_value()) {
                const std::array<MATH::Vec2, 2> path{*firstPoint_, *lastPoint_};
                ApplyPath(store, path, input, *recorder_);
            }
            FinishStroke();
        }
    }

protected:
    virtual std::string_view Hint() const = 0;
    virtual EditorWorkspaceColor Color() const = 0;
    virtual float& Radius() = 0;
    virtual const char* UndoLabel() const = 0;
    // 坂のように、離した時に始点から終点まで一度に適用する工具は false。
    virtual bool AppliesWhileDragging() const { return true; }
    virtual void ApplyPath(TerrainStore& store, std::span<const MATH::Vec2> path, const EditorViewportInput& input,
                           TerrainEditRecorder& recorder) = 0;

    std::shared_ptr<EditorShared> shared_;

private:
    void ApplyTimed(TerrainStore& store, MATH::Vec2 point, const EditorViewportInput& input) {
        const auto started = std::chrono::steady_clock::now();
        const std::array<MATH::Vec2, 2> path{*lastPoint_, point};
        ApplyPath(store, path, input, *recorder_);
        lastPoint_ = point;
        const double milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        shared_->brushFrameTotalMs += milliseconds;
        shared_->brushFrameMaxMs = (std::max)(shared_->brushFrameMaxMs, milliseconds);
        ++shared_->brushFrames;
    }

    void FinishStroke() {
        if (recorder_ == nullptr) {
            return;
        }
        TerrainEdit edit = recorder_->Finish(UndoLabel());
        recorder_.reset();
        firstPoint_.reset();
        lastPoint_.reset();
        if (!edit.Empty()) {
            ++shared_->strokes;
            RecordTerrainEdit(*shared_, std::move(edit));
        }
    }

    std::unique_ptr<TerrainEditRecorder> recorder_{};
    std::optional<MATH::Vec2> firstPoint_{};
    std::optional<MATH::Vec2> lastPoint_{};
};

class SculptTool final : public StrokeTool {
public:
    using StrokeTool::StrokeTool;
    const char* GetToolId() const noexcept override { return kSculptToolId; }
    const char* GetDisplayName() const noexcept override { return "Terrain Sculpt"; }

protected:
    std::string_view Hint() const override {
        return "Terrain Sculpt: drag to apply, Shift inverts, Ctrl + wheel sets the radius";
    }
    EditorWorkspaceColor Color() const override { return kBrushColor; }
    float& Radius() override { return shared_->brush.radiusMeters; }
    const char* UndoLabel() const override { return "Sculpt Terrain"; }
    bool AppliesWhileDragging() const override { return shared_->brush.op != HeightBrushOp::Ramp; }

    void ApplyPath(TerrainStore& store, std::span<const MATH::Vec2> path, const EditorViewportInput& input,
                   TerrainEditRecorder& recorder) override {
        HeightBrushSettings brush = shared_->brush;
        if (AppliesWhileDragging()) {
            brush.strength *= std::clamp(input.pressure, 0.0f, 1.0f);
        }
        if (input.shift && brush.op == HeightBrushOp::Raise) {
            brush.op = HeightBrushOp::Lower;
        } else if (input.shift && brush.op == HeightBrushOp::Lower) {
            brush.op = HeightBrushOp::Raise;
        }
        BrushEditResult edit{};
        std::string error{};
        if (ApplyHeightBrush(store.Data(), TargetSculptLayer(*shared_), brush, path, &recorder, edit, error)) {
            store.RebakeRect(edit.touched);
        }
    }
};

class MaskTool final : public StrokeTool {
public:
    using StrokeTool::StrokeTool;
    const char* GetToolId() const noexcept override { return kMaskToolId; }
    const char* GetDisplayName() const noexcept override { return "Terrain Layer Mask"; }

protected:
    std::string_view Hint() const override {
        return "Layer Mask: drag to hide the selected layer, Shift brings it back, Ctrl + wheel sets the radius";
    }
    EditorWorkspaceColor Color() const override { return kMaskColor; }
    float& Radius() override { return shared_->maskBrush.radiusMeters; }
    const char* UndoLabel() const override { return "Paint Terrain Mask"; }

    void ApplyPath(TerrainStore& store, std::span<const MATH::Vec2> path, const EditorViewportInput& input,
                   TerrainEditRecorder& recorder) override {
        MaskBrushSettings brush = shared_->maskBrush;
        brush.strength *= std::clamp(input.pressure, 0.0f, 1.0f);
        if (input.shift) {
            brush.op = brush.op == MaskBrushOp::Erase ? MaskBrushOp::Paint : MaskBrushOp::Erase;
        }
        const HeightLayer* selected = store.Data().FindLayer(shared_->selectedLayer);
        const bool wasMasked = selected != nullptr && selected->masked;
        BrushEditResult edit{};
        std::string error{};
        if (selected != nullptr && ApplyMaskBrush(store.Data(), selected->id, brush, path, &recorder, edit, error)) {
            if (wasMasked) {
                store.RebakeRect(edit.touched);
            } else {
                store.RebakeAfterStackChange();
            }
        }
    }
};

class PaintTool final : public StrokeTool {
public:
    using StrokeTool::StrokeTool;
    const char* GetToolId() const noexcept override { return kPaintToolId; }
    const char* GetDisplayName() const noexcept override { return "Terrain Paint"; }

protected:
    std::string_view Hint() const override {
        return "Terrain Paint: drag to paint the selected surface, Shift hands it back to the rules, Ctrl + wheel sets "
               "the radius";
    }
    EditorWorkspaceColor Color() const override { return kPaintColor; }
    float& Radius() override { return shared_->paintBrush.radiusMeters; }
    const char* UndoLabel() const override { return "Paint Terrain"; }

    void ApplyPath(TerrainStore& store, std::span<const MATH::Vec2> path, const EditorViewportInput& input,
                   TerrainEditRecorder& recorder) override {
        ControlBrushSettings brush = shared_->paintBrush;
        brush.strength *= std::clamp(input.pressure, 0.0f, 1.0f);
        if (input.shift) {
            brush.op = ControlBrushOp::Unlock;
        }
        BrushEditResult edit{};
        std::string error{};
        if (ApplyControlBrush(store.Data(), brush, path, &recorder, edit, error)) {
            store.MarkControlChanged(edit.touched);
        }
    }
};

class StampTool final : public IEditorViewportTool {
public:
    explicit StampTool(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {}
    const char* GetToolId() const noexcept override { return kStampToolId; }
    const char* GetDisplayName() const noexcept override { return "Terrain Stamp"; }

    void Update(const EditorViewportInput& input, IEditorViewportCanvas& canvas) override {
        TerrainStore& store = TerrainStore::Get();
        canvas.DrawHint("Terrain Stamp: click to place, Ctrl + wheel sets the radius, Alt + wheel rotates",
                        kStampColor);
        if (!store.IsOpen()) {
            return;
        }
        StampSettings& stamp = shared_->stamp;
        AdjustBrushRadius(input, stamp.radiusMeters);
        if (input.alt && input.wheel != 0.0f) {
            stamp.rotationDegrees += input.wheel * 15.0f;
        }
        float hit[3]{};
        if (!RayToGround(input, store, hit)) {
            return;
        }
        DrawGroundCircle(canvas, store, hit, stamp.radiusMeters, kStampColor);
        const float angle = stamp.rotationDegrees * std::numbers::pi_v<float> / 180.0f;
        canvas.DrawLine({hit[0], hit[1] + 0.2f, hit[2]},
                        {hit[0] + std::cos(angle) * stamp.radiusMeters, hit[1] + 0.2f,
                         hit[2] + std::sin(angle) * stamp.radiusMeters},
                        kStampColor, 2.0f);
        if (input.primaryPressed) {
            StampSettings placed = stamp;
            placed.center = MATH::Vec2{hit[0], hit[2]};
            placed.heightMeters *= input.shift ? -1.0f : std::clamp(input.pressure, 0.0f, 1.0f);
            std::string error{};
            if (!RunStamp(*shared_, shared_->stampName, placed, error) && shared_->commands != nullptr) {
                shared_->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, error);
            }
        }
    }

private:
    std::shared_ptr<EditorShared> shared_;
};

} // namespace

std::unique_ptr<IEditorViewportTool> MakeSculptTool(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<SculptTool>(std::move(shared));
}

std::unique_ptr<IEditorViewportTool> MakeMaskTool(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<MaskTool>(std::move(shared));
}

std::unique_ptr<IEditorViewportTool> MakePaintTool(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<PaintTool>(std::move(shared));
}

std::unique_ptr<IEditorViewportTool> MakeStampTool(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<StampTool>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
