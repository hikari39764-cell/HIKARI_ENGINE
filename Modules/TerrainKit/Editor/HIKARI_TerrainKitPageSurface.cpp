#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "HIKARI_TerrainKitFieldUi.h"
#include "HIKARI_TerrainKitWorkspacePages.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

constexpr float kSurfaceRowHeight = 44.0f;

// 草の一覧の絵。地面の図は草の下に隠れて見えないので、葉の色と背丈と穂から葉を描く。
void PaintGrassSwatch(const TerrainSurfaceGrass& grass, std::vector<uint8_t>& pixels) {
    constexpr int kSize = static_cast<int>(kSurfaceThumbnailSize);
    constexpr int kBlades = 44;
    const auto byte = [](float value) { return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f); };
    pixels.resize(static_cast<size_t>(kSize) * kSize * 4u);
    for (size_t index = 0u; index < pixels.size(); index += 4u) {
        pixels[index] = byte(grass.rootColor.x * 0.55f);
        pixels[index + 1u] = byte(grass.rootColor.y * 0.55f);
        pixels[index + 2u] = byte(grass.rootColor.z * 0.55f);
        pixels[index + 3u] = 255u;
    }
    // 背丈は 1.6 m で絵の高さいっぱい。
    const float tallest = std::clamp(grass.heightMax / 1.6f, 0.2f, 1.0f) * static_cast<float>(kSize - 2);
    const float shortest = tallest * std::clamp(grass.heightMin / (std::max)(grass.heightMax, 0.01f), 0.2f, 1.0f);
    uint32_t random = 0x9E3779B9u;
    const auto next = [&random]() {
        random = random * 1664525u + 1013904223u;
        return static_cast<float>(random >> 8u) / 16777216.0f;
    };
    for (int blade = 0; blade < kBlades; ++blade) {
        const float rootX = next() * static_cast<float>(kSize);
        const float height = shortest + (tallest - shortest) * next();
        const float lean = (next() - 0.5f) * 2.0f * (std::sin(grass.tiltDegrees * 0.01745f) * height * 0.8f);
        const bool plume = next() < grass.plumeShare;
        for (float t = 0.0f; t <= 1.0f; t += 1.0f / height) {
            const int y = kSize - 1 - static_cast<int>(t * height);
            const int x = static_cast<int>(rootX + lean * t * t);
            const bool head = plume && t > 0.72f;
            const MATH::Vec3 color = head ? grass.plumeColor
                                          : MATH::Vec3{grass.rootColor.x + (grass.tipColor.x - grass.rootColor.x) * t,
                                                       grass.rootColor.y + (grass.tipColor.y - grass.rootColor.y) * t,
                                                       grass.rootColor.z + (grass.tipColor.z - grass.rootColor.z) * t};
            // 図の中の小さな絵なので実際より明るく描く。
            const float shown = head ? 1.0f : 1.7f;
            const int width = head ? 2 : (t < 0.5f ? 2 : 1);
            for (int offset = 0; offset < width; ++offset) {
                const int column = x + offset;
                if (column < 0 || column >= kSize || y < 0) {
                    continue;
                }
                uint8_t* pixel = pixels.data() + (static_cast<size_t>(y) * kSize + column) * 4u;
                pixel[0] = byte(color.x * shown);
                pixel[1] = byte(color.y * shown);
                pixel[2] = byte(color.z * shown);
            }
        }
    }
}

// 地面を塗る頁と草を塗る頁。どちらも同じ筆で材質の枠を塗る（草はその材質が決める）。
class SurfacePageBase : public TerrainPage {
public:
    using TerrainPage::TerrainPage;

    const char* ToolId() const override { return kPaintToolId; }

protected:
    void DrawBrush(IEditorWorkspaceUi& ui) {
        ControlBrushSettings& brush = shared_->paintBrush;
        ui.SliderFloat("paintRadius", "Size (m)", &brush.radiusMeters, 1.0f, 2048.0f);
        ui.SliderFloat("paintStrength", "Strength", &brush.strength, 0.01f, 1.0f);
        ui.SliderFloat("paintHardness", "Hardness", &brush.hardness, 0.0f, 1.0f);
        ui.Tooltip("The inner ring in the view: full strength inside it, fading out to the outer ring.");
    }

    void DrawLibraryState(IEditorWorkspaceUi& ui, const TerrainSurfaceLibrary& library) {
        if (library.Busy()) {
            ui.Text("Preparing surfaces: " + std::to_string(static_cast<int>(library.Progress() * 100.0f)) + "%");
        }
        if (!library.LastError().empty()) {
            ui.Text(library.LastError());
        }
    }

    void DrawSurfaceRow(IEditorWorkspaceUi& ui, const TerrainSurfaceLibrary& library, uint32_t slot) {
        const auto& surfaces = library.Surfaces();
        const uint8_t* pixels = nullptr;
        uint64_t revision = library.Revision();
        if (slot < surfaces.size()) {
            if (surfaces[slot].grass.density > 0.0f) {
                const GrassSwatch& swatch = SwatchOf(slot, surfaces[slot].grass);
                pixels = swatch.pixels.data();
                revision = swatch.revision;
            } else if (!surfaces[slot].thumbnail.empty()) {
                pixels = surfaces[slot].thumbnail.data();
            }
        }
        ControlBrushSettings& brush = shared_->paintBrush;
        if (ui.ImageSelectable("surface" + std::to_string(slot), library.Slots()[slot], pixels, kSurfaceThumbnailSize,
                               kSurfaceThumbnailSize, revision, brush.material == slot, kSurfaceRowHeight)) {
            brush.material = slot;
        }
    }

private:
    struct GrassSwatch {
        TerrainSurfaceGrass grass{};
        std::vector<uint8_t> pixels{};
        uint64_t revision = 0u;
    };

    // 草の設定が変わった時だけ描き直す。
    const GrassSwatch& SwatchOf(uint32_t slot, const TerrainSurfaceGrass& grass) {
        GrassSwatch& swatch = swatches_[slot];
        if (swatch.pixels.empty() || !(swatch.grass == grass)) {
            swatch.grass = grass;
            PaintGrassSwatch(grass, swatch.pixels);
            swatch.revision = (1ull << 63u) | ++swatchRevision_;
        }
        return swatch;
    }

    std::map<uint32_t, GrassSwatch> swatches_{};
    uint64_t swatchRevision_ = 0u;
};

class SurfacePage final : public SurfacePageBase {
public:
    using SurfacePageBase::SurfacePageBase;

    const char* Name() const override { return "Surface"; }
    const char* Help() const override { return "Paint the ground by hand and set the rules that paint it for you."; }

    void OnOpen(TerrainStore& store) override { catalog_ = ListTerrainSurfaces(store.ProjectRoot()); }

    void DrawList(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        TerrainSurfaceLibrary& library = store.Surfaces();
        DrawLibraryState(ui, library);
        ui.Text("Pick a surface, then paint in the view");
        for (uint32_t slot = 0u; slot < library.Slots().size(); ++slot) {
            DrawSurfaceRow(ui, library, slot);
        }
    }

    void DrawProperties(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        if (ui.Section("surfaceBrush", "Brush")) {
            DrawBrush(ui);
        }
        if (ui.Section("surfaceBrushShape", "Brush Shape", false)) {
            DrawBrushShapes(ui, store, *shared_);
        }
        if (ui.Section("surfaceSlot", "Selected Slot", false)) {
            DrawSlot(ui, store);
        }
        if (ui.Section("surfaceRules", "Automatic Rules")) {
            DrawRules(ui, store);
        }
        if (ui.Section("surfaceWeather", "Snow and Wetness", false)) {
            TerrainWeather weather = store.Weather();
            bool changed = ui.InputFloat("snowLine", "Snow Line (m)", &weather.snowLineMeters);
            changed |= ui.SliderFloat("snowAmount", "Snow Amount", &weather.snowAmount, 0.0f, 1.0f);
            changed |= ui.SliderFloat("wetness", "Wetness", &weather.wetness, 0.0f, 1.0f);
            if (changed) {
                ChangeWeather(*shared_, weather);
            }
        }
    }

private:
    // 枠の中身を選べる材質（内蔵とプロジェクトの Assets）から替える。
    void DrawSlot(IEditorWorkspaceUi& ui, TerrainStore& store) {
        std::vector<std::string> names = store.Surfaces().Slots();
        const uint32_t slot = shared_->paintBrush.material;
        std::vector<std::string_view> items{};
        for (const SurfaceCatalogEntry& entry : catalog_) {
            items.emplace_back(entry.name);
        }
        int32_t current = -1;
        if (slot < names.size()) {
            for (size_t index = 0u; index < catalog_.size(); ++index) {
                if (catalog_[index].name == names[slot]) {
                    current = static_cast<int32_t>(index);
                }
            }
        }
        if (ui.Combo("slotSurface", "Surface", items, &current) && slot < names.size()) {
            names[slot] = catalog_[static_cast<size_t>(current)].name;
            store.SetSurfaceSlots(names);
        }
        ui.Tooltip("Add your own: put a .hterrainsurface file under the project's Assets folder.");
        if (ui.Button("slotAdd", "Add Slot") && !catalog_.empty() && names.size() < kTerrainControlMaterialCount) {
            names.push_back(catalog_.front().name);
            shared_->paintBrush.material = static_cast<uint32_t>(names.size() - 1u);
            store.SetSurfaceSlots(names);
        }
    }

    void DrawRules(IEditorWorkspaceUi& ui, TerrainStore& store) {
        const size_t pending = store.RuleBaker().PendingTiles();
        if (pending != 0u) {
            ui.Text("Updating " + std::to_string(pending) + " tiles");
        }
        SurfaceRuleSet rules = store.SurfaceRules();
        bool changed = false;
        for (size_t index = 0u; index < rules.rules.size(); ++index) {
            SurfaceRule& rule = rules.rules[index];
            const std::string id = std::to_string(index);
            changed |= ui.Checkbox("ruleEnabled" + id, "##ruleEnabled" + id, &rule.enabled);
            ui.SameLine();
            const std::string name = rule.name.empty() ? "Rule " + std::to_string(index + 1u) : rule.name;
            if (ui.Selectable("ruleRow" + id, name, selectedRule_ == index)) {
                selectedRule_ = index;
            }
        }
        if (ui.Button("ruleAdd", "Add Rule")) {
            rules.rules.push_back({});
            selectedRule_ = rules.rules.size() - 1u;
            changed = true;
        }
        ui.SameLine();
        if (ui.Button("ruleRemove", "Remove Selected") && selectedRule_ < rules.rules.size()) {
            rules.rules.erase(rules.rules.begin() + static_cast<std::ptrdiff_t>(selectedRule_));
            changed = true;
        }
        if (selectedRule_ < rules.rules.size()) {
            SurfaceRule& rule = rules.rules[selectedRule_];
            const auto custom = [&](std::string_view key, SurfaceRule& object) {
                if (key == "enabled") {
                    return true;
                }
                if (key == "surface") {
                    changed |= DrawSurfaceChoice(ui, "ruleSurface", "Surface", store, object.surface);
                    return true;
                }
                return false;
            };
            // 独自の欄（custom）も changed へ書くので、先に描いてから合わせる。
            const bool fieldChanged = DrawFieldUi(ui, "surfaceRule", rule, kSurfaceRuleFields, custom, 2);
            changed = changed || fieldChanged;
        }
        if (changed) {
            ChangeSurfaceRules(*shared_, std::move(rules));
        }
    }

    std::vector<SurfaceCatalogEntry> catalog_{};
    size_t selectedRule_ = 0u;
};

class GrassPage final : public SurfacePageBase {
public:
    using SurfacePageBase::SurfacePageBase;

    const char* Name() const override { return "Grass"; }
    const char* Help() const override { return "Paint which grass grows where, and change how each grass looks."; }

    void DrawList(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        TerrainSurfaceLibrary& library = store.Surfaces();
        DrawLibraryState(ui, library);
        const auto& surfaces = library.Surfaces();
        const auto hasGrass = [&](uint32_t slot) {
            return slot < surfaces.size() && surfaces[slot].grass.density > 0.0f;
        };
        ui.Text("Pick a grass, then paint in the view");
        for (uint32_t slot = 0u; slot < library.Slots().size(); ++slot) {
            if (hasGrass(slot)) {
                DrawSurfaceRow(ui, library, slot);
            }
        }
        ui.Separator();
        ui.Text("No grass (paint to clear)");
        for (uint32_t slot = 0u; slot < library.Slots().size(); ++slot) {
            if (!hasGrass(slot)) {
                DrawSurfaceRow(ui, library, slot);
            }
        }
    }

    void DrawProperties(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        if (ui.Section("grassBrush", "Brush")) {
            DrawBrush(ui);
        }
        if (ui.Section("grassBrushShape", "Brush Shape", false)) {
            DrawBrushShapes(ui, store, *shared_);
        }
        TerrainSurfaceLibrary& library = store.Surfaces();
        const auto& surfaces = library.Surfaces();
        const uint32_t slot = shared_->paintBrush.material;
        if (slot < surfaces.size() && ui.Section("grassSelected", "Grass On " + surfaces[slot].asset.name)) {
            const CookedSurface& surface = surfaces[slot];
            TerrainSurfaceGrass grass = surface.grass;
            if (DrawFieldUi(ui, "surfaceGrass", grass, kTerrainSurfaceGrassFields, NoCustomFieldUi{}, 2)) {
                ChangeSurfaceGrass(*shared_, surface.asset.name, grass);
            }
            if (library.GrassOverrides().contains(surface.asset.name) &&
                ui.Button("surfaceGrassReset", "Reset To The Surface Asset")) {
                ChangeSurfaceGrass(*shared_, surface.asset.name, std::nullopt);
            }
            ui.Separator();
            ui.InputText("surfaceSaveName", "New Grass Name", saveName_.data(), saveName_.size());
            if (ui.Button("surfaceSaveAs", "Save As New Grass")) {
                SaveAsNewGrass(store, surface);
            }
            ui.Tooltip("Keeps these settings as a grass of this project, added to the list on the left. "
                       "Other projects keep their own.");
        }
        if (ui.Section("grassAll", "All Grass", false)) {
            TerrainGrassSettings grass = store.Grass();
            if (DrawFieldUi(ui, "grass", grass, kTerrainGrassSettingsFields)) {
                ChangeGrassSettings(*shared_, grass);
            }
        }
    }

private:
    void SaveAsNewGrass(TerrainStore& store, const CookedSurface& surface) {
        const std::string name = saveName_.data();
        std::string error{};
        std::vector<std::string> names = store.Surfaces().Slots();
        if (names.size() >= kTerrainControlMaterialCount) {
            Report("All 32 surface slots are in use.");
        } else if (!SaveTerrainSurfaceAs(store.ProjectRoot(), surface.asset.name, name, surface.grass, error)) {
            Report(error);
        } else {
            names.push_back(name);
            shared_->paintBrush.material = static_cast<uint32_t>(names.size() - 1u);
            store.SetSurfaceSlots(std::move(names));
            saveName_.fill('\0');
        }
    }

    std::array<char, 64> saveName_{};
};

} // namespace

bool DrawSurfaceChoice(IEditorWorkspaceUi& ui, const std::string& id, const std::string& label,
                       const TerrainStore& store, uint32_t& slot) {
    const std::vector<std::string>& names = store.Surfaces().Slots();
    std::vector<std::string_view> items(names.begin(), names.end());
    int32_t selected = static_cast<int32_t>(slot);
    if (!ui.Combo(id, label, items, &selected)) {
        return false;
    }
    slot = static_cast<uint32_t>(selected);
    return true;
}

std::unique_ptr<TerrainPage> MakeSurfacePage(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<SurfacePage>(std::move(shared));
}

std::unique_ptr<TerrainPage> MakeGrassPage(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<GrassPage>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
