#include "HIKARI_TerrainKitRuleBaker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <execution>
#include <utility>

#include "HIKARI_TerrainKitStore.h"
#include "HIKARI_TerrainKitSurfaces.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"
#include "TerrainKit/Data/HIKARI_TerrainDataRules.h"
#include "TerrainKit/Data/HIKARI_TerrainDataSpline.h"

namespace HIKARI::TERRAINKIT {
namespace {

using namespace TERRAIN::DATA;

// 筆で塗っている間は計算を始めない（離してからまとめて）。
constexpr uint32_t kQuietFramesBeforeStart = 12u;
constexpr size_t kTilesPerJob = 16u;

struct TileInput {
    TileCoord tile{};
    int64_t firstSampleX = 0;
    int64_t firstSampleZ = 0;
    std::vector<float> heights{};
    std::vector<uint32_t> control{};
};

// 瓦片と周りの余白の高さを、隣の瓦片から集める。無い瓦片は端の標本で埋める。
std::vector<float> CollectHeights(const TerrainStore& store, TileCoord tile) {
    const TerrainGrid& grid = store.Settings().grid;
    const uint32_t samples = TileSamples(grid);
    const int32_t cells = static_cast<int32_t>(grid.tileCells);
    const int32_t margin = static_cast<int32_t>(kSurfaceRuleHeightMargin);
    const int32_t stride = static_cast<int32_t>(samples) + 2 * margin;
    std::array<const TileRuntime*, 9u> neighbors{};
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const auto found = store.Tiles().find(TileCoord{tile.x + dx, tile.z + dz});
            const bool usable = found != store.Tiles().end() && !found->second.heights.empty();
            neighbors[static_cast<size_t>(dz + 1) * 3u + static_cast<size_t>(dx + 1)] =
                usable ? &found->second : nullptr;
        }
    }
    std::vector<float> heights(static_cast<size_t>(stride) * stride, 0.0f);
    for (int32_t row = 0; row < stride; ++row) {
        const int32_t z = row - margin;
        for (int32_t column = 0; column < stride; ++column) {
            const int32_t x = column - margin;
            const int32_t dx = x < 0 ? -1 : (x > cells ? 1 : 0);
            const int32_t dz = z < 0 ? -1 : (z > cells ? 1 : 0);
            const TileRuntime* source = neighbors[static_cast<size_t>(dz + 1) * 3u + static_cast<size_t>(dx + 1)];
            int32_t localX = x - dx * cells;
            int32_t localZ = z - dz * cells;
            if (source == nullptr) {
                source = neighbors[4];
                localX = std::clamp(x, 0, cells);
                localZ = std::clamp(z, 0, cells);
            }
            if (source != nullptr) {
                heights[static_cast<size_t>(row) * stride + static_cast<size_t>(column)] = DequantizeHeight(
                    grid, source->heights[static_cast<size_t>(localZ) * samples + static_cast<size_t>(localX)]);
            }
        }
    }
    return heights;
}

// 読み取りの格子（m）。集水と尾根・谷を見るには十分で、大きな世界でも背景で一瞬で解ける細かさ。
constexpr float kAnalysisCellMeters = 4.0f;

// 世界全体の粗い高さを焼いた瓦片から集める。
TerrainAnalysis CollectAnalysisHeights(const TerrainStore& store) {
    const TerrainSettings& settings = store.Settings();
    const TerrainGrid& grid = settings.grid;
    const uint32_t samples = TileSamples(grid);
    const uint32_t step = (std::max)(static_cast<uint32_t>(std::lround(kAnalysisCellMeters / SampleSpacing(grid))), 1u);
    const uint32_t cellsPerTile = (samples - 1u) / step;
    TerrainAnalysis analysis{};
    analysis.width = static_cast<uint32_t>(settings.tilesX) * cellsPerTile + 1u;
    analysis.height = static_cast<uint32_t>(settings.tilesZ) * cellsPerTile + 1u;
    analysis.cellMeters = static_cast<float>(SampleSpacing(grid) * step);
    analysis.originX = static_cast<double>(settings.firstTileX) * grid.tileSizeMeters;
    analysis.originZ = static_cast<double>(settings.firstTileZ) * grid.tileSizeMeters;
    analysis.heights.assign(static_cast<size_t>(analysis.width) * analysis.height, 0.0f);
    for (const auto& [tile, runtime] : store.Tiles()) {
        if (runtime.heights.empty()) {
            continue;
        }
        const uint32_t firstX = static_cast<uint32_t>(tile.x - settings.firstTileX) * cellsPerTile;
        const uint32_t firstZ = static_cast<uint32_t>(tile.z - settings.firstTileZ) * cellsPerTile;
        for (uint32_t z = 0u; z <= cellsPerTile; ++z) {
            for (uint32_t x = 0u; x <= cellsPerTile; ++x) {
                analysis.heights[static_cast<size_t>(firstZ + z) * analysis.width + firstX + x] =
                    DequantizeHeight(grid, runtime.heights[static_cast<size_t>(z * step) * samples + x * step]);
            }
        }
    }
    return analysis;
}

// 全段を作って詰める。RGB は線形の平方根で詰める（暗い色の段差を減らす）。
std::vector<uint8_t> PackMipChain(std::vector<float> level) {
    std::vector<uint8_t> bytes{};
    uint32_t size = kColorMapSize;
    for (uint32_t mip = 0u; mip < kColorMapMips; ++mip) {
        const size_t offset = bytes.size();
        bytes.resize(offset + static_cast<size_t>(size) * size * 4u);
        for (size_t index = 0u; index < static_cast<size_t>(size) * size * 4u; ++index) {
            const float value = (index % 4u) != 3u ? std::sqrt((std::max)(level[index], 0.0f)) : level[index];
            bytes[offset + index] = static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        if (mip + 1u == kColorMapMips) {
            break;
        }
        const uint32_t next = (std::max)(size >> 1u, 1u);
        std::vector<float> reduced(static_cast<size_t>(next) * next * 4u);
        for (uint32_t z = 0u; z < next; ++z) {
            for (uint32_t x = 0u; x < next; ++x) {
                for (uint32_t channel = 0u; channel < 4u; ++channel) {
                    const auto at = [&](uint32_t sx, uint32_t sz) {
                        return level[(static_cast<size_t>(sz) * size + sx) * 4u + channel];
                    };
                    reduced[(static_cast<size_t>(z) * next + x) * 4u + channel] =
                        0.25f * (at(x * 2u, z * 2u) + at(x * 2u + 1u, z * 2u) + at(x * 2u, z * 2u + 1u) +
                                 at(x * 2u + 1u, z * 2u + 1u));
                }
            }
        }
        level = std::move(reduced);
        size = next;
    }
    return bytes;
}

} // namespace

void TerrainRuleBaker::MarkDirty(TileCoord tile) {
    dirty_.insert(tile);
    quietFrames_ = 0u;
}

void TerrainRuleBaker::MarkAllDirty(const TerrainStore& store) {
    for (const auto& [tile, runtime] : store.Tiles()) {
        dirty_.insert(tile);
    }
    quietFrames_ = kQuietFramesBeforeStart;
}

void TerrainRuleBaker::Cancel() {
    if (job_.valid()) {
        job_.wait();
        (void)job_.get();
    }
    activeTiles_ = 0u;
    dirty_.clear();
}

void TerrainRuleBaker::Update(TerrainStore& store) {
    if (job_.valid()) {
        if (job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        JobResult finished = job_.get();
        activeTiles_ = 0u;
        analysis_ = std::move(finished.analysis);
        for (TileResult& result : finished.tiles) {
            store.ApplyRuleResult(result.tile, std::move(result.control), std::move(result.colorMap));
        }
    }
    const auto& surfaces = store.Surfaces().Surfaces();
    if (dirty_.empty() || !store.IsOpen() || surfaces.empty() || store.Surfaces().Busy() ||
        ++quietFrames_ < kQuietFramesBeforeStart) {
        return;
    }
    std::vector<TileInput> inputs{};
    const TerrainGrid& grid = store.Settings().grid;
    const uint32_t samples = TileSamples(grid);
    for (auto it = dirty_.begin(); it != dirty_.end() && inputs.size() < kTilesPerJob;) {
        const auto tile = store.Tiles().find(*it);
        if (tile == store.Tiles().end()) {
            it = dirty_.erase(it);
            continue;
        }
        if (tile->second.heights.empty()) {
            ++it;
            continue;
        }
        TileInput input{};
        input.tile = *it;
        const SampleRect rect = TileSampleRect(grid, *it);
        input.firstSampleX = rect.x0;
        input.firstSampleZ = rect.z0;
        input.heights = CollectHeights(store, *it);
        const auto* raster = store.Data().Control().Find(*it);
        input.control =
            raster != nullptr ? raster->values : std::vector<uint32_t>(static_cast<size_t>(samples) * samples, 0u);
        inputs.push_back(std::move(input));
        it = dirty_.erase(it);
    }
    if (inputs.empty()) {
        return;
    }
    std::vector<SurfaceMapStyle> styles{};
    styles.reserve(surfaces.size());
    for (const CookedSurface& surface : surfaces) {
        styles.push_back(MakeSurfaceMapStyle(surface));
    }
    // 曲線は下の層から順に塗る（上の層が勝つ）。
    std::vector<PreparedSpline> splines{};
    for (const HeightLayer& layer : store.Data().Stack().layers) {
        if (layer.kind == HeightLayerKind::Spline && layer.enabled) {
            splines.push_back(PrepareSpline(layer.spline));
        }
    }
    // 高さが変わっていなければ前の読み取りをそのまま使う。
    TerrainAnalysis fresh = CollectAnalysisHeights(store);
    std::shared_ptr<const TerrainAnalysis> previous =
        analysis_ != nullptr && analysis_->heights == fresh.heights ? analysis_ : nullptr;
    activeTiles_ = inputs.size();
    job_ = std::async(std::launch::async,
                      [inputs = std::move(inputs), rules = store.SurfaceRules(), styles = std::move(styles), samples,
                       splines = std::move(splines), grid, previous = std::move(previous), fresh = std::move(fresh),
                       spacing = static_cast<float>(SampleSpacing(grid))]() mutable {
                          std::shared_ptr<const TerrainAnalysis> analysis = previous;
                          if (analysis == nullptr) {
                              BuildTerrainAnalysis(fresh);
                              analysis = std::make_shared<const TerrainAnalysis>(std::move(fresh));
                          }
                          std::vector<TileResult> results(inputs.size());
                          std::vector<size_t> indices(inputs.size());
                          for (size_t index = 0u; index < indices.size(); ++index) {
                              indices[index] = index;
                          }
                          std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t index) {
                              TileInput& input = inputs[index];
                              TileResult& result = results[index];
                              result.tile = input.tile;
                              SurfaceRuleTile tile{};
                              tile.samples = samples;
                              tile.spacingMeters = spacing;
                              tile.firstSampleX = input.firstSampleX;
                              tile.firstSampleZ = input.firstSampleZ;
                              tile.heights = input.heights;
                              tile.analysis = analysis.get();
                              EvaluateSurfaceRules(rules, tile, input.control);
                              PaintSplineSurfaces(splines, grid, TileSampleRect(grid, input.tile), input.control);
                              std::vector<float> color{};
                              BakeSurfaceColorMap(rules, tile, input.control, styles, kColorMapSize, color);
                              result.colorMap = PackMipChain(std::move(color));
                              result.control = std::move(input.control);
                          });
                          return JobResult{std::move(results), std::move(analysis)};
                      });
}

} // namespace HIKARI::TERRAINKIT
