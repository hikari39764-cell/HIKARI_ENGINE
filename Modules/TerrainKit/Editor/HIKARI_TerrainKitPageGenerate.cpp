#include <algorithm>
#include <string>

#include "HIKARI_TerrainKitWorkspacePages.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

// 一度で作る形（Gaea や World Creator の始め方に倣う）。
struct TerrainPreset {
    const char* name;
    const char* help;
    NoiseFractal fractal;
    float amplitudeMeters;
    float featureSizeMeters;
    float massifRadiusMeters;
    uint32_t octaves;
};

constexpr TerrainPreset kTerrainPresets[] = {
    {"Rolling Hills", "Soft hills about 45 m high.", NoiseFractal::FBm, 45.0f, 700.0f, 0.0f, 5u},
    {"Plains", "Almost flat ground with gentle swells.", NoiseFractal::FBm, 12.0f, 900.0f, 0.0f, 4u},
    {"Highlands", "Broad high ground, up to about 160 m.", NoiseFractal::FBm, 160.0f, 900.0f, 0.0f, 6u},
    {"Single Mountain", "One large mountain in the middle.", NoiseFractal::Ridged, 900.0f, 1100.0f, 1000.0f, 5u},
    {"Mountain Range", "Sharp ridges across the whole terrain.", NoiseFractal::Ridged, 600.0f, 1400.0f, 0.0f, 6u},
};

// 地形全体を一度に作り替える頁。
class GeneratePage final : public TerrainPage {
public:
    using TerrainPage::TerrainPage;

    const char* Name() const override { return "Generate"; }
    const char* Help() const override { return "Make the whole terrain at once, then carve and erode it."; }
    const char* ToolId() const override { return ""; }

    void DrawList(IEditorWorkspaceUi& ui, TerrainStore&) override {
        ui.Text("Start from");
        std::string error{};
        for (const TerrainPreset& preset : kTerrainPresets) {
            if (ui.Button(std::string("preset") + preset.name, preset.name)) {
                NoiseLayerSettings noise = DefaultNoise();
                noise.fractal = preset.fractal;
                noise.amplitudeMeters = preset.amplitudeMeters;
                noise.featureSizeMeters = preset.featureSizeMeters;
                noise.massifRadiusMeters = preset.massifRadiusMeters;
                noise.octaves = preset.octaves;
                noise_ = noise;
                Generate(error);
            }
            ui.Tooltip(preset.help);
        }
        ui.Separator();
        ui.Text("This replaces all layers. Ctrl + Z brings them back.");
        Report(error);
    }

    void DrawProperties(IEditorWorkspaceUi& ui, TerrainStore& store) override {
        if (ui.Section("generateCustom", "Set The Size Yourself")) {
            ui.InputFloat("amplitude", "Height (m)", &noise_.amplitudeMeters);
            ui.SliderFloat("feature", "Feature Size (m)", &noise_.featureSizeMeters, 16.0f, 8192.0f);
            ui.InputInt("seed", "Variation", &noise_.seed);
            if (ui.Button("generate", "Generate Terrain")) {
                std::string error{};
                Generate(error);
                Report(error);
            }
        }
        if (ui.Section("generateValleys", "Carve Valleys")) {
            DrawValleys(ui, store);
        }
        if (ui.Section("generateErosion", "Erosion")) {
            DrawErosion(ui, store);
        }
    }

private:
    // 作り替えると相機が山に埋まることがあるので、全体が見える所へ置き直す。
    void Generate(std::string& error) {
        if (RegenerateTerrain(*shared_, noise_, error)) {
            FrameTerrainView(*shared_);
        }
    }

    void DrawValleys(IEditorWorkspaceUi& ui, TerrainStore& store) {
        ValleySettings& valleys = shared_->valleys;
        ui.InputFloat("valleyErosion", "Depth", &valleys.erosion);
        ui.InputFloat("valleyDetail", "Branching", &valleys.detail);
        ui.InputFloat("valleyLedges", "Rock Ledges", &valleys.ledges);
        ui.InputFloat("valleyRoughness", "Rock Roughness (m)", &valleys.roughness);
        ui.InputFloat("valleyStrength", "Strength", &valleys.strength);
        if (ui.Button("valleys", "Carve Valleys")) {
            std::string error{};
            if (!RunValleys(*shared_, WholeTerrainRect(store), valleys, error)) {
                Report(error);
            }
        }
    }

    void DrawErosion(IEditorWorkspaceUi& ui, TerrainStore& store) {
        ErosionRequest& erosion = shared_->erosion;
        ui.Checkbox("hydraulic", "Water", &erosion.hydraulic);
        ui.Tooltip("Water cuts gullies and carries sediment downhill.");
        ui.SameLine();
        ui.Checkbox("thermal", "Crumbling", &erosion.thermal);
        ui.Tooltip("Slopes steeper than the talus angle crumble and form scree.");
        int32_t iterations = static_cast<int32_t>(erosion.hydraulicSettings.iterations);
        if (ui.InputInt("erosionIterations", "Water Iterations", &iterations)) {
            erosion.hydraulicSettings.iterations = static_cast<uint32_t>(std::clamp(iterations, 1, 20000));
        }
        ui.InputFloat("erosionRain", "Rain (m/s)", &erosion.hydraulicSettings.rainRate);
        ui.SliderFloat("erosionTalus", "Talus Angle (deg)", &erosion.thermalSettings.talusAngleDegrees, 5.0f, 70.0f);
        if (store.Erosion().Running()) {
            ui.Text("Eroding: " + std::to_string(static_cast<int>(store.Erosion().Progress() * 100.0f)) + "%");
        } else if (ui.Button("erode", "Erode Whole Terrain")) {
            std::string error{};
            if (!StartGpuErosion(shared_, WholeTerrainRect(store), erosion, error)) {
                Report(error);
            }
        }
    }

    NoiseLayerSettings noise_ = DefaultMountain();
};

} // namespace

std::unique_ptr<TerrainPage> MakeGeneratePage(std::shared_ptr<EditorShared> shared) {
    return std::make_unique<GeneratePage>(std::move(shared));
}

} // namespace HIKARI::TERRAINKIT
