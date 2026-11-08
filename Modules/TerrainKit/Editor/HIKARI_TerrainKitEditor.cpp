#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <json.hpp>

#include "HIKARI/Project/GameProjectEditorModule.h"
#include "HIKARI_TerrainKitStore.h"
#include "TerrainKit/Data/HIKARI_TerrainDataBrush.h"
#include "TerrainKit/Data/HIKARI_TerrainDataEdit.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace EDITOR_PROJECT;
using TERRAIN::DATA::HeightBrushOp;
using TERRAIN::DATA::HeightBrushSettings;
using TERRAIN::DATA::TerrainEdit;
using TERRAIN::DATA::TerrainEditRecorder;

constexpr char kSculptToolId[] = "terrainkit.sculpt";
constexpr EditorWorkspaceColor kBrushColor{1.0f, 0.82f, 0.25f, 1.0f};

// 工具・窓・操作で共有する。編集器の所有スレッドだけで触る。
struct EditorShared {
    IEditorSceneServices* services = nullptr;
    IEditorWorkspaceCommands* commands = nullptr;
    HeightBrushSettings brush{};
    uint32_t strokes = 0u;
};

void ReplyFailure(IEditorOperationResultSink& result, std::string_view code, const std::string& message) {
    result.Receive(false, code, message, "{}");
}

// 一筆の差分。瓦片の前後の画素を持ち、撤销と重做で戻して焼き直す。
class StrokeUndo final : public IEditorUndoEntry {
public:
    explicit StrokeUndo(TerrainEdit edit) : edit_(std::move(edit)) {}
    void Undo() override { Apply(TERRAIN::DATA::UndoTerrainEdit); }
    void Redo() override { Apply(TERRAIN::DATA::RedoTerrainEdit); }

private:
    template <class Function>
    void Apply(Function function) {
        TerrainStore& store = TerrainStore::Get();
        std::string error{};
        if (store.IsOpen() && function(store.Data(), edit_, error)) {
            store.RebakeChanged(edit_.ChangedTiles());
        }
    }

    TerrainEdit edit_;
};

// 地形全体を作り直した時の撤销。前のデータをそのまま持って入れ替える。
class ReplaceUndo final : public IEditorUndoEntry {
public:
    explicit ReplaceUndo(std::unique_ptr<TERRAIN::DATA::TerrainData> previous) : other_(std::move(previous)) {}
    void Undo() override { Swap(); }
    void Redo() override { Swap(); }

private:
    void Swap() {
        if (TerrainStore::Get().IsOpen() && other_ != nullptr) {
            other_ = TerrainStore::Get().ReplaceData(std::move(other_));
        }
    }

    std::unique_ptr<TERRAIN::DATA::TerrainData> other_;
};

bool ReadBrushOp(std::string_view name, HeightBrushOp& out) {
    for (const auto& entry : TERRAIN::DATA::kHeightBrushOpNames) {
        if (name == entry.name) {
            out = entry.value;
            return true;
        }
    }
    return false;
}

// 噪声の設定で地形を作り直し、撤销に載せる。
bool RegenerateTerrain(EditorShared& shared, const TERRAIN::DATA::NoiseLayerSettings& noise, std::string& error) {
    TerrainStore& store = TerrainStore::Get();
    auto generated = store.MakeGeneratedData(noise, error);
    if (generated == nullptr) {
        return false;
    }
    auto previous = store.ReplaceData(std::move(generated));
    if (shared.services != nullptr) {
        shared.services->RecordUndo("Generate Terrain", std::make_unique<ReplaceUndo>(std::move(previous)));
    }
    return true;
}

TERRAIN::DATA::NoiseLayerSettings DefaultNoise() {
    TERRAIN::DATA::NoiseLayerSettings noise{};
    noise.amplitudeMeters = 140.0f;
    noise.offsetMeters = 60.0f;
    noise.featureSizeMeters = 900.0f;
    noise.octaves = 6u;
    noise.fractal = TERRAIN::DATA::NoiseFractal::FBm;
    return noise;
}

// 無圧縮の PNG（deflate の格納ブロック）。出す先は Library の中なので大きさは問わない。
uint32_t Crc32(const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0u; i < 256u; ++i) {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) != 0u ? 0xEDB88320u ^ (value >> 1u) : value >> 1u;
            }
            values[i] = value;
        }
        return values;
    }();
    for (size_t index = 0u; index < size; ++index) {
        crc = table[(crc ^ data[index]) & 0xFFu] ^ (crc >> 8u);
    }
    return crc;
}

void PutU32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void PutChunk(std::vector<uint8_t>& out, const char type[4], const std::vector<uint8_t>& body) {
    PutU32(out, static_cast<uint32_t>(body.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), body.begin(), body.end());
    PutU32(out, Crc32(out.data() + start, out.size() - start) ^ 0xFFFFFFFFu);
}

bool WritePng(const std::filesystem::path& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgb) {
    std::vector<uint8_t> raw{};
    raw.reserve((static_cast<size_t>(width) * 3u + 1u) * height);
    for (uint32_t y = 0u; y < height; ++y) {
        raw.push_back(0u);
        const auto row = rgb.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(y) * width * 3u);
        raw.insert(raw.end(), row, row + static_cast<std::ptrdiff_t>(static_cast<size_t>(width) * 3u));
    }
    std::vector<uint8_t> zlib{0x78u, 0x01u};
    uint32_t adlerA = 1u;
    uint32_t adlerB = 0u;
    for (const uint8_t value : raw) {
        adlerA = (adlerA + value) % 65521u;
        adlerB = (adlerB + adlerA) % 65521u;
    }
    for (size_t offset = 0u; offset < raw.size();) {
        const size_t size = (std::min)(raw.size() - offset, size_t{65535u});
        zlib.push_back(offset + size == raw.size() ? 1u : 0u);
        zlib.push_back(static_cast<uint8_t>(size));
        zlib.push_back(static_cast<uint8_t>(size >> 8u));
        zlib.push_back(static_cast<uint8_t>(~size));
        zlib.push_back(static_cast<uint8_t>(~size >> 8u));
        zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                    raw.begin() + static_cast<std::ptrdiff_t>(offset + size));
        offset += size;
    }
    PutU32(zlib, (adlerB << 16u) | adlerA);
    std::vector<uint8_t> png{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};
    std::vector<uint8_t> header{};
    PutU32(header, width);
    PutU32(header, height);
    header.insert(header.end(), {8u, 2u, 0u, 0u, 0u});
    PutChunk(png, "IHDR", header);
    PutChunk(png, "IDAT", zlib);
    PutChunk(png, "IEND", {});
    std::error_code ec{};
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    return static_cast<bool>(
        file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size())));
}

class SculptTool final : public IEditorViewportTool {
public:
    explicit SculptTool(std::shared_ptr<EditorShared> shared) : shared_(std::move(shared)) {}
    const char* GetToolId() const noexcept override { return kSculptToolId; }
    const char* GetDisplayName() const noexcept override { return "Terrain Sculpt"; }
    void OnDeactivate() override { FinishStroke(); }

    void Update(const EditorViewportInput& input, IEditorViewportCanvas& canvas) override {
        TerrainStore& store = TerrainStore::Get();
        canvas.DrawHint("Terrain Sculpt: drag to apply, Shift inverts, Ctrl + wheel sets the radius", kBrushColor);
        if (!store.IsOpen()) {
            return;
        }
        HeightBrushSettings& brush = shared_->brush;
        if (input.control && input.wheel != 0.0f) {
            brush.radiusMeters = std::clamp(brush.radiusMeters * (input.wheel > 0.0f ? 1.15f : 0.87f), 1.0f, 2048.0f);
        }
        const float origin[3]{input.rayOrigin.x, input.rayOrigin.y, input.rayOrigin.z};
        const float direction[3]{input.rayDirection.x, input.rayDirection.y, input.rayDirection.z};
        float hit[3]{};
        const bool hasHit = input.pointerInViewport && store.Raycast(origin, direction, 8000.0f, hit);
        if (hasHit) {
            DrawBrushCircle(canvas, store, hit, brush.radiusMeters);
        }
        if (input.primaryPressed && hasHit && recorder_ == nullptr) {
            recorder_ = std::make_unique<TerrainEditRecorder>(store.Data());
            lastPoint_ = MATH::Vec2{hit[0], hit[2]};
            Apply(store, *lastPoint_, input.shift);
        } else if (recorder_ != nullptr && input.primaryDown && hasHit) {
            Apply(store, MATH::Vec2{hit[0], hit[2]}, input.shift);
        }
        if (recorder_ != nullptr && !input.primaryDown) {
            FinishStroke();
        }
    }

private:
    static void DrawBrushCircle(IEditorViewportCanvas& canvas, const TerrainStore& store, const float center[3],
                                float radius) {
        constexpr int kSegments = 72;
        GameProjectFloat3 previous{};
        for (int index = 0; index <= kSegments; ++index) {
            const float angle = 2.0f * std::numbers::pi_v<float> * static_cast<float>(index) / kSegments;
            const float x = center[0] + std::cos(angle) * radius;
            const float z = center[2] + std::sin(angle) * radius;
            const GameProjectFloat3 point{x, store.HeightAt(x, z).value_or(center[1]) + 0.2f, z};
            if (index > 0) {
                canvas.DrawLine(previous, point, kBrushColor, 2.0f);
            }
            previous = point;
        }
    }

    void Apply(TerrainStore& store, MATH::Vec2 point, bool invert) {
        HeightBrushSettings brush = shared_->brush;
        if (invert && brush.op == HeightBrushOp::Raise) {
            brush.op = HeightBrushOp::Lower;
        } else if (invert && brush.op == HeightBrushOp::Lower) {
            brush.op = HeightBrushOp::Raise;
        }
        const std::array<MATH::Vec2, 2> path{*lastPoint_, point};
        TERRAIN::DATA::BrushEditResult edit{};
        std::string error{};
        if (TERRAIN::DATA::ApplyHeightBrush(store.Data(), store.SculptLayerId(), brush, path, recorder_.get(), edit,
                                            error)) {
            store.RebakeRect(edit.touched);
        }
        lastPoint_ = point;
    }

    void FinishStroke() {
        if (recorder_ == nullptr) {
            return;
        }
        TerrainEdit edit = recorder_->Finish("Sculpt Terrain");
        recorder_.reset();
        lastPoint_.reset();
        if (!edit.Empty() && shared_->services != nullptr) {
            ++shared_->strokes;
            shared_->services->RecordUndo("Sculpt Terrain", std::make_unique<StrokeUndo>(std::move(edit)));
        }
    }

    std::shared_ptr<EditorShared> shared_;
    std::unique_ptr<TerrainEditRecorder> recorder_{};
    std::optional<MATH::Vec2> lastPoint_{};
};

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
        if (ui.Button("sculpt", "Sculpt Tool") && shared_->services != nullptr) {
            shared_->services->ActivateViewportTool(kSculptToolId);
        }
        HeightBrushSettings& brush = shared_->brush;
        for (const auto& entry : TERRAIN::DATA::kHeightBrushOpNames) {
            if (ui.Selectable(entry.name, entry.name, brush.op == entry.value)) {
                brush.op = entry.value;
            }
        }
        ui.InputFloat("radius", "Radius (m)", &brush.radiusMeters);
        ui.InputFloat("strength", "Strength", &brush.strength);
        ui.InputFloat("hardness", "Hardness", &brush.hardness);
        if (brush.op == HeightBrushOp::Flatten) {
            ui.InputFloat("target", "Target Height (m)", &brush.targetHeightMeters);
        }
        ui.Separator();
        ui.InputFloat("amplitude", "Noise Amplitude (m)", &noise_.amplitudeMeters);
        ui.InputFloat("feature", "Noise Feature Size (m)", &noise_.featureSizeMeters);
        if (ui.Button("generate", "Regenerate Terrain")) {
            std::string error{};
            if (!RegenerateTerrain(*shared_, noise_, error) && shared_->commands != nullptr) {
                shared_->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, error);
            }
        }
    }

private:
    std::shared_ptr<EditorShared> shared_;
    TERRAIN::DATA::NoiseLayerSettings noise_ = DefaultNoise();
};

// 操作の手続きの共通部分：JSON を読み、例外を結果に変える。
template <class Body>
class JsonOperation final : public IEditorOperationHandler {
public:
    explicit JsonOperation(Body body) : body_(std::move(body)) {}
    void Execute(std::string_view parametersJson, IEditorOperationResultSink& result) override {
        try {
            const nlohmann::json parameters =
                parametersJson.empty() ? nlohmann::json::object() : nlohmann::json::parse(parametersJson);
            body_(parameters, result);
        } catch (const std::exception& exception) {
            ReplyFailure(result, "INVALID_PARAMETERS", exception.what());
        }
    }

private:
    Body body_;
};

template <class Body>
std::unique_ptr<IEditorOperationHandler> MakeOperation(Body body) {
    return std::make_unique<JsonOperation<Body>>(std::move(body));
}

void DescribeTerrain(const nlohmann::json&, IEditorOperationResultSink& result) {
    const TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        ReplyFailure(result, "NO_TERRAIN", "No terrain is open. Add the Terrain component to an object.");
        return;
    }
    const auto& grid = store.Settings().grid;
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (const auto& [tile, runtime] : store.Tiles()) {
        minimum = (std::min)(minimum, runtime.minHeight);
        maximum = (std::max)(maximum, runtime.maxHeight);
    }
    nlohmann::json layers = nlohmann::json::array();
    for (const auto& layer : store.Data().Stack().layers) {
        layers.push_back({{"id", layer.id}, {"name", layer.name}});
    }
    const nlohmann::json data{{"scene", store.ScenePath().generic_string()},
                              {"tileSizeMeters", grid.tileSizeMeters},
                              {"tileCells", grid.tileCells},
                              {"tiles", store.Tiles().size()},
                              {"tilesX", store.Settings().tilesX},
                              {"tilesZ", store.Settings().tilesZ},
                              {"firstTileX", store.Settings().firstTileX},
                              {"firstTileZ", store.Settings().firstTileZ},
                              {"minHeight", minimum},
                              {"maxHeight", maximum},
                              {"layers", std::move(layers)},
                              {"unsaved", store.HasUnsavedTiles()}};
    result.Receive(true, "OK", "Terrain description.", data.dump());
}

void HeightAt(const nlohmann::json& parameters, IEditorOperationResultSink& result) {
    const auto height = TerrainStore::Get().HeightAt(parameters.at("x").get<float>(), parameters.at("z").get<float>());
    if (!height.has_value()) {
        ReplyFailure(result, "NO_TILE", "No terrain tile at that position.");
        return;
    }
    result.Receive(true, "OK", "Height.", nlohmann::json{{"height", *height}}.dump());
}

// 俯視図：高さから陰影を付けた絵を PNG で書く（人が見る。智能体は describe の数字と合わせて読む）。
void ExportOverview(const nlohmann::json& parameters, IEditorOperationResultSink& result) {
    const TerrainStore& store = TerrainStore::Get();
    if (!store.IsOpen()) {
        ReplyFailure(result, "NO_TERRAIN", "No terrain is open.");
        return;
    }
    const uint32_t size = std::clamp(parameters.value("size", 1024u), 16u, 8192u);
    const auto& settings = store.Settings();
    const float tileSize = settings.grid.tileSizeMeters;
    const float minX = static_cast<float>(settings.firstTileX) * tileSize;
    const float minZ = static_cast<float>(settings.firstTileZ) * tileSize;
    const float extentX = static_cast<float>(settings.tilesX) * tileSize;
    const float extentZ = static_cast<float>(settings.tilesZ) * tileSize;
    const float extent = (std::max)(extentX, extentZ);
    const float step = extent / static_cast<float>(size);
    std::vector<uint8_t> rgb(static_cast<size_t>(size) * size * 3u);
    const auto sample = [&](float x, float z) { return store.HeightAt(x, z).value_or(0.0f); };
    for (uint32_t row = 0u; row < size; ++row) {
        // 上が +Z になるよう、画像の行を反転する。
        const float z = minZ + extent - (static_cast<float>(row) + 0.5f) * step;
        for (uint32_t column = 0u; column < size; ++column) {
            const float x = minX + (static_cast<float>(column) + 0.5f) * step;
            const float height = sample(x, z);
            const float dx = (sample(x + step, z) - sample(x - step, z)) / (2.0f * step);
            const float dz = (sample(x, z + step) - sample(x, z - step)) / (2.0f * step);
            const float inverse = 1.0f / std::sqrt(dx * dx + dz * dz + 1.0f);
            const float shade = std::clamp((0.35f * -dx + 0.8f + 0.45f * -dz) * inverse, 0.15f, 1.0f);
            const float slope = 1.0f - inverse;
            const float rock = std::clamp((slope - 0.14f) / 0.18f, 0.0f, 1.0f);
            const float snow = std::clamp((height - 170.0f) / 40.0f, 0.0f, 1.0f) * (1.0f - rock);
            const std::array<float, 3> color{
                (0.30f + 0.20f * rock + 0.45f * snow) * shade,
                (0.40f + 0.05f * rock + 0.42f * snow) * shade,
                (0.20f + 0.20f * rock + 0.60f * snow) * shade,
            };
            uint8_t* pixel = rgb.data() + (static_cast<size_t>(row) * size + column) * 3u;
            for (int channel = 0; channel < 3; ++channel) {
                pixel[channel] = static_cast<uint8_t>(std::clamp(color[channel], 0.0f, 1.0f) * 255.0f);
            }
        }
    }
    // 場面は <root>/Assets/... にある。出力は <root>/Library/TerrainKit へ置く。
    std::filesystem::path root = store.ScenePath().parent_path();
    while (!root.empty() && root.filename() != "Assets" && root != root.root_path()) {
        root = root.parent_path();
    }
    const std::filesystem::path output = root.parent_path() / "Library" / "TerrainKit" / "overview.png";
    if (!WritePng(output, size, size, rgb)) {
        ReplyFailure(result, "WRITE_FAILED", "Could not write " + output.generic_string());
        return;
    }
    result.Receive(true, "OK", "Overview written.",
                   nlohmann::json{{"path", output.generic_string()}, {"size", size}, {"metersPerPixel", step}}.dump());
}

class TerrainKitEditorModule final : public IGameProjectEditorModule, public IEditorSceneListener {
public:
    std::string_view GetModuleId() const override { return "HIKARI.TerrainKit"; }
    std::vector<std::unique_ptr<IEditorWorkspace>> CreateEditorWorkspaces() const override { return {}; }

    std::vector<std::unique_ptr<IEditorViewportTool>> CreateViewportTools() const override {
        std::vector<std::unique_ptr<IEditorViewportTool>> tools{};
        tools.push_back(std::make_unique<SculptTool>(shared_));
        return tools;
    }
    std::vector<std::unique_ptr<IEditorPanel>> CreateEditorPanels() const override {
        std::vector<std::unique_ptr<IEditorPanel>> panels{};
        panels.push_back(std::make_unique<TerrainPanel>(shared_));
        return panels;
    }

    void OnSceneSaved(std::string_view scenePathUtf8) override {
        std::string error{};
        const std::filesystem::path path(
            std::u8string_view(reinterpret_cast<const char8_t*>(scenePathUtf8.data()), scenePathUtf8.size()));
        if (TerrainStore::Get().IsOpen() && !TerrainStore::Get().Save(path, error) && shared_->commands != nullptr) {
            shared_->commands->PublishStatus(EditorWorkspaceStatusSeverity::Error, error);
        }
    }

    void RegisterEditorOperations(IEditorOperationRegistrar& registrar) override {
        shared_->services = registrar.SceneServices();
        shared_->commands = registrar.Commands();
        if (shared_->services != nullptr) {
            shared_->services->SetSceneListener(this);
        }
        using Type = EditorOperationParameterType;
        Register(registrar, "terrainkit.describe", "Describe the open terrain: tiles, height range and layers.",
                 EditorOperationKind::Query, EditorOperationPermission::ReadEditorState, {}, DescribeTerrain);
        Register(registrar, "terrainkit.generate",
                 "Replace the terrain with a noise layer and an empty sculpt layer. One undo step.",
                 EditorOperationKind::Mutation, EditorOperationPermission::ModifyEditorState,
                 {{"amplitudeMeters", Type::Number, "Height of noise value 1."},
                  {"offsetMeters", Type::Number, "Height added to the noise."},
                  {"featureSizeMeters", Type::Number, "World size of the largest noise feature."},
                  {"octaves", Type::Integer, "Noise octaves."},
                  {"seed", Type::Integer, "Noise seed."}},
                 [shared = shared_](const nlohmann::json& parameters, IEditorOperationResultSink& result) {
                     TERRAIN::DATA::NoiseLayerSettings noise = DefaultNoise();
                     noise.amplitudeMeters = parameters.value("amplitudeMeters", noise.amplitudeMeters);
                     noise.offsetMeters = parameters.value("offsetMeters", noise.offsetMeters);
                     noise.featureSizeMeters = parameters.value("featureSizeMeters", noise.featureSizeMeters);
                     noise.octaves = parameters.value("octaves", noise.octaves);
                     noise.seed = parameters.value("seed", noise.seed);
                     std::string error{};
                     if (!RegenerateTerrain(*shared, noise, error)) {
                         ReplyFailure(result, "GENERATE_FAILED", error);
                         return;
                     }
                     result.Receive(true, "OK", "Terrain generated.",
                                    nlohmann::json{{"tiles", TerrainStore::Get().Tiles().size()}}.dump());
                 });
        Register(registrar, "terrainkit.stroke",
                 "Apply a height brush along a world-space path, the same way the Sculpt tool does. One undo step.",
                 EditorOperationKind::Mutation, EditorOperationPermission::ModifyEditorState,
                 {{"op", Type::String, "raise, lower, smooth, flatten or noise."},
                  {"radiusMeters", Type::Number, "Brush radius."},
                  {"strength", Type::Number, "Meters per dab, or a 0..1 fraction for smooth and flatten."},
                  {"hardness", Type::Number, "Fraction of the radius at full weight."},
                  {"targetHeightMeters", Type::Number, "Target height for flatten."},
                  {"points", Type::Json, "World-space path [[x,z],...]."}},
                 [shared = shared_](const nlohmann::json& parameters, IEditorOperationResultSink& result) {
                     TerrainStore& store = TerrainStore::Get();
                     if (!store.IsOpen() || store.SculptLayerId() == 0u) {
                         ReplyFailure(result, "NO_TERRAIN", "No terrain with a sculpt layer is open.");
                         return;
                     }
                     HeightBrushSettings brush = shared->brush;
                     if (!ReadBrushOp(parameters.value("op", std::string("raise")), brush.op)) {
                         ReplyFailure(result, "UNKNOWN_OP", "op must be raise, lower, smooth, flatten or noise.");
                         return;
                     }
                     brush.radiusMeters = parameters.value("radiusMeters", brush.radiusMeters);
                     brush.strength = parameters.value("strength", brush.strength);
                     brush.hardness = parameters.value("hardness", brush.hardness);
                     brush.targetHeightMeters = parameters.value("targetHeightMeters", brush.targetHeightMeters);
                     std::vector<MATH::Vec2> path{};
                     for (const auto& point : parameters.at("points")) {
                         path.push_back({point.at(0).get<float>(), point.at(1).get<float>()});
                     }
                     const auto started = std::chrono::steady_clock::now();
                     TerrainEditRecorder recorder(store.Data());
                     TERRAIN::DATA::BrushEditResult edit{};
                     std::string error{};
                     if (!TERRAIN::DATA::ApplyHeightBrush(store.Data(), store.SculptLayerId(), brush, path, &recorder,
                                                          edit, error)) {
                         ReplyFailure(result, "STROKE_FAILED", error);
                         return;
                     }
                     store.RebakeRect(edit.touched);
                     TerrainEdit undo = recorder.Finish("Sculpt Terrain");
                     const size_t changedTiles = undo.ChangedTiles().size();
                     if (shared->services != nullptr && !undo.Empty()) {
                         shared->services->RecordUndo("Sculpt Terrain", std::make_unique<StrokeUndo>(std::move(undo)));
                     }
                     const double milliseconds =
                         std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                     result.Receive(true, "OK", "Stroke applied.",
                                    nlohmann::json{{"dabs", edit.dabs},
                                                   {"changedTiles", changedTiles},
                                                   {"milliseconds", milliseconds}}
                                        .dump());
                 });
        Register(registrar, "terrainkit.height_at", "Read the terrain height at x, z.", EditorOperationKind::Query,
                 EditorOperationPermission::ReadEditorState,
                 {{"x", Type::Number, "World X."}, {"z", Type::Number, "World Z."}}, HeightAt);
        Register(registrar, "terrainkit.export_overview",
                 "Write a shaded top-down picture of the whole terrain to Library/TerrainKit/overview.png.",
                 EditorOperationKind::Query, EditorOperationPermission::ReadEditorState,
                 {{"size", Type::Integer, "Picture width and height in pixels."}}, ExportOverview);
    }

private:
    struct Parameter {
        const char* name = nullptr;
        EditorOperationParameterType type = EditorOperationParameterType::Unspecified;
        const char* description = nullptr;
    };

    template <class Body>
    void Register(IEditorOperationRegistrar& registrar, const char* id, const char* summary, EditorOperationKind kind,
                  EditorOperationPermission permission, std::initializer_list<Parameter> parameters, Body body) {
        handlers_.push_back(MakeOperation(std::move(body)));
        std::vector<EditorOperationParameterDescriptor> descriptors{};
        for (const Parameter& parameter : parameters) {
            EditorOperationParameterDescriptor entry{};
            entry.name = parameter.name;
            entry.type = parameter.type;
            entry.description = parameter.description;
            descriptors.push_back(entry);
        }
        EditorOperationDescriptor descriptor{};
        descriptor.id = id;
        descriptor.summary = summary;
        descriptor.workspace = "general";
        descriptor.kind = kind;
        descriptor.permission = permission;
        descriptor.exposureMask = EditorOperationExposureMask::Human | EditorOperationExposureMask::Agent;
        descriptor.handler = handlers_.back().get();
        descriptor.parameters = descriptors.data();
        descriptor.parameterCount = descriptors.size();
        (void)registrar.RegisterOperation(descriptor);
    }

    std::shared_ptr<EditorShared> shared_ = std::make_shared<EditorShared>();
    std::vector<std::unique_ptr<IEditorOperationHandler>> handlers_{};
};

} // namespace
} // namespace HIKARI::TERRAINKIT

namespace HIKARI::EDITOR_PROJECT {
std::unique_ptr<IGameProjectEditorModule> CreateGameProjectEditorModule() {
    return std::make_unique<::HIKARI::TERRAINKIT::TerrainKitEditorModule>();
}
} // namespace HIKARI::EDITOR_PROJECT
