#include "HIKARI_TerrainKitRuleBaker.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <execution>

#include "HIKARI_TerrainKitStore.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"
#include "TerrainKit/Data/HIKARI_TerrainDataRules.h"

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

// 遠景の色：制御図の 2 材質の平均色を混ぜて全段を作る（RGB は平方根で詰める、A は粗さ）。
std::vector<uint8_t> BuildFarColor(const std::vector<uint32_t>& control, uint32_t samples,
                                   const std::vector<std::array<float, 4>>& surfaces) {
    std::vector<float> level(static_cast<size_t>(kFarCacheSize) * kFarCacheSize * 4u);
    const uint32_t lastSample = samples - 1u;
    const auto limit = surfaces.size() - 1u;
    for (uint32_t z = 0u; z < kFarCacheSize; ++z) {
        const uint32_t sourceZ = z * lastSample / (kFarCacheSize - 1u);
        for (uint32_t x = 0u; x < kFarCacheSize; ++x) {
            const uint32_t sourceX = x * lastSample / (kFarCacheSize - 1u);
            const ControlTexel texel = UnpackControlTexel(control[static_cast<size_t>(sourceZ) * samples + sourceX]);
            const float blend = static_cast<float>(texel.blend) / 255.0f;
            const auto& base = surfaces[(std::min)(static_cast<size_t>(texel.baseMaterial), limit)];
            const auto& overlay = surfaces[(std::min)(static_cast<size_t>(texel.overlayMaterial), limit)];
            float* out = level.data() + (static_cast<size_t>(z) * kFarCacheSize + x) * 4u;
            for (int channel = 0; channel < 4; ++channel) {
                out[channel] = base[channel] + (overlay[channel] - base[channel]) * blend;
            }
        }
    }
    std::vector<uint8_t> bytes{};
    uint32_t size = kFarCacheSize;
    for (uint32_t mip = 0u; mip < kFarCacheMips; ++mip) {
        const size_t offset = bytes.size();
        bytes.resize(offset + static_cast<size_t>(size) * size * 4u);
        for (size_t index = 0u; index < static_cast<size_t>(size) * size * 4u; ++index) {
            const float value = (index % 4u) == 3u ? level[index] : std::sqrt((std::max)(level[index], 0.0f));
            bytes[offset + index] = static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        if (mip + 1u == kFarCacheMips) {
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

void TerrainRuleBaker::Shutdown() {
    if (job_.valid()) {
        job_.wait();
        (void)job_.get();
    }
    activeTiles_ = 0u;
}

void TerrainRuleBaker::Update(TerrainStore& store) {
    if (job_.valid()) {
        if (job_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        std::vector<TileResult> results = job_.get();
        activeTiles_ = 0u;
        for (TileResult& result : results) {
            store.ApplyRuleResult(result.tile, result.hasControl, std::move(result.control),
                                  std::move(result.farColor));
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
    std::vector<std::array<float, 4>> colors{};
    for (const CookedSurface& surface : surfaces) {
        colors.push_back({surface.averageColor[0], surface.averageColor[1], surface.averageColor[2],
                          std::clamp(surface.asset.roughnessValue, 0.0f, 1.0f)});
    }
    activeTiles_ = inputs.size();
    job_ = std::async(std::launch::async,
                      [inputs = std::move(inputs), rules = store.SurfaceRules(), colors = std::move(colors), samples,
                       spacing = static_cast<float>(SampleSpacing(grid))]() mutable {
                          std::vector<TileResult> results(inputs.size());
                          std::vector<size_t> indices(inputs.size());
                          for (size_t index = 0u; index < indices.size(); ++index) {
                              indices[index] = index;
                          }
                          std::for_each(std::execution::par, indices.begin(), indices.end(), [&](size_t index) {
                              TileInput& input = inputs[index];
                              TileResult& result = results[index];
                              result.tile = input.tile;
                              if (!rules.rules.empty()) {
                                  SurfaceRuleTile tile{};
                                  tile.samples = samples;
                                  tile.spacingMeters = spacing;
                                  tile.firstSampleX = input.firstSampleX;
                                  tile.firstSampleZ = input.firstSampleZ;
                                  tile.heights = input.heights;
                                  EvaluateSurfaceRules(rules, tile, input.control);
                                  result.hasControl = true;
                              }
                              result.farColor = BuildFarColor(input.control, samples, colors);
                              result.control = std::move(input.control);
                          });
                          return results;
                      });
}

} // namespace HIKARI::TERRAINKIT
