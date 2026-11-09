#include <algorithm>
#include <array>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

#include "HIKARI_TerrainKitEditorCommon.h"
#include "HIKARI_TerrainKitFieldUi.h"

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
        DrawRules(ui, store);
        ui.Separator();
        DrawInstances(ui, store);
        ui.Separator();
        DrawScatter(ui, store);
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
        for (const auto& entry : kSplineKindNames) {
            ui.SameLine();
            if (ui.Button(std::string("add") + entry.name, std::string("Add ") + entry.name)) {
                uint32_t id = 0u;
                if (AddTerrainLayer(*shared_, MakeSplineLayer(entry.value), TerrainData::kAppend, id, error)) {
                    shared_->selectedLayer = id;
                    if (shared_->services != nullptr) {
                        shared_->services->ActivateViewportTool(kSplineToolId);
                    }
                }
            }
        }
        if (ui.Button("lakeFill", "Fill Basin With Water") && shared_->services != nullptr) {
            shared_->lakeFillArmed = true;
            shared_->services->ActivateViewportTool(kSplineToolId);
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
        } else if (next.kind == HeightLayerKind::Spline) {
            changed |= DrawSpline(ui, next.spline);
        }
        if (changed) {
            std::string error{};
            const bool updated = next.kind == HeightLayerKind::Spline ? ChangeSplineLayer(*shared_, next, error)
                                                                      : UpdateTerrainLayer(*shared_, next, error);
            if (!updated) {
                Report(error);
            }
        }
    }

    bool DrawSpline(IEditorWorkspaceUi& ui, SplineLayerSettings& spline) {
        if (ui.Button("splineTool", "Road and River Tool") && shared_->services != nullptr) {
            shared_->services->ActivateViewportTool(kSplineToolId);
        }
        ui.Text(std::to_string(spline.points.size()) + " points");
        bool changed = false;
        float width = spline.points.empty() ? DefaultSplineWidth(spline.kind) : spline.points.front().widthMeters;
        if (ui.InputFloat("splineWidth", "Width (m)", &width)) {
            for (SplinePoint& point : spline.points) {
                point.widthMeters = width;
            }
            changed = true;
        }
        changed |= ui.InputFloat("splineFalloff", "Falloff (m)", &spline.falloffMeters);
        changed |= ui.InputFloat("splineEdge", "Edge Width (m)", &spline.edgeMeters);
        changed |= ui.Checkbox("splinePaint", "Paint Surface", &spline.paint);
        int32_t surface = static_cast<int32_t>(spline.surface);
        if (ui.InputInt("splineSurface", "Surface Slot", &surface)) {
            spline.surface = static_cast<uint32_t>(std::clamp(surface, 0, 31));
            changed = true;
        }
        int32_t edge = static_cast<int32_t>(spline.edgeSurface);
        if (ui.InputInt("splineEdgeSurface", "Edge Surface Slot", &edge)) {
            spline.edgeSurface = static_cast<uint32_t>(std::clamp(edge, 0, 31));
            changed = true;
        }
        if (spline.kind == SplineKind::River || spline.kind == SplineKind::Lake) {
            changed |= ui.InputFloat("riverDepth", "Depth (m)", &spline.depthMeters);
            changed |= ui.InputFloat("riverWet", "Wet Bank (m)", &spline.wetMeters);
            changed |= ui.InputFloat("riverSpeed", "Flow Speed (m/s)", &spline.flowSpeed);
            changed |= ui.InputFloat("riverClarity", "Clarity (m)", &spline.clarityMeters);
        }
        if (spline.kind == SplineKind::Lake) {
            if (!spline.points.empty()) {
                changed |= ui.InputFloat("lakeLevel", "Water Level (m)", &spline.points.front().position.y);
            }
            changed |= ui.InputFloat("lakeShelf", "Shore Shelf (m)", &spline.shelfMeters);
            changed |= ui.InputFloat("lakeNoise", "Shore Noise (m)", &spline.shoreNoiseMeters);
        }
        return changed;
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

    void DrawRules(IEditorWorkspaceUi& ui, TerrainStore& store) {
        const size_t pending = store.RuleBaker().PendingTiles();
        ui.Text(pending != 0u ? "Surface rules (updating " + std::to_string(pending) + " tiles)" : "Surface rules");
        SurfaceRuleSet rules = store.SurfaceRules();
        bool changed = false;
        for (size_t index = 0u; index < rules.rules.size(); ++index) {
            SurfaceRule& rule = rules.rules[index];
            const std::string id = std::to_string(index);
            changed |= ui.Checkbox("ruleEnabled" + id, rule.name.empty() ? "Rule " + id : rule.name, &rule.enabled);
            int32_t slot = static_cast<int32_t>(rule.surface);
            if (ui.InputInt("ruleSurface" + id, "Surface Slot##" + id, &slot)) {
                rule.surface = static_cast<uint32_t>(std::clamp(slot, 0, 31));
                changed = true;
            }
            changed |= ui.InputFloat("ruleMinSlope" + id, "Min Slope (deg)##" + id, &rule.minSlopeDegrees);
            changed |= ui.InputFloat("ruleMaxSlope" + id, "Max Slope (deg)##" + id, &rule.maxSlopeDegrees);
            changed |= ui.InputFloat("ruleMinHeight" + id, "Min Height (m)##" + id, &rule.minHeightMeters);
            changed |= ui.InputFloat("ruleMaxHeight" + id, "Max Height (m)##" + id, &rule.maxHeightMeters);
        }
        if (ui.Button("ruleAdd", "Add Rule")) {
            rules.rules.push_back({});
            changed = true;
        }
        ui.SameLine();
        if (ui.Button("ruleRemove", "Remove Last") && !rules.rules.empty()) {
            rules.rules.pop_back();
            changed = true;
        }
        if (changed) {
            ChangeSurfaceRules(*shared_, std::move(rules));
        }
        TerrainWeather weather = store.Weather();
        bool weatherChanged = ui.InputFloat("snowLine", "Snow Line (m)", &weather.snowLineMeters);
        weatherChanged |= ui.InputFloat("snowAmount", "Snow Amount", &weather.snowAmount);
        weatherChanged |= ui.InputFloat("wetness", "Wetness", &weather.wetness);
        if (weatherChanged) {
            ChangeWeather(*shared_, weather);
        }
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

    void DrawInstances(IEditorWorkspaceUi& ui, TerrainStore& store) {
        ui.Text("Instance Sets");
        const WorldView& world = store.World();
        std::array<ComponentTypeId, 64> ids{};
        const uint32_t setCount = (std::min)(world.GetInstanceSetIds(ids), static_cast<uint32_t>(ids.size()));
        for (uint32_t index = 0u; index < setCount; ++index) {
            const ComponentTypeId id = ids[index];
            const GameProjectWorldInstanceSetDesc* set = world.FindInstanceSet(id);
            if (set == nullptr) {
                continue;
            }
            const std::string name = set->name;
            if (ui.Selectable("instanceSet" + name, name + "  (" + std::to_string(world.GetInstanceCount(id)) + ")",
                              shared_->selectedInstanceSet == id.value)) {
                shared_->selectedInstanceSet = id.value;
                LoadInstanceFields(*set);
            }
        }
        ui.InputText("instanceName", "Set Name", instanceName_.data(), instanceName_.size());
        ui.InputText("instanceModel", "Model Asset GUID (empty = box)", instanceModel_.data(), instanceModel_.size());
        ui.InputFloat("instanceTint", "Color Variation", &instanceTint_);
        ui.InputFloat("instanceWind", "Wind", &instanceWind_);
        ui.InputFloat("instanceMinScreen", "Hide Below Screen Height", &instanceMinScreen_);
        ui.InputText("instanceFarModel", "Far Model Asset GUID", instanceFarModel_.data(), instanceFarModel_.size());
        ui.InputFloat("instanceFarDistance", "Far Model Distance (m, at scale 1)", &instanceFarDistance_);
        if (ui.Button("instanceBakeFar", "Bake Far Model From Model") && shared_->services != nullptr) {
            if (!shared_->services->BakeFarModel(instanceModel_.data(), instanceFarModel_.data(),
                                                 instanceFarModel_.size())) {
                Report("The far model bake failed: it needs a model GUID, see the log.");
            }
        }
        ui.Checkbox("instanceRay", "Ray Traced", &instanceRayTraced_);
        if (ui.Button("instanceAdd", "Add or Update Set")) {
            GameProjectWorldInstanceSetDesc desc{};
            std::memcpy(desc.name, instanceName_.data(), (std::min)(instanceName_.size(), sizeof(desc.name) - 1u));
            std::memcpy(desc.modelAssetGuid, instanceModel_.data(),
                        (std::min)(instanceModel_.size(), sizeof(desc.modelAssetGuid) - 1u));
            desc.geometry = desc.modelAssetGuid[0] != '\0' ? GameProjectWorldMassRenderGeometry::ModelAsset
                                                           : GameProjectWorldMassRenderGeometry::BuiltInBox;
            desc.flags = static_cast<uint32_t>(instanceRayTraced_ ? GameProjectWorldInstanceSetFlags::ReceiveShadow |
                                                                        GameProjectWorldInstanceSetFlags::RayTraced
                                                                  : GameProjectWorldInstanceSetFlags::ReceiveShadow);
            desc.tintVariation = instanceTint_;
            desc.windStrength = instanceWind_;
            desc.minScreenHeight = instanceMinScreen_;
            std::memcpy(desc.farModelAssetGuid, instanceFarModel_.data(),
                        (std::min)(instanceFarModel_.size(), sizeof(desc.farModelAssetGuid) - 1u));
            desc.farDistance = instanceFarDistance_;
            if (world.AddInstanceSet(desc)) {
                shared_->selectedInstanceSet = MakeComponentTypeId(desc.name).value;
            } else {
                Report("The instance set is not valid: it needs a name, and a model GUID or none for a box.");
            }
        }
        InstanceBrushSettings& brush = shared_->instanceBrush;
        ui.InputFloat("instanceRadius", "Brush Radius (m)", &brush.radiusMeters);
        ui.InputFloat("instanceDensity", "Density (per 100 m2)", &brush.density);
        ui.InputFloat("instanceSpacing", "Min Spacing (m)", &brush.spacingMeters);
        ui.InputFloat("instanceScaleMin", "Scale Min", &brush.scaleMin);
        ui.InputFloat("instanceScaleMax", "Scale Max", &brush.scaleMax);
        ui.InputFloat("instanceAlign", "Align To Ground", &brush.alignToGround);
        ui.InputFloat("instanceSlope", "Max Slope (deg)", &brush.maxSlopeDegrees);
        if (ui.Button("instanceTool", "Place Instances Tool") && shared_->services != nullptr) {
            brush.mode = InstanceBrushMode::Paint;
            shared_->services->ActivateViewportTool(kInstanceToolId);
        }
        ui.SameLine();
        if (ui.Button("instanceMoveTool", "Move Instances Tool") && shared_->services != nullptr) {
            brush.mode = InstanceBrushMode::Move;
            shared_->services->ActivateViewportTool(kInstanceToolId);
        }
    }

    // 選んだ実例集を入力欄へ読み込む。同じ名前のまま Add or Update Set すると個体を残して設定だけ替わる。
    void LoadInstanceFields(const GameProjectWorldInstanceSetDesc& set) {
        const auto copy = [](auto& field, const char* value) {
            field.fill('\0');
            std::strncpy(field.data(), value, field.size() - 1u);
        };
        copy(instanceName_, set.name);
        copy(instanceModel_, set.modelAssetGuid);
        copy(instanceFarModel_, set.farModelAssetGuid);
        instanceTint_ = set.tintVariation;
        instanceWind_ = set.windStrength;
        instanceMinScreen_ = set.minScreenHeight;
        if (set.farDistance > 0.0f) {
            instanceFarDistance_ = set.farDistance;
        }
        instanceRayTraced_ = (set.flags & static_cast<uint32_t>(GameProjectWorldInstanceSetFlags::RayTraced)) != 0u;
    }

    // 実例集を名前で選ぶ。
    void DrawSetChoice(IEditorWorkspaceUi& ui, const WorldView& world, const std::string& prefix, std::string& name,
                       bool& changed) {
        ui.Text("Instance Set");
        std::array<ComponentTypeId, 64> ids{};
        const uint32_t count = (std::min)(world.GetInstanceSetIds(ids), static_cast<uint32_t>(ids.size()));
        for (uint32_t index = 0u; index < count; ++index) {
            const GameProjectWorldInstanceSetDesc* set = world.FindInstanceSet(ids[index]);
            if (set != nullptr &&
                ui.Selectable(prefix + "set" + set->name, std::string(set->name) + "##" + prefix, name == set->name)) {
                name = set->name;
                changed = true;
            }
        }
        if (!name.empty() && world.FindInstanceSet(MakeComponentTypeId(name.c_str())) == nullptr) {
            ui.Text("\"" + name + "\" does not exist yet; add it above. Nothing is placed until then.");
        }
    }

    // 置いてよい地表材質の枠を印で選ぶ。
    void DrawSurfaceSlots(IEditorWorkspaceUi& ui, const TerrainStore& store, uint32_t& mask, bool& changed) {
        ui.Text("Allowed Surfaces");
        if (!ui.BeginTable("scatterSlots", 3u)) {
            return;
        }
        const auto& slots = store.Surfaces().Slots();
        for (uint32_t slot = 0u; slot < slots.size() && slot < 32u; ++slot) {
            ui.TableNextColumn();
            bool allowed = (mask & (1u << slot)) != 0u;
            if (ui.Checkbox("scatterSlot" + std::to_string(slot), slots[slot], &allowed)) {
                mask = allowed ? mask | (1u << slot) : mask & ~(1u << slot);
                changed = true;
            }
        }
        ui.EndTable();
    }

    // 密度に掛けるマスクの層を選ぶ。マスクは層のマスクの筆で塗る。
    void DrawMaskChoice(IEditorWorkspaceUi& ui, const TerrainStore& store, ScatterRule& rule, bool& changed) {
        ui.Text("Density Mask");
        if (ui.Selectable("scatterMaskNone", "None", rule.maskLayer == 0u) && rule.maskLayer != 0u) {
            rule.maskLayer = 0u;
            changed = true;
        }
        for (const HeightLayer& layer : store.Data().Stack().layers) {
            const std::string id = std::to_string(layer.id);
            if (ui.Selectable("scatterMask" + id, layer.name + "##mask" + id, rule.maskLayer == layer.id)) {
                rule.maskLayer = layer.id;
                changed = true;
            }
        }
        if (ui.Button("scatterMaskNew", "New Mask Layer")) {
            HeightLayer layer{};
            layer.kind = HeightLayerKind::Sculpt;
            layer.name = "Mask: " + rule.name;
            uint32_t id = 0u;
            std::string error{};
            if (AddTerrainLayer(*shared_, std::move(layer), TerrainData::kAppend, id, error)) {
                rule.maskLayer = id;
                changed = true;
                shared_->selectedLayer = id;
                if (shared_->services != nullptr) {
                    shared_->services->ActivateViewportTool(kMaskToolId);
                }
            }
            Report(error);
        }
    }

    // 沿う曲線の層を選ぶ。
    void DrawSplineChoice(IEditorWorkspaceUi& ui, const TerrainStore& store, uint32_t& layerId, bool& changed) {
        ui.Text("Along Spline");
        for (const HeightLayer& layer : store.Data().Stack().layers) {
            if (layer.kind != HeightLayerKind::Spline) {
                continue;
            }
            const std::string id = std::to_string(layer.id);
            if (ui.Selectable("scatterSpline" + id, layer.name + "##spline" + id, layerId == layer.id)) {
                layerId = layer.id;
                changed = true;
            }
        }
    }

    // 規則は一覧から選び、選んだ一件の全部の項目を字段表から作る。面の規則の次に沿線の規則を並べる。
    void DrawScatter(IEditorWorkspaceUi& ui, TerrainStore& store) {
        const size_t pending = store.RuleBaker().PendingTiles();
        ui.Text(pending != 0u ? "Scatter rules (updating " + std::to_string(pending) + " tiles)" : "Scatter rules");
        ScatterRuleSet rules = store.Scatter().Rules();
        bool changed = false;
        const WorldView& world = store.World();
        const auto listRow = [&](size_t index, bool& enabled, const std::string& text) {
            const std::string id = std::to_string(index);
            changed |= ui.Checkbox("scatterEnabled" + id, "##scatterEnabled" + id, &enabled);
            ui.SameLine();
            if (ui.Selectable("scatterRow" + id, text + "##row" + id, selectedScatter_ == index)) {
                selectedScatter_ = index;
            }
        };
        for (size_t index = 0u; index < rules.rules.size(); ++index) {
            ScatterRule& rule = rules.rules[index];
            listRow(index, rule.enabled, rule.name + "  ->  " + rule.instanceSet);
        }
        for (size_t index = 0u; index < rules.lines.size(); ++index) {
            ScatterLineRule& rule = rules.lines[index];
            listRow(rules.rules.size() + index, rule.enabled, rule.name + "  (along)  ->  " + rule.instanceSet);
        }
        if (ui.Button("scatterAdd", "Add Rule")) {
            ScatterRule rule{};
            rule.name = "Rule " + std::to_string(rules.rules.size() + 1u);
            rule.seed = static_cast<uint32_t>(rules.rules.size() + rules.lines.size() + 1u) * 101u;
            rules.rules.push_back(std::move(rule));
            selectedScatter_ = rules.rules.size() - 1u;
            changed = true;
        }
        ui.SameLine();
        if (ui.Button("scatterAddLine", "Add Line Rule")) {
            ScatterLineRule rule{};
            rule.name = "Line " + std::to_string(rules.lines.size() + 1u);
            rule.seed = static_cast<uint32_t>(rules.rules.size() + rules.lines.size() + 1u) * 103u;
            rules.lines.push_back(std::move(rule));
            selectedScatter_ = rules.rules.size() + rules.lines.size() - 1u;
            changed = true;
        }
        ui.SameLine();
        if (ui.Button("scatterRemove", "Remove Selected")) {
            if (selectedScatter_ < rules.rules.size()) {
                rules.rules.erase(rules.rules.begin() + static_cast<std::ptrdiff_t>(selectedScatter_));
                changed = true;
            } else if (selectedScatter_ - rules.rules.size() < rules.lines.size()) {
                rules.lines.erase(rules.lines.begin() +
                                  static_cast<std::ptrdiff_t>(selectedScatter_ - rules.rules.size()));
                changed = true;
            }
        }
        ui.SameLine();
        if (ui.Button("scatterDefaults", "Use Mountain Rules")) {
            rules = DefaultScatterRules();
            for (const ScatterRule& rule : rules.rules) {
                if (world.FindInstanceSet(MakeComponentTypeId(rule.instanceSet.c_str())) == nullptr) {
                    GameProjectWorldInstanceSetDesc desc{};
                    std::memcpy(desc.name, rule.instanceSet.data(),
                                (std::min)(rule.instanceSet.size(), sizeof(desc.name) - 1u));
                    desc.geometry = GameProjectWorldMassRenderGeometry::BuiltInBox;
                    desc.tintVariation = 0.4f;
                    (void)world.AddInstanceSet(desc);
                }
            }
            changed = true;
        }
        if (selectedScatter_ < rules.rules.size()) {
            ScatterRule& rule = rules.rules[selectedScatter_];
            const auto custom = [&](std::string_view key, ScatterRule& object) {
                if (key == "enabled") {
                    return true;
                }
                if (key == "instanceSet") {
                    DrawSetChoice(ui, world, "scatterRule", object.instanceSet, changed);
                    return true;
                }
                if (key == "surfaceMask") {
                    DrawSurfaceSlots(ui, store, object.surfaceMask, changed);
                    return true;
                }
                if (key == "maskLayer") {
                    DrawMaskChoice(ui, store, object, changed);
                    return true;
                }
                return false;
            };
            // 独自の欄（custom）も changed へ書くので、先に描いてから合わせる。
            const bool fieldChanged = DrawFieldUi(ui, "scatterRule", rule, kScatterRuleFields, custom);
            changed = changed || fieldChanged;
        } else if (selectedScatter_ - rules.rules.size() < rules.lines.size()) {
            ScatterLineRule& rule = rules.lines[selectedScatter_ - rules.rules.size()];
            const auto custom = [&](std::string_view key, ScatterLineRule& object) {
                if (key == "enabled") {
                    return true;
                }
                if (key == "instanceSet") {
                    DrawSetChoice(ui, world, "scatterLine", object.instanceSet, changed);
                    return true;
                }
                if (key == "splineLayer") {
                    DrawSplineChoice(ui, store, object.splineLayer, changed);
                    return true;
                }
                return false;
            };
            const bool fieldChanged = DrawFieldUi(ui, "scatterLine", rule, kScatterLineRuleFields, custom);
            changed = changed || fieldChanged;
        }
        changed |= ui.InputFloat("scatterMargin", "Spline Margin (m)", &rules.splineMarginMeters);
        if (changed) {
            ChangeScatterRules(*shared_, std::move(rules));
        }
    }

    void DrawErosion(IEditorWorkspaceUi& ui, TerrainStore& store) {
        ValleySettings& valleys = shared_->valleys;
        ui.InputFloat("valleyErosion", "Valley Erosion", &valleys.erosion);
        ui.InputFloat("valleyDetail", "Valley Branching", &valleys.detail);
        ui.InputFloat("valleyLedges", "Rock Ledges", &valleys.ledges);
        ui.InputFloat("valleyRoughness", "Rock Roughness (m)", &valleys.roughness);
        ui.InputFloat("valleyStrength", "Valley Strength", &valleys.strength);
        if (ui.Button("valleys", "Carve Valleys")) {
            std::string error{};
            if (!RunValleys(*shared_, WholeTerrainRect(store), valleys, error)) {
                Report(error);
            }
        }
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
    NoiseLayerSettings noise_ = DefaultMountain();
    std::array<char, 95> instanceName_{"Rocks"};
    std::array<char, 95> instanceModel_{};
    float instanceTint_ = 0.3f;
    float instanceWind_ = 0.0f;
    float instanceMinScreen_ = 0.005f;
    std::array<char, 95> instanceFarModel_{};
    float instanceFarDistance_ = 180.0f;
    size_t selectedScatter_ = 0u;
    bool instanceRayTraced_ = true;
};

} // namespace

std::unique_ptr<IEditorPanel> MakeTerrainPanel(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<TerrainPanel>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
