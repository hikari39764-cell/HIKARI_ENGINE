#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#include "HIKARI_TerrainKitEditorCommon.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

class TerrainPanel final : public IEditorPanel {
public:
    explicit TerrainPanel(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {}
    const char* GetPanelId() const noexcept override { return "terrainkit.panel"; }
    const char* GetDisplayName() const noexcept override { return "Terrain"; }

    void Draw(IEditorWorkspaceUi& ui) override {
        TerrainStore& store = TerrainStore::Get();
        if (!store.IsOpen()) {
            ui.Text("Add a Terrain component to an object.");
            return;
        }
        const auto& grid = store.Settings().grid;
        ui.Text(std::to_string(store.Tiles().size()) + " tiles, " +
                std::to_string(static_cast<int>(grid.tileSizeMeters)) + " m, " + std::to_string(grid.tileCells) +
                " cells");
        if (store.HasUnsavedTiles()) {
            ui.Text("Unsaved terrain changes; they are written with the scene.");
        }
        ui.Separator();
        DrawLayers(ui, store);
        ui.Separator();
        DrawSelectedLayer(ui, store);
        ui.Separator();
        DrawBrush(ui);
        ui.Separator();
        DrawSurfaces(ui, store);
        ui.Separator();
        DrawStamp(ui);
        ui.Separator();
        DrawErosion(ui, store);
        ui.Separator();
        ui.InputFloat("amplitude", "Noise Amplitude (m)", &noise_.amplitudeMeters);
        ui.InputFloat("feature", "Noise Feature Size (m)", &noise_.featureSizeMeters);
        if (ui.Button("generate", "Regenerate Terrain")) {
            std::string error{};
            if (!RegenerateTerrain(*shared_, noise_, error)) {
                Report(error);
            }
        }
    }

private:
    void Report(const std::string& error) {
        if (shared_->commands != nullptr && !error.empty()) {
            shared_->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, error);
        }
    }

    void DrawLayers(IEditorWorkspaceUi& ui, TerrainStore& store) {
        ui.Text("Layers (top first)");
        const auto layers = store.Data().Stack().layers; // 操作で並びが変わるので写しを読む
        std::string error{};
        for (size_t reversed = layers.size(); reversed > 0u; --reversed) {
            const size_t index = reversed - 1u;
            const HeightLayer& layer = layers[index];
            const std::string id = std::to_string(layer.id);
            bool enabled = layer.enabled;
            if (ui.Checkbox("enabled" + id, "##enabled" + id, &enabled)) {
                HeightLayer next = layer;
                next.enabled = enabled;
                UpdateTerrainLayer(*shared_, next, error);
            }
            ui.SameLine();
            const std::string label = layer.name + "  [" + TEXT::EnumToName(kHeightLayerKindNames, layer.kind) + "]";
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
            if (ui.Button("layerRemove", "Delete")) {
                if (RemoveTerrainLayer(*shared_, shared_->selectedLayer, error)) {
                    shared_->selectedLayer = 0u;
                }
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
            ui.Text("Select a layer to edit it.");
            return;
        }
        HeightLayer next = *selected;
        bool changed = false;
        for (const auto& entry : kHeightBlendModeNames) {
            if (ui.Selectable(std::string("blend") + entry.name, std::string("Blend: ") + entry.name,
                              next.blend == entry.value) &&
                next.blend != entry.value) {
                next.blend = entry.value;
                changed = true;
            }
        }
        changed |= ui.InputFloat("opacity", "Opacity", &next.opacity);
        changed |= ui.Checkbox("masked", "Use Mask", &next.masked);
        if (ui.Button("maskTool", "Paint Mask Tool") && shared_->services != nullptr) {
            shared_->services->ActivateViewportTool(kMaskToolId);
        }
        ui.InputFloat("maskRadius", "Mask Radius (m)", &shared_->maskBrush.radiusMeters);
        ui.InputFloat("maskStrength", "Mask Strength", &shared_->maskBrush.strength);
        if (next.kind == HeightLayerKind::Noise) {
            changed |= ui.InputFloat("noiseAmplitude", "Amplitude (m)", &next.noise.amplitudeMeters);
            changed |= ui.InputFloat("noiseOffset", "Offset (m)", &next.noise.offsetMeters);
            changed |= ui.InputFloat("noiseFeature", "Feature Size (m)", &next.noise.featureSizeMeters);
            int32_t octaves = static_cast<int32_t>(next.noise.octaves);
            if (ui.InputInt("noiseOctaves", "Octaves", &octaves)) {
                next.noise.octaves = static_cast<uint32_t>(std::clamp(octaves, 1, 16));
                changed = true;
            }
            changed |= ui.InputInt("noiseSeed", "Seed", &next.noise.seed);
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

    void DrawBrush(IEditorWorkspaceUi& ui) {
        if (ui.Button("sculpt", "Sculpt Tool") && shared_->services != nullptr) {
            shared_->services->ActivateViewportTool(kSculptToolId);
        }
        HeightBrushSettings& brush = shared_->brush;
        for (const auto& entry : kHeightBrushOpNames) {
            if (ui.Selectable(entry.name, entry.name, brush.op == entry.value)) {
                brush.op = entry.value;
            }
        }
        ui.InputFloat("radius", "Radius (m)", &brush.radiusMeters);
        ui.InputFloat("strength", "Strength", &brush.strength);
        ui.InputFloat("hardness", "Hardness", &brush.hardness);
        switch (brush.op) {
        case HeightBrushOp::Flatten: ui.InputFloat("target", "Target Height (m)", &brush.targetHeightMeters); break;
        case HeightBrushOp::Noise: ui.InputFloat("noiseScale", "Noise Scale (m)", &brush.noiseScaleMeters); break;
        case HeightBrushOp::Terrace: ui.InputFloat("terraceStep", "Step (m)", &brush.terraceStepMeters); break;
        case HeightBrushOp::Ramp:
            ui.InputFloat("rampSlope", "Slope (deg, 0 joins the end)", &brush.rampSlopeDegrees);
            break;
        default: break;
        }
        ui.InputFloat("minSlope", "Only Above Slope (deg)", &brush.limitMinSlopeDegrees);
        ui.InputFloat("maxSlope", "Only Below Slope (deg)", &brush.limitMaxSlopeDegrees);
        ui.InputFloat("minHeight", "Only Above Height (m)", &brush.limitMinHeightMeters);
        ui.InputFloat("maxHeight", "Only Below Height (m)", &brush.limitMaxHeightMeters);
    }

    void DrawSurfaces(IEditorWorkspaceUi& ui, TerrainStore& store) {
        if (ui.Button("paintTool", "Paint Tool") && shared_->services != nullptr) {
            shared_->services->ActivateViewportTool(kPaintToolId);
        }
        TerrainSurfaceLibrary& library = store.Surfaces();
        if (library.Busy()) {
            ui.Text("Preparing surfaces: " + std::to_string(static_cast<int>(library.Progress() * 100.0f)) + "%");
        }
        if (!library.LastError().empty()) {
            ui.Text(library.LastError());
        }
        ControlBrushSettings& brush = shared_->paintBrush;
        const auto& slots = library.Slots();
        for (uint32_t slot = 0u; slot < slots.size(); ++slot) {
            if (ui.Selectable("surface" + std::to_string(slot), std::to_string(slot) + ": " + slots[slot],
                              brush.material == slot)) {
                brush.material = slot;
            }
        }
        ui.InputFloat("paintRadius", "Paint Radius (m)", &brush.radiusMeters);
        ui.InputFloat("paintStrength", "Paint Strength", &brush.strength);
        ui.InputFloat("paintHardness", "Paint Hardness", &brush.hardness);
    }

    void DrawStamp(IEditorWorkspaceUi& ui) {
        if (ui.Button("stampTool", "Stamp Tool") && shared_->services != nullptr) {
            shared_->services->ActivateViewportTool(kStampToolId);
        }
        for (const std::string& name : ListStampNames()) {
            if (ui.Selectable("stamp" + name, name, shared_->stampName == name)) {
                shared_->stampName = name;
            }
        }
        StampSettings& stamp = shared_->stamp;
        ui.InputFloat("stampRadius", "Stamp Radius (m)", &stamp.radiusMeters);
        ui.InputFloat("stampHeight", "Stamp Height (m)", &stamp.heightMeters);
        ui.InputFloat("stampRotation", "Stamp Rotation (deg)", &stamp.rotationDegrees);
        for (const auto& entry : kStampBlendNames) {
            if (ui.Selectable(std::string("stampBlend") + entry.name, std::string("Stamp: ") + entry.name,
                              stamp.blend == entry.value)) {
                stamp.blend = entry.value;
            }
        }
    }

    void DrawErosion(IEditorWorkspaceUi& ui, TerrainStore& store) {
        ErosionRequest& erosion = shared_->erosion;
        ui.Checkbox("hydraulic", "Hydraulic", &erosion.hydraulic);
        ui.SameLine();
        ui.Checkbox("thermal", "Thermal", &erosion.thermal);
        int32_t iterations = static_cast<int32_t>(erosion.hydraulicSettings.iterations);
        if (ui.InputInt("erosionIterations", "Water Iterations", &iterations)) {
            erosion.hydraulicSettings.iterations = static_cast<uint32_t>(std::clamp(iterations, 1, 20000));
        }
        ui.InputFloat("erosionRain", "Rain (m/s)", &erosion.hydraulicSettings.rainRate);
        ui.InputFloat("erosionTalus", "Talus Angle (deg)", &erosion.thermalSettings.talusAngleDegrees);
        if (store.Erosion().Running()) {
            ui.Text("Eroding: " + std::to_string(static_cast<int>(store.Erosion().Progress() * 100.0f)) + "%");
        } else if (ui.Button("erode", "Erode Whole Terrain")) {
            std::string error{};
            if (!StartGpuErosion(shared_, WholeTerrainRect(store), erosion, error)) {
                Report(error);
            }
        }
    }

    std::shared_ptr<EditorShared> shared_;
    NoiseLayerSettings noise_ = DefaultNoise();
};

} // namespace

std::unique_ptr<IEditorPanel> MakeTerrainPanel(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<TerrainPanel>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
