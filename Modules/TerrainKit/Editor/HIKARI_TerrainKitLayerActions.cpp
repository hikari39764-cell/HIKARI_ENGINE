#include <algorithm>
#include <memory>
#include <system_error>

#include <json.hpp>

#include "HIKARI_TerrainKitEditorCommon.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using namespace TERRAIN::DATA;

constexpr uint32_t kBuiltinStampSize = 513u;
constexpr char kProjectStampDirectory[] = "Assets/TerrainStamps";

// 一件の差分。瓦片の前後の画素と層の記述を持ち、撤销と重做で戻して焼き直す。
class TerrainEditUndo final : public IEditorUndoEntry {
public:
    explicit TerrainEditUndo(TerrainEdit edit) : edit_(std::move(edit)) {}
    void Undo() override { Apply(UndoTerrainEdit); }
    void Redo() override { Apply(RedoTerrainEdit); }

private:
    template <class Function>
    void Apply(Function function) {
        TerrainStore& store = TerrainStore::Get();
        std::string error{};
        if (!store.IsOpen() || !function(store.Data(), edit_, error)) {
            return;
        }
        if (edit_.stackBefore.has_value()) {
            store.RebakeAfterStackChange();
        } else {
            store.RebakeChanged(edit_.ChangedTiles());
        }
    }

    TerrainEdit edit_;
};

// 地形全体を作り直した時の撤销。前のデータをそのまま持って入れ替える。
class ReplaceUndo final : public IEditorUndoEntry {
public:
    explicit ReplaceUndo(std::unique_ptr<TerrainData> previous) : other_(std::move(previous)) {}
    void Undo() override { Swap(); }
    void Redo() override { Swap(); }

private:
    void Swap() {
        if (TerrainStore::Get().IsOpen() && other_ != nullptr) {
            other_ = TerrainStore::Get().ReplaceData(std::move(other_));
        }
    }

    std::unique_ptr<TerrainData> other_;
};

// 材質規則の前後を持つ撤销。
class RulesUndo final : public IEditorUndoEntry {
public:
    RulesUndo(SurfaceRuleSet before, SurfaceRuleSet after) : before_(std::move(before)), after_(std::move(after)) {}
    void Undo() override { TerrainStore::Get().SetSurfaceRules(before_); }
    void Redo() override { TerrainStore::Get().SetSurfaceRules(after_); }

private:
    SurfaceRuleSet before_;
    SurfaceRuleSet after_;
};

// 積雪と湿りの前後を持つ撤销。
class WeatherUndo final : public IEditorUndoEntry {
public:
    WeatherUndo(TerrainWeather before, TerrainWeather after) : before_(before), after_(after) {}
    void Undo() override { TerrainStore::Get().SetWeather(before_); }
    void Redo() override { TerrainStore::Get().SetWeather(after_); }

private:
    TerrainWeather before_;
    TerrainWeather after_;
};

// 層の記述を変える手続きの共通部分：撤销を取り、焼き直す。
template <class Change>
bool ChangeStack(EditorShared& shared, const char* label, Change change, std::string& error) {
    TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        error = "No terrain is open.";
        return false;
    }
    TerrainEditRecorder recorder(store.Data());
    if (!change(store.Data(), recorder)) {
        if (error.empty()) {
            error = "The terrain layers could not be changed.";
        }
        return false;
    }
    store.RebakeAfterStackChange();
    RecordTerrainEdit(shared, recorder.Finish(label));
    return true;
}

std::filesystem::path ProjectStampDirectory() { return TerrainStore::Get().ProjectRoot() / kProjectStampDirectory; }

bool IsStampImage(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    return extension == ".exr" || extension == ".r16" || extension == ".raw";
}

bool LoadStamp(std::string_view name, HeightImage& image, std::string& error) {
    if (MakeBuiltinStamp(name, kBuiltinStampSize, image)) {
        return true;
    }
    const std::filesystem::path directory = ProjectStampDirectory();
    std::error_code ec{};
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (IsStampImage(entry.path()) && entry.path().stem().string() == name) {
            return LoadHeightImage(entry.path(), 0u, image, error);
        }
    }
    error = "Unknown stamp: " + std::string(name);
    return false;
}

} // namespace

uint32_t TargetSculptLayer(const EditorShared& shared) {
    const TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        return 0u;
    }
    const HeightLayer* selected = store.Data().FindLayer(shared.selectedLayer);
    return selected != nullptr && selected->kind == HeightLayerKind::Sculpt ? selected->id : store.SculptLayerId();
}

void RecordTerrainEdit(EditorShared& shared, TerrainEdit edit) {
    if (edit.Empty() || shared.services == nullptr) {
        return;
    }
    const std::string label = edit.label;
    shared.services->RecordUndo(label, std::make_unique<TerrainEditUndo>(std::move(edit)));
}

NoiseLayerSettings DefaultNoise() {
    NoiseLayerSettings noise{};
    noise.amplitudeMeters = 140.0f;
    noise.offsetMeters = 60.0f;
    noise.featureSizeMeters = 900.0f;
    noise.octaves = 6u;
    noise.fractal = NoiseFractal::FBm;
    return noise;
}

bool RegenerateTerrain(EditorShared& shared, const NoiseLayerSettings& noise, std::string& error) {
    TerrainStore& store = TerrainStore::Get();
    auto generated = store.MakeGeneratedData(noise, error);
    if (generated == nullptr) {
        return false;
    }
    auto previous = store.ReplaceData(std::move(generated));
    shared.selectedLayer = 0u;
    if (shared.services != nullptr) {
        shared.services->RecordUndo("Generate Terrain", std::make_unique<ReplaceUndo>(std::move(previous)));
    }
    return true;
}

bool AddTerrainLayer(EditorShared& shared, HeightLayer layer, std::size_t index, uint32_t& outId, std::string& error) {
    return ChangeStack(
        shared, "Add Terrain Layer",
        [&](TerrainData& data, TerrainEditRecorder&) {
            outId = data.AddLayer(std::move(layer), index);
            return outId != 0u;
        },
        error);
}

bool RemoveTerrainLayer(EditorShared& shared, uint32_t id, std::string& error) {
    return ChangeStack(
        shared, "Remove Terrain Layer",
        [&](TerrainData& data, TerrainEditRecorder& recorder) {
            if (data.FindLayer(id) == nullptr) {
                error = "Unknown terrain layer.";
                return false;
            }
            recorder.TouchAll(TerrainChannel::LayerValues, id);
            recorder.TouchAll(TerrainChannel::LayerMask, id);
            return data.RemoveLayer(id);
        },
        error);
}

bool MoveTerrainLayer(EditorShared& shared, uint32_t id, std::size_t index, std::string& error) {
    return ChangeStack(
        shared, "Move Terrain Layer",
        [&](TerrainData& data, TerrainEditRecorder&) { return data.MoveLayer(id, index); }, error);
}

bool UpdateTerrainLayer(EditorShared& shared, const HeightLayer& layer, std::string& error) {
    return ChangeStack(
        shared, "Edit Terrain Layer", [&](TerrainData& data, TerrainEditRecorder&) { return data.SetLayer(layer); },
        error);
}

std::vector<std::string> ListStampNames() {
    std::vector<std::string> names{};
    for (const std::string_view name : BuiltinStampNames()) {
        names.emplace_back(name);
    }
    std::error_code ec{};
    for (const auto& entry : std::filesystem::directory_iterator(ProjectStampDirectory(), ec)) {
        if (IsStampImage(entry.path())) {
            names.push_back(entry.path().stem().string());
        }
    }
    return names;
}

void ChangeSurfaceRules(EditorShared& shared, SurfaceRuleSet rules) {
    TerrainStore& store = TerrainStore::Get();
    SurfaceRuleSet before = store.SurfaceRules();
    if (before == rules) {
        return;
    }
    store.SetSurfaceRules(rules);
    if (shared.services != nullptr) {
        shared.services->RecordUndo("Edit Terrain Rules",
                                    std::make_unique<RulesUndo>(std::move(before), std::move(rules)));
    }
}

void ChangeWeather(EditorShared& shared, const TerrainWeather& weather) {
    TerrainStore& store = TerrainStore::Get();
    const TerrainWeather before = store.Weather();
    store.SetWeather(weather);
    if (!(store.Weather() == before) && shared.services != nullptr) {
        shared.services->RecordUndo("Edit Terrain Weather", std::make_unique<WeatherUndo>(before, store.Weather()));
    }
}

bool RunStamp(EditorShared& shared, std::string_view stampName, const StampSettings& settings, std::string& error) {
    TerrainStore& store = TerrainStore::Get();
    const uint32_t layer = TargetSculptLayer(shared);
    if (!store.IsOpen() || layer == 0u) {
        error = "No terrain with a sculpt layer is open.";
        return false;
    }
    HeightImage image{};
    if (!LoadStamp(stampName, image, error)) {
        return false;
    }
    TerrainEditRecorder recorder(store.Data());
    BrushEditResult edit{};
    if (!ApplyStamp(store.Data(), layer, image, settings, &recorder, edit, error)) {
        return false;
    }
    store.RebakeRect(edit.touched);
    RecordTerrainEdit(shared, recorder.Finish("Stamp Terrain"));
    return true;
}

bool RunErosion(EditorShared& shared, const SampleRect& rect, const ErosionRequest& request, ErosionMaps* maps,
                std::string& error) {
    TerrainStore& store = TerrainStore::Get();
    const uint32_t layer = TargetSculptLayer(shared);
    if (!store.IsOpen() || layer == 0u) {
        error = "No terrain with a sculpt layer is open.";
        return false;
    }
    TerrainEditRecorder recorder(store.Data());
    if (!ApplyErosionToLayer(store.Data(), layer, rect, request, &recorder, maps, error)) {
        return false;
    }
    store.RebakeRect(rect);
    RecordTerrainEdit(shared, recorder.Finish("Erode Terrain"));
    return true;
}

bool StartGpuErosion(const std::shared_ptr<EditorShared>& shared, const SampleRect& rect, const ErosionRequest& request,
                     std::string& error) {
    TerrainStore& store = TerrainStore::Get();
    const uint32_t layer = TargetSculptLayer(*shared);
    if (!store.IsOpen() || layer == 0u) {
        error = "No terrain with a sculpt layer is open.";
        return false;
    }
    if (store.Erosion().Running()) {
        error = "Erosion is already running.";
        return false;
    }
    auto input = std::make_shared<ErosionInput>();
    if (!PrepareErosionInput(store.Data(), layer, rect, *input, error)) {
        return false;
    }
    shared->lastErosion.clear();
    auto completion = [shared, rect, request, layer, input](GpuErosionResult result) {
        TerrainStore& target = TerrainStore::Get();
        nlohmann::json report{
            {"ok", result.ok}, {"seconds", result.seconds}, {"samples", rect.Count()}, {"error", result.error}};
        std::string message{};
        if (result.ok && target.IsOpen()) {
            TerrainEditRecorder recorder(target.Data());
            if (CommitErosionResult(target.Data(), layer, rect, request, *input, result.heights, &recorder, message)) {
                target.RebakeRect(rect);
                RecordTerrainEdit(*shared, recorder.Finish("Erode Terrain"));
                report["maps"] = nlohmann::json::parse(WriteErosionMaps(target, rect, result.maps));
            } else {
                report["ok"] = false;
                report["error"] = message;
            }
        }
        shared->lastErosion = report.dump();
        if (shared->commands != nullptr && !report["ok"].get<bool>()) {
            shared->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, report["error"].get<std::string>());
        }
    };
    if (!store.Erosion().Start(rect.width, rect.height, input->field.spacingMeters, input->field.heights, request,
                               std::move(completion))) {
        error = "Erosion could not be started.";
        return false;
    }
    return true;
}

SampleRect WholeTerrainRect(const TerrainStore& store) {
    const TerrainSettings& settings = store.Settings();
    const TerrainGrid& grid = settings.grid;
    const SampleRect first = TileSampleRect(grid, TileCoord{settings.firstTileX, settings.firstTileZ});
    const SampleRect last = TileSampleRect(
        grid, TileCoord{settings.firstTileX + settings.tilesX - 1, settings.firstTileZ + settings.tilesZ - 1});
    return SampleRect{first.x0, first.z0, static_cast<uint32_t>(last.X1() - first.x0 + 1),
                      static_cast<uint32_t>(last.Z1() - first.z0 + 1)};
}

} // namespace HIKARI::TERRAINKIT
