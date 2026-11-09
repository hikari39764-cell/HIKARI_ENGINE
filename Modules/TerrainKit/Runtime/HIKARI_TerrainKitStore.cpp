#include "HIKARI_TerrainKitStore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <execution>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>

#include <json.hpp>

#include <windows.h>

#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"
#include "TerrainKit/Data/HIKARI_TerrainDataFile.h"
#include "TerrainKit/Data/HIKARI_TerrainDataLayers.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace TERRAIN::DATA;

constexpr uint32_t kNormalMipCount = 5u;
constexpr char kLayerFileName[] = "terrain.layers.json";
constexpr char kSurfaceSlotFileName[] = "terrain.surfaces.json";

// 法線の xz を 16 bit で持つ（y は着色器で復元する）。
int16_t ToSnorm16(float value) noexcept {
    return static_cast<int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
}

void BuildNormalMips(const std::vector<float>& bordered, uint32_t samples, double spacing, std::vector<int16_t>& out) {
    // bordered は周囲 1 標本を足した (samples + 2)²。
    const uint32_t stride = samples + 2u;
    size_t total = 0u;
    for (uint32_t mip = 0u; mip < kNormalMipCount; ++mip) {
        const uint32_t size = (std::max)(samples >> mip, 1u);
        total += static_cast<size_t>(size) * size * 2u;
    }
    out.resize(total);
    std::vector<float> level(static_cast<size_t>(samples) * samples * 2u);
    const float scale = 0.5f / static_cast<float>(spacing);
    for (uint32_t z = 0u; z < samples; ++z) {
        for (uint32_t x = 0u; x < samples; ++x) {
            const size_t center = static_cast<size_t>(z + 1u) * stride + x + 1u;
            const float dx = (bordered[center + 1u] - bordered[center - 1u]) * scale;
            const float dz = (bordered[center + stride] - bordered[center - stride]) * scale;
            const float inverseLength = 1.0f / std::sqrt(dx * dx + dz * dz + 1.0f);
            const size_t index = (static_cast<size_t>(z) * samples + x) * 2u;
            level[index] = -dx * inverseLength;
            level[index + 1u] = -dz * inverseLength;
        }
    }
    size_t offset = 0u;
    uint32_t size = samples;
    for (uint32_t mip = 0u; mip < kNormalMipCount; ++mip) {
        for (size_t index = 0u; index < static_cast<size_t>(size) * size * 2u; ++index) {
            out[offset + index] = ToSnorm16(level[index]);
        }
        offset += static_cast<size_t>(size) * size * 2u;
        if (mip + 1u == kNormalMipCount) {
            break;
        }
        const uint32_t next = (std::max)(size >> 1u, 1u);
        std::vector<float> reduced(static_cast<size_t>(next) * next * 2u);
        for (uint32_t z = 0u; z < next; ++z) {
            for (uint32_t x = 0u; x < next; ++x) {
                for (uint32_t c = 0u; c < 2u; ++c) {
                    const auto at = [&](uint32_t sx, uint32_t sz) {
                        return level[(static_cast<size_t>(sz) * size + sx) * 2u + c];
                    };
                    reduced[(static_cast<size_t>(z) * next + x) * 2u + c] =
                        0.25f * (at(x * 2u, z * 2u) + at(x * 2u + 1u, z * 2u) + at(x * 2u, z * 2u + 1u) +
                                 at(x * 2u + 1u, z * 2u + 1u));
                }
            }
        }
        level = std::move(reduced);
        size = next;
    }
}

bool MakeInitialStack(TerrainData& data, const NoiseLayerSettings& noise, uint32_t& outSculptLayer) {
    HeightLayer base{};
    base.name = "Base Noise";
    base.kind = HeightLayerKind::Noise;
    base.noise = noise;
    if (data.AddLayer(std::move(base)) == 0u) {
        return false;
    }
    HeightLayer sculpt{};
    sculpt.name = "Sculpt";
    sculpt.kind = HeightLayerKind::Sculpt;
    outSculptLayer = data.AddLayer(std::move(sculpt));
    return outSculptLayer != 0u;
}

uint32_t FindSculptLayer(const TerrainData& data) noexcept {
    for (auto layer = data.Stack().layers.rbegin(); layer != data.Stack().layers.rend(); ++layer) {
        if (layer->kind == HeightLayerKind::Sculpt) {
            return layer->id;
        }
    }
    return 0u;
}

// 引擎の JSON 道具には繋がないので、ここで読み書きする。
bool ReadJson(const std::filesystem::path& path, nlohmann::json& out, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "Could not open " + path.generic_string();
        return false;
    }
    out = nlohmann::json::parse(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), nullptr, false);
    if (out.is_discarded()) {
        error = "Invalid JSON in " + path.generic_string();
        return false;
    }
    return true;
}

bool WriteJson(const std::filesystem::path& path, const nlohmann::json& document, std::string& error) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!(file << document.dump(2))) {
        error = "Could not write " + path.generic_string();
        return false;
    }
    return true;
}

bool ValidSettings(const TerrainSettings& settings, std::string* error) {
    if (!ValidateTerrainGrid(settings.grid, error)) {
        return false;
    }
    if (settings.tilesX <= 0 || settings.tilesZ <= 0 || settings.tilesX > 256 || settings.tilesZ > 256) {
        if (error != nullptr) {
            *error = "Terrain tile counts must be between 1 and 256.";
        }
        return false;
    }
    return true;
}

} // namespace

std::filesystem::path TerrainModuleDirectory() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&TerrainModuleDirectory), &module);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

std::filesystem::path TerrainCellDirectory(const std::filesystem::path& scenePath) {
    // 引擎の格子の置き場と同じ規則（Main.scene.json -> Main.cells）。
    std::u8string stem = scenePath.filename().u8string();
    constexpr std::u8string_view sceneJson = u8".scene.json";
    if (stem.ends_with(sceneJson)) {
        stem.resize(stem.size() - sceneJson.size());
    } else {
        stem = scenePath.stem().u8string();
    }
    return scenePath.parent_path() / std::filesystem::path(stem + u8".cells");
}

TerrainStore& TerrainStore::Get() {
    static TerrainStore store{};
    return store;
}

void TerrainStore::Close() {
    ruleBaker_.Cancel();
    rivers_.Invalidate();
    scenePath_.clear();
    data_.reset();
    sculptLayer_ = 0u;
    tiles_.clear();
    unsavedTiles_.clear();
    stackDirty_ = false;
    ++layoutRevision_;
}

bool TerrainStore::Open(const std::filesystem::path& scenePath, const TerrainSettings& settings, std::string* error) {
    if (data_ != nullptr && scenePath_ == scenePath && settings_ == settings) {
        return true;
    }
    if (!ValidSettings(settings, error)) {
        return false;
    }
    const bool sameScene = data_ != nullptr && scenePath_ == scenePath;
    if (sameScene && settings_.grid == settings.grid) {
        // 範囲だけの変更は編集データを残す。
        settings_ = settings;
        BuildTileList();
        return true;
    }
    Close();
    scenePath_ = scenePath;
    settings_ = settings;
    data_ = std::make_unique<TerrainData>(settings.grid);
    if (!LoadFromDisk(scenePath, error)) {
        return false;
    }
    if (data_->Stack().layers.empty()) {
        NoiseLayerSettings flat{};
        flat.amplitudeMeters = 0.0f;
        if (!MakeInitialStack(*data_, flat, sculptLayer_)) {
            return false;
        }
    }
    // 枠の名前。無ければ内蔵の材質を順に割り当てる（枠 0 が地形全体の既定）。
    std::vector<std::string> slots{};
    nlohmann::json slotFile{};
    std::string slotMessage{};
    const std::filesystem::path slotPath = TerrainCellDirectory(scenePath) / kSurfaceSlotFileName;
    std::error_code slotError{};
    surfaceRules_ = DefaultSurfaceRules();
    weather_ = {};
    if (std::filesystem::exists(slotPath, slotError) && ReadJson(slotPath, slotFile, slotMessage)) {
        slots = slotFile.value("slots", std::vector<std::string>{});
        if (slotFile.contains("rules")) {
            SurfaceRuleSet rules{};
            JSONFIELDS::Read(slotFile, rules, kSurfaceRuleSetFields);
            surfaceRules_ = std::move(rules);
        }
        weather_.snowLineMeters = slotFile.value("snowLineMeters", weather_.snowLineMeters);
        weather_.snowAmount = slotFile.value("snowAmount", weather_.snowAmount);
        weather_.wetness = slotFile.value("wetness", weather_.wetness);
    }
    if (slots.empty()) {
        slots = {"Grass", "Soil", "Rock", "Sand", "Snow"};
    }
    surfaces_.SetSlots(ProjectRoot(), std::move(slots));
    BuildTileList();
    return true;
}

bool TerrainStore::LoadFromDisk(const std::filesystem::path& scenePath, std::string* error) {
    const std::filesystem::path directory = TerrainCellDirectory(scenePath);
    const std::filesystem::path layerFile = directory / kLayerFileName;
    std::error_code ec{};
    if (!std::filesystem::exists(layerFile, ec)) {
        return true;
    }
    nlohmann::json root{};
    std::string message{};
    if (!ReadJson(layerFile, root, message)) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    }
    HeightLayerStack stack{};
    if (!ReadHeightLayerStackJson(root, stack, message) || !data_->ReplaceStack(stack, &message)) {
        if (error != nullptr) {
            *error = "Terrain layer file is invalid: " + message;
        }
        return false;
    }
    settings_.grid = stack.grid;
    sculptLayer_ = FindSculptLayer(*data_);
    LoadLayerImages();
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        const std::string name = entry.path().filename().string();
        constexpr std::string_view suffix = ".terrain.bin";
        if (name.size() <= suffix.size() || name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        int tileX = 0;
        int tileZ = 0;
        if (std::sscanf(name.c_str(), "x%d_z%d", &tileX, &tileZ) != 2) {
            continue;
        }
        std::vector<uint8_t> bytes{};
        if (!LoadTerrainCellFile(entry.path(), bytes, message) ||
            !ReadTerrainCell(*data_, TileCoord{tileX, tileZ}, bytes, message)) {
            if (error != nullptr) {
                *error = name + ": " + message;
            }
            return false;
        }
    }
    return true;
}

std::unique_ptr<TerrainData> TerrainStore::MakeGeneratedData(const NoiseLayerSettings& noise,
                                                             std::string& error) const {
    auto data = std::make_unique<TerrainData>(settings_.grid);
    uint32_t sculptLayer = 0u;
    if (!MakeInitialStack(*data, noise, sculptLayer)) {
        error = "Could not create the terrain layers.";
        return nullptr;
    }
    return data;
}

std::unique_ptr<TerrainData> TerrainStore::ReplaceData(std::unique_ptr<TerrainData> data) {
    ruleBaker_.Cancel();
    rivers_.Invalidate();
    std::unique_ptr<TerrainData> previous = std::move(data_);
    data_ = std::move(data);
    sculptLayer_ = FindSculptLayer(*data_);
    stackDirty_ = true;
    std::vector<TileCoord> all{};
    for (const auto& [tile, runtime] : tiles_) {
        all.push_back(tile);
    }
    RebakeTiles(all);
    MarkSaveDirty(all);
    return previous;
}

void TerrainStore::BuildTileList() {
    std::map<TileCoord, TileRuntime> next{};
    std::vector<TileCoord> missing{};
    uint32_t layer = 0u;
    for (int32_t z = 0; z < settings_.tilesZ; ++z) {
        for (int32_t x = 0; x < settings_.tilesX; ++x) {
            const TileCoord tile{settings_.firstTileX + x, settings_.firstTileZ + z};
            TileRuntime runtime{};
            runtime.layer = layer++;
            next.emplace(tile, std::move(runtime));
            missing.push_back(tile);
        }
    }
    tiles_ = std::move(next);
    ++layoutRevision_;
    RebakeTiles(missing);
}

void TerrainStore::BakeTile(TileCoord tile, const HeightStackEvaluator& evaluator) {
    const auto found = tiles_.find(tile);
    if (found == tiles_.end()) {
        return;
    }
    TileRuntime& runtime = found->second;
    const TerrainGrid& grid = settings_.grid;
    const uint32_t samples = TileSamples(grid);
    SampleRect rect = TileSampleRect(grid, tile);
    rect.x0 -= 1;
    rect.z0 -= 1;
    rect.width += 2u;
    rect.height += 2u;
    std::vector<float> bordered{};
    if (!evaluator.EvaluateRect(rect, bordered)) {
        bordered.assign(rect.Count(), 0.0f);
    }
    const uint32_t stride = samples + 2u;
    runtime.heights.resize(static_cast<size_t>(samples) * samples);
    runtime.minHeight = std::numeric_limits<float>::max();
    runtime.maxHeight = std::numeric_limits<float>::lowest();
    runtime.leafMinMax.assign(static_cast<size_t>(kTileLeafDivisions) * kTileLeafDivisions * 2u, 0.0f);
    for (size_t leaf = 0u; leaf < static_cast<size_t>(kTileLeafDivisions) * kTileLeafDivisions; ++leaf) {
        runtime.leafMinMax[leaf * 2u] = std::numeric_limits<float>::max();
        runtime.leafMinMax[leaf * 2u + 1u] = std::numeric_limits<float>::lowest();
    }
    const uint32_t cells = grid.tileCells;
    for (uint32_t z = 0u; z < samples; ++z) {
        for (uint32_t x = 0u; x < samples; ++x) {
            const float height = bordered[static_cast<size_t>(z + 1u) * stride + x + 1u];
            runtime.heights[static_cast<size_t>(z) * samples + x] = QuantizeHeight(grid, height);
            runtime.minHeight = (std::min)(runtime.minHeight, height);
            runtime.maxHeight = (std::max)(runtime.maxHeight, height);
            // 葉の境界の標本は両方の葉に数える。
            const uint32_t leafX0 =
                (std::min)((x == 0u ? 0u : x - 1u) * kTileLeafDivisions / cells, kTileLeafDivisions - 1u);
            const uint32_t leafX1 = (std::min)(x * kTileLeafDivisions / cells, kTileLeafDivisions - 1u);
            const uint32_t leafZ0 =
                (std::min)((z == 0u ? 0u : z - 1u) * kTileLeafDivisions / cells, kTileLeafDivisions - 1u);
            const uint32_t leafZ1 = (std::min)(z * kTileLeafDivisions / cells, kTileLeafDivisions - 1u);
            for (uint32_t leafZ = leafZ0; leafZ <= leafZ1; ++leafZ) {
                for (uint32_t leafX = leafX0; leafX <= leafX1; ++leafX) {
                    const size_t index = (static_cast<size_t>(leafZ) * kTileLeafDivisions + leafX) * 2u;
                    runtime.leafMinMax[index] = (std::min)(runtime.leafMinMax[index], height);
                    runtime.leafMinMax[index + 1u] = (std::max)(runtime.leafMinMax[index + 1u], height);
                }
            }
        }
    }
    BuildNormalMips(bordered, samples, SampleSpacing(grid), runtime.normalMips);
    runtime.uploadPending = true;
    ++runtime.revision;
}

void TerrainStore::RebakeTiles(std::span<const TileCoord> tiles) {
    if (data_ == nullptr || tiles.empty()) {
        return;
    }
    const HeightStackEvaluator evaluator(*data_);
    if (!evaluator.Valid()) {
        return;
    }
    std::for_each(std::execution::par, tiles.begin(), tiles.end(),
                  [&](const TileCoord& tile) { BakeTile(tile, evaluator); });
    for (const TileCoord tile : tiles) {
        ruleBaker_.MarkDirty(tile);
    }
}

void TerrainStore::RebakeRect(const SampleRect& rect) {
    if (data_ == nullptr || rect.Empty()) {
        return;
    }
    std::vector<TileCoord> touched{};
    // 法線は周囲 1 標本を見るので、1 標本広げた範囲を焼く。
    SampleRect wider = rect;
    wider.x0 -= 1;
    wider.z0 -= 1;
    wider.width += 2u;
    wider.height += 2u;
    ForEachTileHolding(settings_.grid, wider, [&](TileCoord tile) {
        if (tiles_.contains(tile)) {
            touched.push_back(tile);
        }
    });
    const HeightStackEvaluator evaluator(*data_);
    if (!evaluator.Valid()) {
        return;
    }
    std::for_each(std::execution::par, touched.begin(), touched.end(), [&](const TileCoord& tile) {
        const SampleRect area = IntersectRects(TileSampleRect(settings_.grid, tile), wider);
        if (!area.Empty()) {
            BakeTileRect(tile, area, evaluator);
        }
    });
    MarkSaveDirty(touched);
    for (const TileCoord tile : touched) {
        ruleBaker_.MarkDirty(tile);
    }
}

void TerrainStore::RebakeAfterSplineChange(const SampleRect& rect) {
    stackDirty_ = true;
    RebakeRect(rect);
}

void TerrainStore::MarkControlChanged(const SampleRect& rect) {
    std::vector<TileCoord> touched{};
    ForEachTileHolding(settings_.grid, rect, [&](TileCoord tile) {
        const auto found = tiles_.find(tile);
        if (found == tiles_.end()) {
            return;
        }
        touched.push_back(tile);
        const SampleRect tileRect = TileSampleRect(settings_.grid, tile);
        const SampleRect area = IntersectRects(tileRect, rect);
        if (area.Empty()) {
            return;
        }
        TileRuntime& runtime = found->second;
        const auto x0 = static_cast<uint32_t>(area.x0 - tileRect.x0);
        const auto z0 = static_cast<uint32_t>(area.z0 - tileRect.z0);
        const uint32_t x1 = x0 + area.width - 1u;
        const uint32_t z1 = z0 + area.height - 1u;
        runtime.controlX0 = runtime.controlRectPending ? (std::min)(runtime.controlX0, x0) : x0;
        runtime.controlZ0 = runtime.controlRectPending ? (std::min)(runtime.controlZ0, z0) : z0;
        runtime.controlX1 = runtime.controlRectPending ? (std::max)(runtime.controlX1, x1) : x1;
        runtime.controlZ1 = runtime.controlRectPending ? (std::max)(runtime.controlZ1, z1) : z1;
        runtime.controlRectPending = true;
        ++runtime.revision; // 光線追跡の代理の色も変わる
    });
    MarkSaveDirty(touched);
    // 手塗りを外した標本は規則へ戻り、遠景の色も変わる。
    for (const TileCoord tile : touched) {
        ruleBaker_.MarkDirty(tile);
    }
}

void TerrainStore::SetSurfaceRules(SurfaceRuleSet rules) {
    surfaceRules_ = std::move(rules);
    surfaceSlotsDirty_ = true;
    ruleBaker_.MarkAllDirty(*this);
}

void TerrainStore::SetWeather(const TerrainWeather& weather) {
    const auto unit = [](float value, float fallback) {
        return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : fallback;
    };
    weather_.snowLineMeters = std::isfinite(weather.snowLineMeters) ? weather.snowLineMeters : weather_.snowLineMeters;
    weather_.snowAmount = unit(weather.snowAmount, weather_.snowAmount);
    weather_.wetness = unit(weather.wetness, weather_.wetness);
    surfaceSlotsDirty_ = true;
}

void TerrainStore::ShutdownWorkers() {
    surfaces_.Shutdown();
    ruleBaker_.Cancel();
}

void TerrainStore::ApplyRuleResult(TileCoord tile, std::vector<uint32_t> control, std::vector<uint8_t> farColor) {
    const auto found = tiles_.find(tile);
    if (found == tiles_.end() || data_ == nullptr) {
        return;
    }
    TileRuntime& runtime = found->second;
    const uint32_t samples = TileSamples(settings_.grid);
    if (control.size() == static_cast<size_t>(samples) * samples) {
        // 計算の間の編集を消さない：手塗りと穴・ナビの印は今の値を取る。
        TiledRaster<uint32_t>& raster = data_->Control();
        const TileRaster<uint32_t>* current = raster.Find(tile);
        for (size_t index = 0u; index < control.size(); ++index) {
            control[index] = MergeComputedControl(control[index], current != nullptr ? current->values[index] : 0u);
        }
        // 全部既定のままの瓦片は図を持たない。
        const bool stored = current != nullptr ? current->values != control
                                               : std::any_of(control.begin(), control.end(),
                                                             [](uint32_t value) { return value != 0u; });
        if (stored) {
            TileRaster<uint32_t> next(samples, 0u);
            next.values = std::move(control);
            raster.Assign(tile, std::move(next));
            data_->MarkChanged();
            MarkSaveDirty(std::span(&tile, 1u));
            runtime.controlX0 = 0u;
            runtime.controlZ0 = 0u;
            runtime.controlX1 = samples - 1u;
            runtime.controlZ1 = samples - 1u;
            runtime.controlRectPending = true;
            ++runtime.revision; // 光線追跡の代理の色も変わる
        }
    }
    runtime.farColor = std::move(farColor);
    runtime.farPending = true;
}

void TerrainStore::SetSurfaceSlots(std::vector<std::string> names) {
    surfaceSlotsDirty_ = true;
    surfaces_.SetSlots(ProjectRoot(), std::move(names));
}

void TerrainStore::RebakeAfterStackChange() {
    if (data_ == nullptr) {
        return;
    }
    LoadLayerImages();
    sculptLayer_ = FindSculptLayer(*data_);
    stackDirty_ = true;
    std::vector<TileCoord> all{};
    for (const auto& [tile, runtime] : tiles_) {
        all.push_back(tile);
    }
    RebakeTiles(all);
    MarkSaveDirty(all);
}

std::filesystem::path TerrainStore::ProjectRoot() const {
    // 場面は <root>/Assets/... にある。
    std::filesystem::path current = scenePath_.parent_path();
    while (!current.empty() && current.filename() != "Assets" && current != current.root_path()) {
        current = current.parent_path();
    }
    return current.filename() == "Assets" ? current.parent_path() : scenePath_.parent_path();
}

void TerrainStore::LoadLayerImages(std::string* error) {
    if (data_ == nullptr) {
        return;
    }
    const std::filesystem::path root = ProjectRoot();
    for (const HeightLayer& layer : data_->Stack().layers) {
        if (layer.kind != HeightLayerKind::Image || layer.image.source.empty()) {
            continue;
        }
        // 撤销や底図の差し替えで参照先が変わったら読み直す。
        const std::string loadedKey = layer.image.source + "|" + std::to_string(layer.image.rawWidth);
        std::string& loaded = loadedImageKeys_[layer.id];
        if (loaded == loadedKey && data_->LayerImage(layer.id) != nullptr) {
            continue;
        }
        loaded = loadedKey;
        const std::filesystem::path source(
            std::u8string_view(reinterpret_cast<const char8_t*>(layer.image.source.data()), layer.image.source.size()));
        auto image = std::make_shared<HeightImage>();
        std::string message{};
        if (LoadHeightImage(root / source, layer.image.rawWidth, *image, message)) {
            data_->SetLayerImage(layer.id, std::move(image));
        } else if (error != nullptr) {
            *error = layer.name + ": " + message;
        }
    }
}

void TerrainStore::BakeTileRect(TileCoord tile, const SampleRect& area, const HeightStackEvaluator& evaluator) {
    const auto found = tiles_.find(tile);
    if (found == tiles_.end()) {
        return;
    }
    TileRuntime& runtime = found->second;
    if (runtime.heights.empty() || runtime.normalMips.empty() || runtime.uploadPending) {
        BakeTile(tile, evaluator);
        return;
    }
    const TerrainGrid& grid = settings_.grid;
    const uint32_t samples = TileSamples(grid);
    const SampleRect tileRect = TileSampleRect(grid, tile);
    SampleRect bordered = area;
    bordered.x0 -= 1;
    bordered.z0 -= 1;
    bordered.width += 2u;
    bordered.height += 2u;
    std::vector<float> values{};
    if (!evaluator.EvaluateRect(bordered, values)) {
        return;
    }
    const uint32_t x0 = static_cast<uint32_t>(area.x0 - tileRect.x0);
    const uint32_t z0 = static_cast<uint32_t>(area.z0 - tileRect.z0);
    const uint32_t x1 = x0 + area.width - 1u;
    const uint32_t z1 = z0 + area.height - 1u;
    const uint32_t stride = bordered.width;
    const float scale = 0.5f / static_cast<float>(SampleSpacing(grid));
    for (uint32_t z = 0u; z < area.height; ++z) {
        for (uint32_t x = 0u; x < area.width; ++x) {
            const size_t center = static_cast<size_t>(z + 1u) * stride + x + 1u;
            const size_t index = static_cast<size_t>(z0 + z) * samples + x0 + x;
            runtime.heights[index] = QuantizeHeight(grid, values[center]);
            const float dx = (values[center + 1u] - values[center - 1u]) * scale;
            const float dz = (values[center + stride] - values[center - stride]) * scale;
            const float inverseLength = 1.0f / std::sqrt(dx * dx + dz * dz + 1.0f);
            runtime.normalMips[index * 2u] = ToSnorm16(-dx * inverseLength);
            runtime.normalMips[index * 2u + 1u] = ToSnorm16(-dz * inverseLength);
        }
    }
    // 粗い段は変わった範囲だけ 4 標本の平均で作り直す。
    size_t offset = 0u;
    uint32_t size = samples;
    uint32_t ax = x0;
    uint32_t az = z0;
    uint32_t bx = x1;
    uint32_t bz = z1;
    for (uint32_t mip = 1u; mip < kNormalMipCount; ++mip) {
        const size_t nextOffset = offset + static_cast<size_t>(size) * size * 2u;
        const uint32_t next = (std::max)(size >> 1u, 1u);
        ax >>= 1u;
        az >>= 1u;
        bx = (std::min)(bx >> 1u, next - 1u);
        bz = (std::min)(bz >> 1u, next - 1u);
        for (uint32_t z = az; z <= bz; ++z) {
            for (uint32_t x = ax; x <= bx; ++x) {
                for (uint32_t c = 0u; c < 2u; ++c) {
                    const auto at = [&](uint32_t sx, uint32_t sz) {
                        return static_cast<int32_t>(
                            runtime.normalMips[offset + (static_cast<size_t>(sz) * size + sx) * 2u + c]);
                    };
                    runtime.normalMips[nextOffset + (static_cast<size_t>(z) * next + x) * 2u + c] =
                        static_cast<int16_t>((at(x * 2u, z * 2u) + at(x * 2u + 1u, z * 2u) + at(x * 2u, z * 2u + 1u) +
                                              at(x * 2u + 1u, z * 2u + 1u)) /
                                             4);
                }
            }
        }
        offset = nextOffset;
        size = next;
    }
    // 触った葉の高さの範囲を標本から数え直す（遠近分級の境界）。
    const uint32_t cells = grid.tileCells;
    const auto leafOf = [&](uint32_t sample) {
        return (std::min)(sample * kTileLeafDivisions / cells, kTileLeafDivisions - 1u);
    };
    const uint32_t leafX0 = leafOf(x0 == 0u ? 0u : x0 - 1u);
    const uint32_t leafX1 = leafOf(x1);
    const uint32_t leafZ0 = leafOf(z0 == 0u ? 0u : z0 - 1u);
    const uint32_t leafZ1 = leafOf(z1);
    for (uint32_t leafZ = leafZ0; leafZ <= leafZ1; ++leafZ) {
        for (uint32_t leafX = leafX0; leafX <= leafX1; ++leafX) {
            uint16_t low = 0xFFFFu;
            uint16_t high = 0u;
            for (uint32_t z = leafZ * cells / kTileLeafDivisions; z <= (leafZ + 1u) * cells / kTileLeafDivisions; ++z) {
                for (uint32_t x = leafX * cells / kTileLeafDivisions; x <= (leafX + 1u) * cells / kTileLeafDivisions;
                     ++x) {
                    const uint16_t value = runtime.heights[static_cast<size_t>(z) * samples + x];
                    low = (std::min)(low, value);
                    high = (std::max)(high, value);
                }
            }
            const size_t index = (static_cast<size_t>(leafZ) * kTileLeafDivisions + leafX) * 2u;
            runtime.leafMinMax[index] = DequantizeHeight(grid, low);
            runtime.leafMinMax[index + 1u] = DequantizeHeight(grid, high);
        }
    }
    runtime.minHeight = std::numeric_limits<float>::max();
    runtime.maxHeight = std::numeric_limits<float>::lowest();
    for (size_t leaf = 0u; leaf < static_cast<size_t>(kTileLeafDivisions) * kTileLeafDivisions; ++leaf) {
        runtime.minHeight = (std::min)(runtime.minHeight, runtime.leafMinMax[leaf * 2u]);
        runtime.maxHeight = (std::max)(runtime.maxHeight, runtime.leafMinMax[leaf * 2u + 1u]);
    }
    if (runtime.rectPending) {
        runtime.pendingX0 = (std::min)(runtime.pendingX0, x0);
        runtime.pendingZ0 = (std::min)(runtime.pendingZ0, z0);
        runtime.pendingX1 = (std::max)(runtime.pendingX1, x1);
        runtime.pendingZ1 = (std::max)(runtime.pendingZ1, z1);
    } else {
        runtime.pendingX0 = x0;
        runtime.pendingZ0 = z0;
        runtime.pendingX1 = x1;
        runtime.pendingZ1 = z1;
        runtime.rectPending = true;
    }
    ++runtime.revision;
}

void TerrainStore::RebakeChanged(std::span<const TileCoord> tiles) {
    std::vector<TileCoord> touched{};
    for (const TileCoord tile : tiles) {
        for (int32_t dz = -1; dz <= 1; ++dz) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const TileCoord neighbor{tile.x + dx, tile.z + dz};
                if (tiles_.contains(neighbor) && std::find(touched.begin(), touched.end(), neighbor) == touched.end()) {
                    touched.push_back(neighbor);
                }
            }
        }
    }
    RebakeTiles(touched);
    MarkSaveDirty(touched);
}

void TerrainStore::MarkSaveDirty(std::span<const TileCoord> tiles) {
    for (const TileCoord tile : tiles) {
        if (std::find(unsavedTiles_.begin(), unsavedTiles_.end(), tile) == unsavedTiles_.end()) {
            unsavedTiles_.push_back(tile);
        }
    }
}

void TerrainStore::MarkAllForUpload() noexcept {
    for (auto& [tile, runtime] : tiles_) {
        runtime.uploadPending = true;
        runtime.farUploaded = false;
    }
}

bool TerrainStore::Save(const std::filesystem::path& scenePath, std::string& error) {
    if (data_ == nullptr) {
        return true;
    }
    const bool samePath = scenePath == scenePath_;
    const std::filesystem::path directory = TerrainCellDirectory(scenePath);
    std::error_code ec{};
    std::filesystem::create_directories(directory, ec);
    std::string message{};
    if ((!samePath || stackDirty_) &&
        !WriteJson(directory / kLayerFileName, WriteHeightLayerStackJson(data_->Stack()), message)) {
        error = "Could not write the terrain layers: " + message;
        return false;
    }
    nlohmann::json surfaceFile{{"slots", surfaces_.Slots()},
                               {"snowLineMeters", weather_.snowLineMeters},
                               {"snowAmount", weather_.snowAmount},
                               {"wetness", weather_.wetness}};
    JSONFIELDS::Write(surfaceFile, surfaceRules_, kSurfaceRuleSetFields);
    if ((!samePath || surfaceSlotsDirty_) && !WriteJson(directory / kSurfaceSlotFileName, surfaceFile, message)) {
        error = "Could not write the terrain surfaces: " + message;
        return false;
    }
    surfaceSlotsDirty_ = false;
    const HeightStackEvaluator evaluator(*data_);
    std::vector<TileCoord> tiles = unsavedTiles_;
    if (!samePath) {
        tiles.clear();
        for (const auto& [tile, runtime] : tiles_) {
            tiles.push_back(tile);
        }
    }
    for (const TileCoord tile : tiles) {
        std::vector<uint8_t> bytes{};
        if (!WriteTerrainCell(*data_, tile, &evaluator, {}, bytes, message) ||
            !SaveTerrainCellFile(directory / TerrainCellFileName(tile), bytes, message)) {
            error = "Could not write terrain tile: " + message;
            return false;
        }
    }
    unsavedTiles_.clear();
    stackDirty_ = false;
    scenePath_ = scenePath;
    return true;
}

std::optional<float> TerrainStore::HeightAt(float worldX, float worldZ) const {
    if (data_ == nullptr) {
        return std::nullopt;
    }
    const TerrainGrid& grid = settings_.grid;
    const TileCoord tile = TileOfWorld(grid, worldX, worldZ);
    const auto found = tiles_.find(tile);
    if (found == tiles_.end() || found->second.heights.empty()) {
        return std::nullopt;
    }
    const float spacing = static_cast<float>(SampleSpacing(grid));
    const float localX = worldX / spacing - static_cast<float>(TileFirstSample(grid, tile.x));
    const float localZ = worldZ / spacing - static_cast<float>(TileFirstSample(grid, tile.z));
    const uint32_t samples = TileSamples(grid);
    const uint32_t x0 = (std::min)(static_cast<uint32_t>((std::max)(localX, 0.0f)), samples - 2u);
    const uint32_t z0 = (std::min)(static_cast<uint32_t>((std::max)(localZ, 0.0f)), samples - 2u);
    const float fx = std::clamp(localX - static_cast<float>(x0), 0.0f, 1.0f);
    const float fz = std::clamp(localZ - static_cast<float>(z0), 0.0f, 1.0f);
    const auto at = [&](uint32_t x, uint32_t z) {
        return DequantizeHeight(grid, found->second.heights[static_cast<size_t>(z) * samples + x]);
    };
    const float h0 = at(x0, z0) + (at(x0 + 1u, z0) - at(x0, z0)) * fx;
    const float h1 = at(x0, z0 + 1u) + (at(x0 + 1u, z0 + 1u) - at(x0, z0 + 1u)) * fx;
    return h0 + (h1 - h0) * fz;
}

bool TerrainStore::Raycast(const float origin[3], const float direction[3], float maxDistance, float outHit[3]) const {
    const float step = (std::max)(static_cast<float>(SampleSpacing(settings_.grid)), 0.5f) * 2.0f;
    float previous = 0.0f;
    float distance = step;
    float stride = step;
    while (distance < maxDistance) {
        const float x = origin[0] + direction[0] * distance;
        const float y = origin[1] + direction[1] * distance;
        const float z = origin[2] + direction[2] * distance;
        const std::optional<float> ground = HeightAt(x, z);
        if (ground.has_value() && y <= *ground) {
            float low = previous;
            float high = distance;
            for (int iteration = 0; iteration < 12; ++iteration) {
                const float middle = 0.5f * (low + high);
                const std::optional<float> height =
                    HeightAt(origin[0] + direction[0] * middle, origin[2] + direction[2] * middle);
                if (height.has_value() && origin[1] + direction[1] * middle <= *height) {
                    high = middle;
                } else {
                    low = middle;
                }
            }
            outHit[0] = origin[0] + direction[0] * high;
            outHit[1] = origin[1] + direction[1] * high;
            outHit[2] = origin[2] + direction[2] * high;
            return true;
        }
        previous = distance;
        distance += stride;
        // 遠くは粗く進める（遠景で細い山を飛び越すのは許容する）。
        stride = (std::max)(step, distance * 0.01f);
    }
    return false;
}

} // namespace HIKARI::TERRAINKIT
