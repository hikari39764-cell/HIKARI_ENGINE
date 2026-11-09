#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "HIKARI_TerrainKitFieldUi.h"
#include "HIKARI_TerrainKitWorkspacePages.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

// 木や岩など大量の物を置く頁。左で実例集を選び、右で実例集・筆・散布規則を設定する。
class PlacementPage final : public TerrainPage {
public:
    using TerrainPage::TerrainPage;

    const char* Name() const override { return "Placement"; }
    const char* Help() const override { return "Place trees, rocks and other objects by brush or by rules."; }
    const char* ToolId() const override { return kInstanceToolId; }

    void OnOpen(TerrainStore& store) override {
        // 何も選んでいなければ先頭を選ぶ。筆がすぐ使える。
        const WorldView& world = store.World();
        if (world.FindInstanceSet(ComponentTypeId{shared_->selectedInstanceSet}) != nullptr) {
            return;
        }
        std::array<ComponentTypeId, 64> ids{};
        if (world.GetInstanceSetIds(ids) != 0u) {
            shared_->selectedInstanceSet = ids[0].value;
        }
    }

    void DrawList(IEditorWorkspaceUi& ui, TerrainStore& store) override {
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
            }
        }
        ui.InputText("instanceNewName", "##newSetName", newSetName_.data(), newSetName_.size());
        ui.SameLine();
        if (ui.Button("instanceNew", "New Set") && newSetName_[0] != '\0') {
            GameProjectWorldInstanceSetDesc desc{};
            std::memcpy(desc.name, newSetName_.data(), (std::min)(newSetName_.size(), sizeof(desc.name) - 1u));
            desc.geometry = GameProjectWorldMassRenderGeometry::BuiltInBox;
            desc.flags = static_cast<uint32_t>(GameProjectWorldInstanceSetFlags::ReceiveShadow |
                                               GameProjectWorldInstanceSetFlags::RayTraced);
            desc.tintVariation = 0.3f;
            desc.minScreenHeight = 0.005f;
            desc.farDistance = 180.0f;
            if (world.AddInstanceSet(desc)) {
                shared_->selectedInstanceSet = MakeComponentTypeId(desc.name).value;
            } else {
                Report("Could not make the instance set. It needs a name no other set uses.");
            }
        }
        ui.Separator();
        InstanceBrushSettings& brush = shared_->instanceBrush;
        ui.Text("In the view");
        ui.SameLine();
        if (ui.Choice("instanceModePlace", "Place", brush.mode == InstanceBrushMode::Paint)) {
            brush.mode = InstanceBrushMode::Paint;
            ActivateTool(kInstanceToolId);
        }
        ui.Tooltip("Drag to place. Hold Shift to erase.");
        ui.SameLine();
        if (ui.Choice("instanceModeMove", "Move", brush.mode == InstanceBrushMode::Move)) {
            brush.mode = InstanceBrushMode::Move;
            ActivateTool(kInstanceToolId);
        }
        ui.Tooltip("Drag one instance to move it. It stays when the rules update.");
    }

    void DrawProperties(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        const WorldView& world = store.World();
        const GameProjectWorldInstanceSetDesc* set =
            world.FindInstanceSet(ComponentTypeId{shared_->selectedInstanceSet});
        if (set != nullptr && ui.Section("placementSet", std::string("Set: ") + set->name)) {
            DrawSet(ui, world, *set);
        }
        if (ui.Section("placementBrush", "Brush")) {
            InstanceBrushSettings& brush = shared_->instanceBrush;
            ui.SliderFloat("instanceRadius", "Size (m)", &brush.radiusMeters, 1.0f, 2048.0f);
            ui.SliderFloat("instanceDensity", "Density (per 100 m2)", &brush.density, 0.01f, 1000.0f);
            ui.SliderFloat("instanceSpacing", "Min Spacing (m)", &brush.spacingMeters, 0.05f, 100.0f);
            ui.SliderFloat("instanceScaleMin", "Scale Min", &brush.scaleMin, 0.05f, 20.0f);
            ui.SliderFloat("instanceScaleMax", "Scale Max", &brush.scaleMax, 0.05f, 20.0f);
            ui.SliderFloat("instanceAlign", "Align To Ground", &brush.alignToGround, 0.0f, 1.0f);
            ui.SliderFloat("instanceSlope", "Max Slope (deg)", &brush.maxSlopeDegrees, 0.0f, 90.0f);
        }
        if (ui.Section("placementRules", "Scatter Rules")) {
            DrawScatter(ui, store);
        }
    }

private:
    // 選んだ実例集の設定。同じ名前で登録し直すと、個体を残して設定だけ替わる。
    void DrawSet(IEditorWorkspaceUi& ui, const WorldView& world, const GameProjectWorldInstanceSetDesc& set) {
        GameProjectWorldInstanceSetDesc next = set;
        bool changed = ui.ModelField("instanceModel", "Model", next.modelAssetGuid, sizeof(next.modelAssetGuid));
        ui.Tooltip("Pick from the list, or drag a model here from the asset browser. None draws a box.");
        changed |=
            ui.ModelField("instanceFarModel", "Far Model", next.farModelAssetGuid, sizeof(next.farModelAssetGuid));
        ui.Tooltip("A simple model used in the distance. Bake one from the model below.");
        if (ui.Button("instanceBakeFar", "Bake Far Model From Model") && shared_->services != nullptr) {
            if (shared_->services->BakeFarModel(next.modelAssetGuid, next.farModelAssetGuid,
                                                sizeof(next.farModelAssetGuid))) {
                changed = true;
            } else {
                Report("The far model bake failed: it needs a model. See the log.");
            }
        }
        changed |= ui.SliderFloat("instanceFarDistance", "Far Model Distance (m)", &next.farDistance, 10.0f, 4000.0f);
        ui.Tooltip("At scale 1. Larger instances switch farther away.");
        changed |= ui.SliderFloat("instanceTint", "Color Variation", &next.tintVariation, 0.0f, 1.0f);
        changed |= ui.SliderFloat("instanceWind", "Wind", &next.windStrength, 0.0f, 3.0f);
        changed |= ui.SliderFloat("instanceMinScreen", "Hide Below Screen Height", &next.minScreenHeight, 0.0f, 0.05f);
        bool rayTraced = (next.flags & static_cast<uint32_t>(GameProjectWorldInstanceSetFlags::RayTraced)) != 0u;
        if (ui.Checkbox("instanceRay", "Ray Traced", &rayTraced)) {
            next.flags = static_cast<uint32_t>(rayTraced ? GameProjectWorldInstanceSetFlags::ReceiveShadow |
                                                               GameProjectWorldInstanceSetFlags::RayTraced
                                                         : GameProjectWorldInstanceSetFlags::ReceiveShadow);
            changed = true;
        }
        if (changed) {
            next.geometry = next.modelAssetGuid[0] != '\0' ? GameProjectWorldMassRenderGeometry::ModelAsset
                                                           : GameProjectWorldMassRenderGeometry::BuiltInBox;
            if (!world.AddInstanceSet(next)) {
                Report("The instance set is not valid.");
            }
        }
    }

    // 実例集を名前で選ぶ。
    void DrawSetChoice(IEditorWorkspaceUi& ui, const WorldView& world, const std::string& prefix, std::string& name,
                       bool& changed) {
        std::array<ComponentTypeId, 64> ids{};
        const uint32_t count = (std::min)(world.GetInstanceSetIds(ids), static_cast<uint32_t>(ids.size()));
        std::array<std::string_view, 64> items{};
        int32_t selected = -1;
        uint32_t used = 0u;
        for (uint32_t index = 0u; index < count; ++index) {
            const GameProjectWorldInstanceSetDesc* set = world.FindInstanceSet(ids[index]);
            if (set != nullptr) {
                items[used] = set->name;
                if (name == set->name) {
                    selected = static_cast<int32_t>(used);
                }
                ++used;
            }
        }
        if (ui.Combo(prefix + "set", "Instance Set", std::span(items).first(used), &selected)) {
            name = std::string(items[static_cast<size_t>(selected)]);
            changed = true;
        }
        if (!name.empty() && selected < 0) {
            ui.Text("\"" + name + "\" does not exist yet. Nothing is placed until it does.");
        }
    }

    // 置いてよい地表材質の枠を印で選ぶ。
    void DrawSurfaceSlots(IEditorWorkspaceUi& ui, const TerrainStore& store, uint32_t& mask, bool& changed) {
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
                ActivateTool(kMaskToolId);
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
        if (pending != 0u) {
            ui.Text("Updating " + std::to_string(pending) + " tiles");
        }
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
            const bool fieldChanged = DrawFieldUi(ui, "scatterRule", rule, kScatterRuleFields, custom, 1);
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
        changed |= ui.SliderFloat("scatterMargin", "Keep Clear Of Roads (m)", &rules.splineMarginMeters, 0.0f, 64.0f);
        if (changed) {
            ChangeScatterRules(*shared_, std::move(rules));
        }
    }

    std::array<char, 95> newSetName_{};
    size_t selectedScatter_ = 0u;
};

} // namespace

std::unique_ptr<TerrainPage> MakePlacementPage(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<PlacementPage>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
