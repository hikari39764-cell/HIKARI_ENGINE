#include <algorithm>
#include <array>
#include <cstring>
#include <string>

#include "Core/Text/HIKARI_HumanizeKey.h"
#include "HIKARI_TerrainKitFieldUi.h"
#include "HIKARI_TerrainKitWorkspacePages.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

// 筆と印章で地形を彫る頁。層の並びもここで扱う。
class ShapePage final : public TerrainPage {
public:
    using TerrainPage::TerrainPage;

    const char* Name() const override { return "Shape"; }
    const char* Help() const override { return "Sculpt with brushes and stamps, and manage the height layers."; }
    const char* ToolId() const override { return stampActive_ ? kStampToolId : kSculptToolId; }

    void DrawList(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        if (ui.Section("shapeBrushes", "Brushes")) {
            HeightBrushSettings& brush = shared_->brush;
            for (const auto& entry : kHeightBrushOpNames) {
                if (ui.Selectable(entry.name, TEXT::HumanizeKey(entry.name),
                                  !stampActive_ && brush.op == entry.value)) {
                    brush.op = entry.value;
                    stampActive_ = false;
                    ActivateTool(kSculptToolId);
                }
                ui.Tooltip(BrushHelp(entry.value));
            }
        }
        if (ui.Section("shapeStamps", "Stamps", false)) {
            for (const std::string& name : ListStampNames()) {
                if (ui.Selectable("stamp" + name, TEXT::HumanizeKey(name),
                                  stampActive_ && shared_->stampName == name)) {
                    shared_->stampName = name;
                    stampActive_ = true;
                    ActivateTool(kStampToolId);
                }
            }
            ui.Tooltip("Add your own: put a height image in Assets/TerrainStamps.");
        }
        if (ui.Section("shapeLayers", "Layers (top first)")) {
            DrawLayers(ui, store);
        }
    }

    void DrawProperties(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        if (stampActive_) {
            if (ui.Section("shapeStamp", "Stamp")) {
                DrawStamp(ui);
            }
        } else {
            if (ui.Section("shapeBrush", "Brush")) {
                DrawBrush(ui);
            }
            if (ui.Section("shapeBrushShape", "Brush Shape", false)) {
                DrawBrushShapes(ui, store, *shared_);
            }
            if (ui.Section("shapeLimits", "Only Where", false)) {
                DrawLimits(ui);
            }
        }
        if (ui.Section("shapeLayer", "Selected Layer")) {
            DrawSelectedLayer(ui, store);
        }
    }

private:
    static const char* BrushHelp(HeightBrushOp op) {
        switch (op) {
        case HeightBrushOp::Raise: return "Lifts the ground. Hold Shift to lower it.";
        case HeightBrushOp::Lower: return "Pushes the ground down. Hold Shift to raise it.";
        case HeightBrushOp::Smooth: return "Evens out bumps.";
        case HeightBrushOp::Flatten: return "Levels the ground to a set height.";
        case HeightBrushOp::Noise: return "Adds natural unevenness.";
        case HeightBrushOp::Ramp: return "Drag from one point to another to make a slope between them.";
        case HeightBrushOp::Terrace: return "Cuts the ground into steps.";
        }
        return "";
    }

    void DrawBrush(IEditorWorkspaceUi& ui) {
        HeightBrushSettings& brush = shared_->brush;
        ui.SliderFloat("radius", "Size (m)", &brush.radiusMeters, 1.0f, 2048.0f);
        ui.SliderFloat("strength", "Strength", &brush.strength, 0.01f, 1.0f);
        ui.SliderFloat("hardness", "Hardness", &brush.hardness, 0.0f, 1.0f);
        ui.Tooltip("The inner ring in the view: full strength inside it, fading out to the outer ring.");
        switch (brush.op) {
        case HeightBrushOp::Flatten: ui.InputFloat("target", "Target Height (m)", &brush.targetHeightMeters); break;
        case HeightBrushOp::Noise:
            ui.SliderFloat("noiseScale", "Noise Scale (m)", &brush.noiseScaleMeters, 0.5f, 256.0f);
            break;
        case HeightBrushOp::Terrace:
            ui.SliderFloat("terraceStep", "Step (m)", &brush.terraceStepMeters, 0.25f, 128.0f);
            break;
        case HeightBrushOp::Ramp:
            ui.SliderFloat("rampSlope", "Slope (deg)", &brush.rampSlopeDegrees, 0.0f, 60.0f);
            ui.Tooltip("0 joins the two ends directly.");
            break;
        default: break;
        }
    }

    void DrawLimits(IEditorWorkspaceUi& ui) {
        HeightBrushSettings& brush = shared_->brush;
        ui.SliderFloat("minSlope", "Above Slope (deg)", &brush.limitMinSlopeDegrees, 0.0f, 90.0f);
        ui.SliderFloat("maxSlope", "Below Slope (deg)", &brush.limitMaxSlopeDegrees, 0.0f, 90.0f);
        ui.InputFloat("minHeight", "Above Height (m)", &brush.limitMinHeightMeters);
        ui.InputFloat("maxHeight", "Below Height (m)", &brush.limitMaxHeightMeters);
    }

    void DrawStamp(IEditorWorkspaceUi& ui) {
        StampSettings& stamp = shared_->stamp;
        ui.Text(TEXT::HumanizeKey(shared_->stampName));
        ui.SliderFloat("stampRadius", "Size (m)", &stamp.radiusMeters, 1.0f, 2048.0f);
        ui.InputFloat("stampHeight", "Height (m)", &stamp.heightMeters);
        ui.InputFloat("stampRotation", "Rotation (deg)", &stamp.rotationDegrees);
        ui.Text("Combine");
        for (const auto& entry : kStampBlendNames) {
            ui.SameLine();
            if (ui.Choice(std::string("stampBlend") + entry.name, TEXT::HumanizeKey(entry.name),
                          stamp.blend == entry.value)) {
                stamp.blend = entry.value;
            }
        }
    }

    void DrawLayers(IEditorWorkspaceUi& ui, TerrainStore& store) {
        const auto layers = store.Data().Stack().layers; // 操作で並びが変わるので写しを読む
        std::string error{};
        for (size_t reversed = layers.size(); reversed > 0u; --reversed) {
            const HeightLayer& layer = layers[reversed - 1u];
            const std::string id = std::to_string(layer.id);
            bool enabled = layer.enabled;
            if (ui.Checkbox("enabled" + id, "##enabled" + id, &enabled)) {
                HeightLayer next = layer;
                next.enabled = enabled;
                UpdateTerrainLayer(*shared_, next, error);
            }
            ui.SameLine();
            const std::string label =
                layer.name + "  [" + TEXT::HumanizeKey(TEXT::EnumToName(kHeightLayerKindNames, layer.kind)) + "]";
            if (ui.Selectable("layer" + id, label, shared_->selectedLayer == layer.id)) {
                shared_->selectedLayer = layer.id;
            }
        }
        if (ui.Button("addSculpt", "Add Sculpt")) {
            AddKind(HeightLayerKind::Sculpt, "Sculpt", error);
        }
        ui.SameLine();
        if (ui.Button("addNoise", "Add Noise")) {
            AddKind(HeightLayerKind::Noise, "Noise", error);
        }
        ui.SameLine();
        if (ui.Button("addImage", "Add Height Image")) {
            AddKind(HeightLayerKind::Image, "Height Image", error);
        }
        const size_t position = store.Data().LayerIndex(shared_->selectedLayer);
        if (position != TerrainData::kAppend) {
            if (ui.Button("layerUp", "Move Up")) {
                MoveTerrainLayer(*shared_, shared_->selectedLayer, position + 1u, error);
            }
            ui.SameLine();
            if (ui.Button("layerDown", "Move Down") && position > 0u) {
                MoveTerrainLayer(*shared_, shared_->selectedLayer, position - 1u, error);
            }
            ui.SameLine();
            if (ui.Button("layerRemove", "Delete") && RemoveTerrainLayer(*shared_, shared_->selectedLayer, error)) {
                shared_->selectedLayer = 0u;
            }
        }
        Report(error);
    }

    void AddKind(HeightLayerKind kind, const char* name, std::string& error) {
        HeightLayer layer{};
        layer.kind = kind;
        layer.name = name;
        layer.blend = kind == HeightLayerKind::Image ? HeightBlendMode::Replace : HeightBlendMode::Add;
        layer.noise = DefaultNoise();
        layer.noise.amplitudeMeters = 20.0f;
        layer.noise.offsetMeters = 0.0f;
        layer.noise.featureSizeMeters = 120.0f;
        uint32_t id = 0u;
        if (AddTerrainLayer(*shared_, std::move(layer), TerrainData::kAppend, id, error)) {
            shared_->selectedLayer = id;
        }
    }

    void DrawSelectedLayer(IEditorWorkspaceUi& ui, TerrainStore& store) {
        const HeightLayer* selected = store.Data().FindLayer(shared_->selectedLayer);
        if (selected == nullptr) {
            ui.Text("Select a layer on the left to edit it.");
            return;
        }
        if (selected->kind == HeightLayerKind::Spline) {
            ui.Text("Roads and rivers are edited on the Roads and Water page.");
            return;
        }
        HeightLayer next = *selected;
        bool changed = false;
        ui.Text("Combine");
        for (const auto& entry : kHeightBlendModeNames) {
            ui.SameLine();
            if (ui.Choice(std::string("blend") + entry.name, TEXT::HumanizeKey(entry.name),
                          next.blend == entry.value) &&
                next.blend != entry.value) {
                next.blend = entry.value;
                changed = true;
            }
        }
        changed |= ui.SliderFloat("opacity", "Opacity", &next.opacity, 0.0f, 1.0f);
        changed |= ui.Checkbox("masked", "Use Mask", &next.masked);
        if (ui.Button("maskTool", "Paint Mask")) {
            ActivateTool(kMaskToolId);
        }
        ui.Tooltip("Paint in the view where this layer applies. Pick a brush on the left to go back to sculpting.");
        ui.SliderFloat("maskRadius", "Mask Brush Size (m)", &shared_->maskBrush.radiusMeters, 1.0f, 2048.0f);
        ui.SliderFloat("maskStrength", "Mask Brush Strength", &shared_->maskBrush.strength, 0.01f, 1.0f);
        if (next.kind == HeightLayerKind::Noise) {
            changed |= ui.InputFloat("noiseAmplitude", "Height (m)", &next.noise.amplitudeMeters);
            changed |= ui.InputFloat("noiseOffset", "Raise All (m)", &next.noise.offsetMeters);
            changed |= ui.SliderFloat("noiseFeature", "Feature Size (m)", &next.noise.featureSizeMeters, 4.0f, 8192.0f);
            int32_t octaves = static_cast<int32_t>(next.noise.octaves);
            if (ui.InputInt("noiseOctaves", "Detail", &octaves)) {
                next.noise.octaves = static_cast<uint32_t>(std::clamp(octaves, 1, 16));
                changed = true;
            }
            changed |= ui.InputInt("noiseSeed", "Variation", &next.noise.seed);
            changed |= ui.InputFloat("noiseMassif", "Mountain Radius (m)", &next.noise.massifRadiusMeters);
        } else if (next.kind == HeightLayerKind::Image) {
            std::array<char, 512> source{};
            std::strncpy(source.data(), next.image.source.c_str(), source.size() - 1u);
            if (ui.InputText("imageSource", "Project Path (.exr .r16 .raw)", source.data(), source.size())) {
                next.image.source = source.data();
                changed = true;
            }
            EditorWorkspacePoint origin{next.image.originMeters.x, next.image.originMeters.y};
            if (ui.InputFloat2("imageOrigin", "Origin (m)", &origin)) {
                next.image.originMeters = MATH::Vec2{origin.x, origin.y};
                changed = true;
            }
            EditorWorkspacePoint size{next.image.sizeMeters.x, next.image.sizeMeters.y};
            if (ui.InputFloat2("imageSize", "Size (m)", &size)) {
                next.image.sizeMeters = MATH::Vec2{size.x, size.y};
                changed = true;
            }
            changed |= ui.InputFloat("imageScale", "Height Scale (m)", &next.image.heightScaleMeters);
            changed |= ui.InputFloat("imageOffset", "Height Offset (m)", &next.image.heightOffsetMeters);
        }
        if (changed) {
            std::string error{};
            if (!UpdateTerrainLayer(*shared_, next, error)) {
                Report(error);
            }
        }
    }

    bool stampActive_ = false;
};

} // namespace

std::unique_ptr<TerrainPage> MakeShapePage(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<ShapePage>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
