#include "TerrainKit/Data/HIKARI_TerrainDataAnalysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>
#include <functional>
#include <numeric>
#include <queue>
#include <utility>

#include "HIKARI_TerrainDataFlow.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

// 埋めた窪地にも流れの向きができるよう、次の格子を少しだけ高くする。
constexpr float kFillStep = 1.0e-3f;

float SmoothStep(float low, float high, float value) noexcept {
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void FillDepressions(const std::vector<float>& heights, uint32_t width, uint32_t height, std::vector<float>& filled,
                     std::vector<uint32_t>& order) {
    const size_t count = heights.size();
    filled.resize(count);
    order.clear();
    order.reserve(count);
    std::vector<uint8_t> visited(count, 0u);
    using Entry = std::pair<float, uint32_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open{};
    const auto seed = [&](uint32_t x, uint32_t z) {
        const uint32_t index = z * width + x;
        if (visited[index] == 0u) {
            visited[index] = 1u;
            filled[index] = heights[index];
            open.emplace(filled[index], index);
        }
    };
    for (uint32_t x = 0u; x < width; ++x) {
        seed(x, 0u);
        seed(x, height - 1u);
    }
    for (uint32_t z = 0u; z < height; ++z) {
        seed(0u, z);
        seed(width - 1u, z);
    }
    while (!open.empty()) {
        const auto [level, index] = open.top();
        open.pop();
        order.push_back(index);
        const int x = static_cast<int>(index % width);
        const int z = static_cast<int>(index / width);
        for (size_t n = 0u; n < kFlowNeighborX.size(); ++n) {
            const int nx = x + kFlowNeighborX[n];
            const int nz = z + kFlowNeighborZ[n];
            if (nx < 0 || nz < 0 || nx >= static_cast<int>(width) || nz >= static_cast<int>(height)) {
                continue;
            }
            const uint32_t neighbor = static_cast<uint32_t>(nz) * width + static_cast<uint32_t>(nx);
            if (visited[neighbor] == 0u) {
                visited[neighbor] = 1u;
                filled[neighbor] = (std::max)(heights[neighbor], level + kFillStep);
                open.emplace(filled[neighbor], neighbor);
            }
        }
    }
}

namespace {

float SampleBilinear(const std::vector<float>& values, uint32_t width, uint32_t height, double fx, double fz) noexcept {
    fx = std::clamp(fx, 0.0, static_cast<double>(width - 1u));
    fz = std::clamp(fz, 0.0, static_cast<double>(height - 1u));
    const uint32_t x0 = (std::min)(static_cast<uint32_t>(fx), width - 2u);
    const uint32_t z0 = (std::min)(static_cast<uint32_t>(fz), height - 2u);
    const auto tx = static_cast<float>(fx - x0);
    const auto tz = static_cast<float>(fz - z0);
    const float* row0 = values.data() + static_cast<size_t>(z0) * width + x0;
    const float* row1 = row0 + width;
    const float top = row0[0] + (row0[1] - row0[0]) * tx;
    const float bottom = row1[0] + (row1[1] - row1[0]) * tx;
    return top + (bottom - top) * tz;
}

// 周りの円（8 方向）の平均の高さと中心の差。
float RingDifference(const std::vector<float>& heights, uint32_t width, uint32_t height, uint32_t x, uint32_t z,
                     double radius) noexcept {
    constexpr double kDiagonal = 0.70710678;
    const std::array<std::array<double, 2>, 8> directions{{{1.0, 0.0},
                                                           {-1.0, 0.0},
                                                           {0.0, 1.0},
                                                           {0.0, -1.0},
                                                           {kDiagonal, kDiagonal},
                                                           {-kDiagonal, kDiagonal},
                                                           {kDiagonal, -kDiagonal},
                                                           {-kDiagonal, -kDiagonal}}};
    float sum = 0.0f;
    for (const auto& direction : directions) {
        sum += SampleBilinear(heights, width, height, x + direction[0] * radius, z + direction[1] * radius);
    }
    return sum / 8.0f - heights[static_cast<size_t>(z) * width + x];
}

template <class RowFunction>
void ForEachRowParallel(uint32_t rows, RowFunction function) {
    std::vector<uint32_t> indices(rows);
    std::iota(indices.begin(), indices.end(), 0u);
    std::for_each(std::execution::par, indices.begin(), indices.end(), function);
}

} // namespace

float TerrainAnalysis::WetnessAt(double worldX, double worldZ) const noexcept {
    return Valid() ? SampleBilinear(wetness, width, height, (worldX - originX) / cellMeters,
                                    (worldZ - originZ) / cellMeters)
                   : 0.5f;
}

float TerrainAnalysis::PositionAt(double worldX, double worldZ) const noexcept {
    return Valid() ? SampleBilinear(position, width, height, (worldX - originX) / cellMeters,
                                    (worldZ - originZ) / cellMeters)
                   : 0.0f;
}

float TerrainAnalysis::DebrisAt(double worldX, double worldZ) const noexcept {
    return Valid()
               ? SampleBilinear(debris, width, height, (worldX - originX) / cellMeters, (worldZ - originZ) / cellMeters)
               : 0.0f;
}

void BuildTerrainAnalysis(TerrainAnalysis& analysis) {
    const uint32_t w = analysis.width;
    const uint32_t h = analysis.height;
    const size_t count = static_cast<size_t>(w) * h;
    analysis.wetness.assign(count, 0.5f);
    analysis.position.assign(count, 0.0f);
    analysis.debris.assign(count, 0.0f);
    if (w < 2u || h < 2u || analysis.heights.size() != count) {
        return;
    }
    std::vector<float> filled{};
    std::vector<uint32_t> order{};
    FillDepressions(analysis.heights, w, h, filled, order);
    const float cell = analysis.cellMeters;
    std::vector<float> area(count, 1.0f);
    std::vector<float> index(count, 0.0f);
    std::vector<float> rock(count, 0.0f); // 上流の急な岩の面積（格子の数）
    // 高い順に、低い隣へ傾きの割合で面積を配る。
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const uint32_t current = *it;
        const int x = static_cast<int>(current % w);
        const int z = static_cast<int>(current / w);
        std::array<float, 8> shares{};
        float total = 0.0f;
        float steepest = 0.0f;
        for (size_t n = 0u; n < kFlowNeighborX.size(); ++n) {
            const int nx = x + kFlowNeighborX[n];
            const int nz = z + kFlowNeighborZ[n];
            if (nx < 0 || nz < 0 || nx >= static_cast<int>(w) || nz >= static_cast<int>(h)) {
                continue;
            }
            const size_t neighbor = static_cast<size_t>(nz) * w + static_cast<size_t>(nx);
            const float slope = (filled[current] - filled[neighbor]) / (cell * kFlowNeighborDistance[n]);
            if (slope > 0.0f) {
                shares[n] = std::pow(slope, 1.1f);
                total += shares[n];
                steepest = (std::max)(steepest, slope);
            }
        }
        const float contour = area[current] * cell;
        index[current] = std::log(contour / (std::max)(steepest, 0.02f));
        rock[current] += SmoothStep(0.75f, 1.1f, steepest);
        // 水が細い筋に集まり始めてから色が運ばれる（斜面全体が岩の色にならない）。
        const float channel = SmoothStep(1.8f, 3.6f, std::log10(area[current] * cell * cell));
        analysis.debris[current] = channel * rock[current] / area[current];
        if (total <= 0.0f) {
            continue;
        }
        for (size_t n = 0u; n < kFlowNeighborX.size(); ++n) {
            if (shares[n] > 0.0f) {
                const size_t neighbor =
                    static_cast<size_t>(z + kFlowNeighborZ[n]) * w + static_cast<size_t>(x + kFlowNeighborX[n]);
                area[neighbor] += area[current] * shares[n] / total;
                rock[neighbor] += rock[current] * shares[n] / total;
            }
        }
    }
    constexpr float kDryIndex = 5.0f;
    constexpr float kWetIndex = 12.0f;
    std::vector<float> raw(count);
    for (size_t i = 0u; i < count; ++i) {
        raw[i] = std::clamp((index[i] - kDryIndex) / (kWetIndex - kDryIndex), 0.0f, 1.0f);
    }
    const double nearRadius = 64.0 / cell;
    const double wideRadius = 192.0 / cell;
    const int blur = (std::max)(static_cast<int>(std::lround(8.0f / cell)), 1);
    ForEachRowParallel(h, [&](uint32_t z) {
        for (uint32_t x = 0u; x < w; ++x) {
            const size_t i = static_cast<size_t>(z) * w + x;
            // 8 m ぶんぼかす（細い線にならず、谷筋の帯になる）。
            float sum = 0.0f;
            float weight = 0.0f;
            for (int dz = -blur; dz <= blur; ++dz) {
                for (int dx = -blur; dx <= blur; ++dx) {
                    const int nx = static_cast<int>(x) + dx;
                    const int nz = static_cast<int>(z) + dz;
                    if (nx >= 0 && nz >= 0 && nx < static_cast<int>(w) && nz < static_cast<int>(h)) {
                        sum += raw[static_cast<size_t>(nz) * w + static_cast<size_t>(nx)];
                        weight += 1.0f;
                    }
                }
            }
            analysis.wetness[i] = sum / weight;
            const float nearDifference = RingDifference(analysis.heights, w, h, x, z, nearRadius);
            const float wideDifference = RingDifference(analysis.heights, w, h, x, z, wideRadius);
            analysis.position[i] =
                std::clamp(0.6f * nearDifference / 7.0f + 0.4f * wideDifference / 22.0f, -1.0f, 1.0f);
        }
    });
}

} // namespace HIKARI::TERRAIN::DATA
