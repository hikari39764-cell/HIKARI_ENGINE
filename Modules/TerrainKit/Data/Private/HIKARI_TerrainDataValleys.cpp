#include "TerrainKit/Data/HIKARI_TerrainDataValleys.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>
#include <numeric>
#include <utility>
#include <vector>

#include "HIKARI_TerrainDataFlow.h"
#include "HIKARI_TerrainDataNoise.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

struct LevelGrid {
    uint32_t width = 0u;
    uint32_t height = 0u;
    float cell = 1.0f;
    std::vector<float> heights{};

    size_t Index(uint32_t x, uint32_t z) const noexcept { return static_cast<size_t>(z) * width + x; }
};

// 流れの向きと集水面積。
struct FlowRoute {
    std::vector<float> filled{};
    std::vector<float> area{};
    std::vector<float> distance{};
    std::vector<uint32_t> receiver{};
    std::vector<uint32_t> order{};
};

struct LevelSettings {
    uint32_t iterations = 0u;
    float erodibility = 0.0f;
    float channelArea = 0.0f; // これ未満の集水面積は川にならない
    float hillSlope = 1.0f;
    float diffusion = 0.0f;
    float floorDepth = 0.0f; // 大きな谷の底を埋める堆積の深さ
    float keep = 1.0f;       // 限界を超えた分のうち 1 回で崩れずに残る割合
};

template <class RowFunction>
void ForEachRow(uint32_t rows, RowFunction function) {
    std::vector<uint32_t> indices(rows);
    std::iota(indices.begin(), indices.end(), 0u);
    std::for_each(std::execution::par, indices.begin(), indices.end(), function);
}

float SmoothStep(float low, float high, float value) noexcept {
    const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

std::array<float, 4> CubicWeights(float t) noexcept {
    const float s = 1.0f - t;
    return {s * s * s / 6.0f, (3.0f * t * t * t - 6.0f * t * t + 4.0f) / 6.0f,
            (-3.0f * t * t * t + 3.0f * t * t + 3.0f * t + 1.0f) / 6.0f, t * t * t / 6.0f};
}

// 一様 3 次 B スプラインで読む（折れ目が法線に出ない）。
float SampleCubic(const LevelGrid& grid, float fx, float fz) noexcept {
    const int baseX = static_cast<int>(std::floor(fx));
    const int baseZ = static_cast<int>(std::floor(fz));
    const std::array<float, 4> weightsX = CubicWeights(fx - static_cast<float>(baseX));
    const std::array<float, 4> weightsZ = CubicWeights(fz - static_cast<float>(baseZ));
    const int lastX = static_cast<int>(grid.width) - 1;
    const int lastZ = static_cast<int>(grid.height) - 1;
    float sum = 0.0f;
    for (int dz = 0; dz < 4; ++dz) {
        const size_t row = static_cast<size_t>(std::clamp(baseZ + dz - 1, 0, lastZ)) * grid.width;
        float line = 0.0f;
        for (int dx = 0; dx < 4; ++dx) {
            line += weightsX[static_cast<size_t>(dx)] * grid.heights[row + std::clamp(baseX + dx - 1, 0, lastX)];
        }
        sum += weightsZ[static_cast<size_t>(dz)] * line;
    }
    return sum;
}

LevelGrid DoubleResolution(const LevelGrid& source) {
    LevelGrid out{};
    out.width = source.width * 2u - 1u;
    out.height = source.height * 2u - 1u;
    out.cell = source.cell * 0.5f;
    out.heights.resize(static_cast<size_t>(out.width) * out.height);
    ForEachRow(out.height, [&](uint32_t z) {
        for (uint32_t x = 0u; x < out.width; ++x) {
            out.heights[out.Index(x, z)] =
                SampleCubic(source, static_cast<float>(x) * 0.5f, static_cast<float>(z) * 0.5f);
        }
    });
    return out;
}

// 岩質の図（1 が平均）。値の大きい所は深く彫られて谷壁が立ち、小さい所は滑らかな斜面になる。
std::vector<float> BuildRockMap(const LevelGrid& grid, float amount, uint32_t seed) {
    std::vector<float> rock(grid.heights.size());
    ForEachRow(grid.height, [&](uint32_t z) {
        for (uint32_t x = 0u; x < grid.width; ++x) {
            const double worldX = static_cast<double>(x) * grid.cell;
            const double worldZ = static_cast<double>(z) * grid.cell;
            rock[grid.Index(x, z)] = std::exp(amount * 2.6f * FractalNoise(worldX / 300.0, worldZ / 300.0, 5, seed));
        }
    });
    return rock;
}

// 一番急な隣へ流す。少し乱して、格子の 8 方向の癖を崩す。
void RouteFlow(const LevelGrid& grid, FlowRoute& route, uint32_t seed) {
    constexpr float kJitter = 0.5f;
    const uint32_t w = grid.width;
    const uint32_t h = grid.height;
    FillDepressions(grid.heights, w, h, route.filled, route.order);
    route.receiver.resize(grid.heights.size());
    route.distance.resize(grid.heights.size());
    route.area.assign(grid.heights.size(), grid.cell * grid.cell);
    ForEachRow(h, [&](uint32_t z) {
        for (uint32_t x = 0u; x < w; ++x) {
            const size_t i = grid.Index(x, z);
            route.receiver[i] = static_cast<uint32_t>(i);
            route.distance[i] = grid.cell;
            if (x == 0u || z == 0u || x + 1u == w || z + 1u == h) {
                continue;
            }
            float best = 0.0f;
            for (size_t n = 0u; n < kFlowNeighborX.size(); ++n) {
                const size_t neighbor = grid.Index(x + kFlowNeighborX[n], z + kFlowNeighborZ[n]);
                const float distance = grid.cell * kFlowNeighborDistance[n];
                const float slope = (route.filled[i] - route.filled[neighbor]) / distance;
                if (slope <= 0.0f) {
                    continue;
                }
                const float noise =
                    static_cast<float>(HashLattice(static_cast<int64_t>(x) * 8 + static_cast<int64_t>(n),
                                                   static_cast<int64_t>(z), seed) &
                                       1023u) /
                    1024.0f;
                const float score = slope * (1.0f + kJitter * noise);
                if (score > best) {
                    best = score;
                    route.receiver[i] = static_cast<uint32_t>(neighbor);
                    route.distance[i] = distance;
                }
            }
        }
    });
    for (auto it = route.order.rbegin(); it != route.order.rend(); ++it) {
        if (route.receiver[*it] != *it) {
            route.area[route.receiver[*it]] += route.area[*it];
        }
    }
}

// 一段階を解く。低い順なら受け手が先に決まっているので、陰解法が 1 回で解ける。
void SolveLevel(LevelGrid& grid, const std::vector<float>& rock, const LevelSettings& settings, uint32_t seed) {
    const uint32_t w = grid.width;
    const uint32_t h = grid.height;
    FlowRoute route{};
    std::vector<float> next(grid.heights.size());
    for (uint32_t iteration = 0u; iteration < settings.iterations; ++iteration) {
        RouteFlow(grid, route, seed + iteration);
        for (const uint32_t i : route.order) {
            const uint32_t receiver = route.receiver[i];
            if (receiver == i) {
                next[i] = grid.heights[i];
                continue;
            }
            const float distance = route.distance[i];
            float solved = route.filled[i];
            if (route.area[i] > settings.channelArea) {
                const float rate =
                    settings.erodibility * rock[i] * std::sqrt(route.area[i] - settings.channelArea) / distance;
                solved = (solved + rate * next[receiver]) / (1.0f + rate);
            }
            // 限界を超えた斜面は崩れる。全部を一度に崩すと平らな三角の面になるので、少しずつ。
            const float limit = next[receiver] + settings.hillSlope * (0.7f + 0.5f * rock[i]) * distance;
            next[i] = solved > limit ? limit + (solved - limit) * settings.keep : solved;
        }
        ForEachRow(h, [&](uint32_t z) {
            for (uint32_t x = 0u; x < w; ++x) {
                const size_t i = grid.Index(x, z);
                const bool border = x == 0u || z == 0u || x + 1u == w || z + 1u == h;
                grid.heights[i] = border ? next[i]
                                         : next[i] + settings.diffusion * (next[i - 1u] + next[i + 1u] + next[i - w] +
                                                                           next[i + w] - 4.0f * next[i]);
            }
        });
    }
    if (settings.floorDepth <= 0.0f) {
        return;
    }
    // 大きな谷の底を堆積で埋める：川より低い横の格子は同じ高さまで上がり、平らな谷底になる。
    constexpr float kNoFloor = -1.0e9f;
    constexpr float kDeepestFloorArea = 1.0e6f;
    RouteFlow(grid, route, seed + settings.iterations);
    std::vector<float> floorLevel(grid.heights.size(), kNoFloor);
    for (const uint32_t i : route.order) {
        const uint32_t receiver = route.receiver[i];
        float level = receiver == i ? kNoFloor : floorLevel[receiver];
        if (route.area[i] > settings.channelArea * 4.0f) {
            level =
                (std::max)(level, route.filled[i] + settings.floorDepth *
                                                        (std::min)(1.0f, std::sqrt(route.area[i] / kDeepestFloorArea)));
        }
        floorLevel[i] = level;
        grid.heights[i] = (std::max)(route.filled[i], level);
    }
}

// 細かい揺らぎ：次の段階の水がこれに沿って枝分かれする。
void AddVariation(LevelGrid& grid, float amplitude, uint32_t seed) {
    if (amplitude <= 0.0f) {
        return;
    }
    const double wander = static_cast<double>(grid.cell) * 22.0;
    const double scale = static_cast<double>(grid.cell) * 8.0;
    ForEachRow(grid.height, [&](uint32_t z) {
        for (uint32_t x = 0u; x < grid.width; ++x) {
            const double worldX = static_cast<double>(x) * grid.cell;
            const double worldZ = static_cast<double>(z) * grid.cell;
            const double warp = static_cast<double>(FractalNoise(worldX / wander, worldZ / wander, 2, seed + 5u)) *
                                6.0 * static_cast<double>(grid.cell);
            grid.heights[grid.Index(x, z)] +=
                amplitude * FractalNoise((worldX + warp) / scale, (worldZ - warp) / scale, 3, seed);
        }
    });
}

// 熱侵蝕：安息角を超えた分を下へ落とす。V 字の谷底に岩屑が溜まって滑らかな斜面になる（体積は保存）。
void SettleTalus(LevelGrid& grid, const std::vector<float>& rock, uint32_t iterations, float talus) {
    constexpr float kRate = 0.2f;
    const uint32_t w = grid.width;
    const uint32_t h = grid.height;
    std::vector<float> next(grid.heights.size());
    for (uint32_t iteration = 0u; iteration < iterations; ++iteration) {
        ForEachRow(h, [&](uint32_t z) {
            for (uint32_t x = 0u; x < w; ++x) {
                const size_t i = grid.Index(x, z);
                if (x == 0u || z == 0u || x + 1u == w || z + 1u == h) {
                    next[i] = grid.heights[i];
                    continue;
                }
                const float limit = talus * grid.cell * (0.75f + 0.35f * rock[i]);
                const float center = grid.heights[i];
                float delta = 0.0f;
                for (const size_t neighbor : {i - 1u, i + 1u, i - w, i + w}) {
                    const float difference = grid.heights[neighbor] - center;
                    if (difference > limit) {
                        delta += difference - limit;
                    } else if (-difference > limit) {
                        delta += difference + limit;
                    }
                }
                next[i] = center + kRate * delta;
            }
        });
        grid.heights.swap(next);
    }
}

// 岩の強い所は水平の段になる。高さを少し揺らして段の線を波打たせる。
void AddLedges(LevelGrid& grid, const std::vector<float>& rock, float step, float amount, uint32_t seed) {
    if (amount <= 0.0f) {
        return;
    }
    ForEachRow(grid.height, [&](uint32_t z) {
        for (uint32_t x = 0u; x < grid.width; ++x) {
            const size_t i = grid.Index(x, z);
            const float mask = SmoothStep(1.0f, 1.8f, rock[i]) * amount;
            if (mask <= 0.0f) {
                continue;
            }
            const double worldX = static_cast<double>(x) * grid.cell;
            const double worldZ = static_cast<double>(z) * grid.cell;
            const float warped = grid.heights[i] + step * 1.5f * FractalNoise(worldX / 240.0, worldZ / 240.0, 3, seed);
            const float position = warped / step;
            const float whole = std::floor(position);
            const float t = position - whole;
            const float terrace = whole + t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
            grid.heights[i] += (terrace - position) * step * mask;
        }
    });
}

} // namespace

bool CarveValleys(ErosionField& field, const ValleySettings& requested, std::string& error) {
    if (!field.Valid()) {
        error = "the valley carving field is invalid";
        return false;
    }
    const ValleySettings settings = JSONFIELDS::Sanitize(requested, kValleyFields);
    constexpr float kCoarsestCell = 16.0f;
    constexpr float kFinestCell = 4.0f;
    const uint32_t step = (std::max)(static_cast<uint32_t>(std::lround(kCoarsestCell / field.spacingMeters)), 1u);
    LevelGrid grid{};
    grid.width = (field.width - 1u) / step + 1u;
    grid.height = (field.height - 1u) / step + 1u;
    grid.cell = field.spacingMeters * static_cast<float>(step);
    if (grid.width < 8u || grid.height < 8u) {
        error = "the area is too small for valley carving";
        return false;
    }
    grid.heights.resize(static_cast<size_t>(grid.width) * grid.height);
    for (uint32_t z = 0u; z < grid.height; ++z) {
        for (uint32_t x = 0u; x < grid.width; ++x) {
            grid.heights[grid.Index(x, z)] = field.heights[static_cast<size_t>(z * step) * field.width + x * step];
        }
    }
    const auto seed = static_cast<uint32_t>(settings.seed);
    const float lowest = *std::min_element(grid.heights.begin(), grid.heights.end());
    const float highest = *std::max_element(grid.heights.begin(), grid.heights.end());
    const float relief = (std::max)(highest - lowest, 1.0e-3f);
    // 大きな形の上に揺らぎを載せてから水を流す（高い所ほど大きく。麓は穏やかなまま）。
    ForEachRow(grid.height, [&](uint32_t z) {
        for (uint32_t x = 0u; x < grid.width; ++x) {
            const size_t i = grid.Index(x, z);
            const double worldX = static_cast<double>(x) * grid.cell;
            const double worldZ = static_cast<double>(z) * grid.cell;
            grid.heights[i] += 70.0f * settings.detail * (grid.heights[i] - lowest) / (std::max)(relief, 300.0f) *
                               FractalNoise(worldX / 160.0, worldZ / 160.0, 4, seed + 51u);
        }
    });
    const float erosion = settings.erosion;
    const float slope = settings.hillSlope;
    // 16 m：大きな谷と尾根。
    {
        const std::vector<float> rock = BuildRockMap(grid, settings.rockVariation, seed + 41u);
        SolveLevel(grid, rock, {60u, 0.012f * erosion, 2000.0f, slope, 0.02f, 12.0f, 0.85f}, seed + 100u);
    }
    // 8 m：枝の谷と、谷を埋める岩屑、岩の大きな段。
    if (grid.cell * 0.5f >= kFinestCell && grid.cell * 0.5f >= field.spacingMeters) {
        grid = DoubleResolution(grid);
        AddVariation(grid, 14.0f * settings.detail, seed + 200u);
        const std::vector<float> rock = BuildRockMap(grid, settings.rockVariation, seed + 41u);
        SolveLevel(grid, rock, {30u, 0.014f * erosion, 1500.0f, slope * 0.95f, 0.004f, 4.0f, 0.8f}, seed + 200u);
        SettleTalus(grid, rock, 12u, 0.8f);
        AddLedges(grid, rock, 34.0f, settings.ledges, seed + 250u);
    }
    // 4 m：まばらな沟と小さな段。これより細かい沟を彫ると、斜面が毛並みのような筋で埋まる。
    if (grid.cell * 0.5f >= kFinestCell && grid.cell * 0.5f >= field.spacingMeters) {
        grid = DoubleResolution(grid);
        AddVariation(grid, 7.0f * settings.detail, seed + 300u);
        const std::vector<float> rock = BuildRockMap(grid, settings.rockVariation, seed + 41u);
        SolveLevel(grid, rock, {16u, 0.004f * erosion, 3000.0f, slope * 0.9f, 0.002f, 0.0f, 0.7f}, seed + 300u);
        AddLedges(grid, rock, 13.0f, settings.ledges, seed + 350u);
        SettleTalus(grid, rock, 3u, 0.9f);
    }
    // 場の解像度へ戻し、岩の凹凸を載せる（斜面の限界で潰されないよう最後に）。斜面ほど強く、岩の強い所ほど荒い。
    const float ratio = field.spacingMeters / grid.cell;
    std::vector<float> smooth(field.heights.size());
    ForEachRow(field.height, [&](uint32_t z) {
        for (uint32_t x = 0u; x < field.width; ++x) {
            smooth[static_cast<size_t>(z) * field.width + x] =
                SampleCubic(grid, static_cast<float>(x) * ratio, static_cast<float>(z) * ratio);
        }
    });
    const auto height = static_cast<int>(field.height);
    const auto width = static_cast<int>(field.width);
    const int reach = (std::max)(static_cast<int>(std::lround(2.0f / field.spacingMeters)), 1);
    const float gradientScale = 0.5f / (static_cast<float>(reach) * field.spacingMeters);
    const float keepOriginal = 1.0f - settings.strength;
    ForEachRow(field.height, [&](uint32_t row) {
        const int z = static_cast<int>(row);
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(z) * field.width + static_cast<size_t>(x);
            const auto at = [&](int sampleX, int sampleZ) {
                return smooth[static_cast<size_t>(std::clamp(sampleZ, 0, height - 1)) * field.width +
                              static_cast<size_t>(std::clamp(sampleX, 0, width - 1))];
            };
            const float gradientX = (at(x + reach, z) - at(x - reach, z)) * gradientScale;
            const float gradientZ = (at(x, z + reach) - at(x, z - reach)) * gradientScale;
            const float steepness = std::sqrt(gradientX * gradientX + gradientZ * gradientZ);
            float rough = 0.0f;
            if (settings.roughness > 0.0f) {
                const double worldX = static_cast<double>(x) * field.spacingMeters;
                const double worldZ = static_cast<double>(z) * field.spacingMeters;
                const double warpedX = worldX + 18.0 * FractalNoise(worldX / 90.0, worldZ / 90.0, 2, seed + 460u);
                const double warpedZ = worldZ + 18.0 * FractalNoise(worldX / 90.0 + 5.0, worldZ / 90.0, 2, seed + 470u);
                const float strong = std::exp(settings.rockVariation * 2.6f *
                                              FractalNoise(worldX / 300.0, worldZ / 300.0, 5, seed + 41u));
                rough = settings.roughness *
                        (RidgedNoise(warpedX / 30.0, warpedZ / 30.0, 5, seed + 450u, 0.75f) - 0.3f) *
                        SmoothStep(0.25f, 0.8f, steepness) * (0.45f + 0.55f * SmoothStep(0.7f, 1.6f, strong));
            }
            const float carved = smooth[i] + rough;
            field.heights[i] = field.heights[i] * keepOriginal + carved * settings.strength;
        }
    });
    return true;
}

} // namespace HIKARI::TERRAIN::DATA
