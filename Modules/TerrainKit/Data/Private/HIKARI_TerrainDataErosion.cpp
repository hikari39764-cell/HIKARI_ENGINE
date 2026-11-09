#include "TerrainKit/Data/HIKARI_TerrainDataErosion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>
#include <numeric>

namespace HIKARI::TERRAIN::DATA {
namespace {

constexpr float kGravity = 9.81f;
constexpr float kMinimumWaterDepth = 1.0e-5f;
constexpr float kMinimumLayerAlpha = 0.05f;
constexpr std::size_t kMaximumErosionSamples = std::size_t{1} << 24; // 一辺 4096 標本まで

// 行ごとに fn(z) を並列に呼ぶ。各行は自分の標本だけを書く（Jacobi 型）ので結果は決定的。
template <class Fn>
void ForEachRow(uint32_t height, Fn&& fn) {
    std::vector<uint32_t> rows(height);
    std::iota(rows.begin(), rows.end(), 0u);
    std::for_each(std::execution::par, rows.begin(), rows.end(), fn);
}

bool ValidateField(const ErosionField& field, std::string& error) {
    if (!field.Valid()) {
        error = "erosion field must be at least 3x3 finite samples within the per-operation limit";
        return false;
    }
    return true;
}

void PrepareMaps(const ErosionField& field, ErosionMaps* maps) {
    if (maps == nullptr) {
        return;
    }
    const std::size_t count = field.heights.size();
    for (std::vector<float>* map : {&maps->flow, &maps->deposition, &maps->wear}) {
        if (map->size() != count) {
            map->assign(count, 0.0f);
        }
    }
}

float FeatherWeight(uint32_t x, uint32_t z, uint32_t width, uint32_t height, uint32_t feather) noexcept {
    if (feather == 0u) {
        return 1.0f;
    }
    const uint32_t edge = (std::min)({x, z, width - 1u - x, height - 1u - z});
    const float t = std::clamp(static_cast<float>(edge) / static_cast<float>(feather), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

bool ErosionField::Valid() const noexcept {
    return width >= 3u && height >= 3u && static_cast<std::size_t>(width) * height <= kMaximumErosionSamples &&
           heights.size() == static_cast<std::size_t>(width) * height && std::isfinite(spacingMeters) &&
           spacingMeters > 0.0f &&
           std::all_of(heights.begin(), heights.end(), [](float h) { return std::isfinite(h); });
}

constexpr float kMinimumTransportVolume = 1.0e-7f;

// 水が薄い所では運べない（Jákó & Tóth 2011 の lmax）。薄い膜で流速の割り算が暴れて無理に削るのを防ぐ。
constexpr float kFullCapacityDepth = 0.01f;
constexpr float kDepositSlope = 0.67f; // tan 34 度

bool RunHydraulicErosion(ErosionField& field, const HydraulicErosionSettings& requested, ErosionMaps* maps,
                         std::string& error) {
    if (!ValidateField(field, error)) {
        return false;
    }
    const HydraulicErosionSettings s = JSONFIELDS::Sanitize(requested, kHydraulicErosionFields);
    PrepareMaps(field, maps);
    const uint32_t w = field.width;
    const uint32_t h = field.height;
    const std::size_t count = field.heights.size();
    const float l = field.spacingMeters;
    const float area = l * l; // 管の断面積と標本の面積を l^2 とする（Mei 2007 の lX = lY = l）
    const float dt = s.timeStep;
    std::vector<float>& b = field.heights;
    std::vector<float> bNext(count), water(count, 0.0f), waterNext(count), sediment(count, 0.0f), sedimentNext(count);
    std::vector<float> fluxL(count, 0.0f), fluxR(count, 0.0f), fluxT(count, 0.0f), fluxB(count, 0.0f);
    std::vector<float> velX(count, 0.0f), velZ(count, 0.0f), eroded(count, 0.0f);
    const auto at = [w](uint32_t x, uint32_t z) { return static_cast<std::size_t>(z) * w + x; };

    for (uint32_t iteration = 0u; iteration < s.iterations; ++iteration) {
        // 1. 降雨
        for (float& d : water) {
            d += dt * s.rainRate;
        }
        // 2. 水位差から 4 方向の流出量を更新し、水が足りなければ縮める（外周の外へは流さない）。
        ForEachRow(h, [&](uint32_t z) {
            for (uint32_t x = 0u; x < w; ++x) {
                const std::size_t i = at(x, z);
                const float surface = b[i] + water[i];
                const auto outflow = [&](float previous, bool exists, std::size_t n) {
                    if (!exists) {
                        return 0.0f;
                    }
                    return (std::max)(0.0f, previous + dt * area * kGravity * (surface - b[n] - water[n]) / l);
                };
                float left = outflow(fluxL[i], x > 0u, x > 0u ? i - 1u : i);
                float right = outflow(fluxR[i], x + 1u < w, i + 1u);
                float top = outflow(fluxT[i], z > 0u, z > 0u ? i - w : i);
                float bottom = outflow(fluxB[i], z + 1u < h, i + w);
                const float total = (left + right + top + bottom) * dt;
                if (total > 0.0f) {
                    const float k = (std::min)(1.0f, water[i] * area / total);
                    left *= k;
                    right *= k;
                    top *= k;
                    bottom *= k;
                }
                fluxL[i] = left;
                fluxR[i] = right;
                fluxT[i] = top;
                fluxB[i] = bottom;
            }
        });
        // 3. 水深と流速
        ForEachRow(h, [&](uint32_t z) {
            for (uint32_t x = 0u; x < w; ++x) {
                const std::size_t i = at(x, z);
                const float inLeft = x > 0u ? fluxR[i - 1u] : 0.0f;
                const float inRight = x + 1u < w ? fluxL[i + 1u] : 0.0f;
                const float inTop = z > 0u ? fluxB[i - w] : 0.0f;
                const float inBottom = z + 1u < h ? fluxT[i + w] : 0.0f;
                const float out = fluxL[i] + fluxR[i] + fluxT[i] + fluxB[i];
                const float next = (std::max)(0.0f, water[i] + dt * (inLeft + inRight + inTop + inBottom - out) / area);
                const float depth = 0.5f * (water[i] + next);
                const float passX = 0.5f * (inLeft - fluxL[i] + fluxR[i] - inRight);
                const float passZ = 0.5f * (inTop - fluxT[i] + fluxB[i] - inBottom);
                waterNext[i] = next;
                velX[i] = depth > kMinimumWaterDepth ? passX / (depth * l) : 0.0f;
                velZ[i] = depth > kMinimumWaterDepth ? passZ / (depth * l) : 0.0f;
                if (maps != nullptr) {
                    maps->flow[i] += std::sqrt(passX * passX + passZ * passZ) * dt;
                }
            }
        });
        water.swap(waterNext);
        // 4. 運搬能力と浸食・堆積（地面の傾き sin と流速の大きさに比例）
        ForEachRow(h, [&](uint32_t z) {
            for (uint32_t x = 0u; x < w; ++x) {
                const std::size_t i = at(x, z);
                const float gx = (b[at(x + 1u < w ? x + 1u : x, z)] - b[at(x > 0u ? x - 1u : x, z)]) / (2.0f * l);
                const float gz = (b[at(x, z + 1u < h ? z + 1u : z)] - b[at(x, z > 0u ? z - 1u : z)]) / (2.0f * l);
                const float slope2 = gx * gx + gz * gz;
                const float tilt = (std::max)(std::sqrt(slope2 / (1.0f + slope2)), s.minimumTilt);
                const float speed = std::sqrt(velX[i] * velX[i] + velZ[i] * velZ[i]);
                const float capacity =
                    s.sedimentCapacity * tilt * speed * std::clamp(water[i] / kFullCapacityDepth, 0.0f, 1.0f);
                float ground = b[i];
                float carried = sediment[i];
                if (capacity > carried) {
                    const float budget = (std::max)(0.0f, s.maximumErosionDepth - eroded[i]);
                    const float amount = (std::min)(s.dissolveRate * (capacity - carried) * dt, budget);
                    ground -= amount;
                    carried += amount;
                    eroded[i] += amount;
                    if (maps != nullptr) {
                        maps->wear[i] += amount;
                    }
                } else {
                    // 周りより安息角を超えて高く積もらせない（一点に集まって尖った山にならないように）。
                    const float lowest = (std::min)({b[at(x > 0u ? x - 1u : x, z)], b[at(x + 1u < w ? x + 1u : x, z)],
                                                     b[at(x, z > 0u ? z - 1u : z)], b[at(x, z + 1u < h ? z + 1u : z)]});
                    const float room = (std::max)(0.0f, lowest + kDepositSlope * l - ground);
                    const float amount = (std::min)({s.depositRate * (carried - capacity) * dt, carried, room});
                    ground += amount;
                    carried -= amount;
                    if (maps != nullptr) {
                        maps->deposition[i] += amount;
                    }
                }
                bNext[i] = ground;
                sediment[i] = carried;
            }
        });
        b.swap(bNext);
        // 5. 堆積物を水と一緒に運ぶ：管の流量の割合で隣へ配る。半ラグランジュ移流と違って総量が保存され、
        //    流れの出口で堆積物が増えて尖った山ができない。
        const auto carried = [&](std::size_t n, float flux) {
            const float out = fluxL[n] + fluxR[n] + fluxT[n] + fluxB[n];
            const float volume = water[n] * area + out * dt;
            return volume > kMinimumTransportVolume ? sediment[n] * flux * dt / volume : 0.0f;
        };
        ForEachRow(h, [&](uint32_t z) {
            for (uint32_t x = 0u; x < w; ++x) {
                const std::size_t i = at(x, z);
                float next = sediment[i] - carried(i, fluxL[i] + fluxR[i] + fluxT[i] + fluxB[i]);
                next += x > 0u ? carried(i - 1u, fluxR[i - 1u]) : 0.0f;
                next += x + 1u < w ? carried(i + 1u, fluxL[i + 1u]) : 0.0f;
                next += z > 0u ? carried(i - w, fluxB[i - w]) : 0.0f;
                next += z + 1u < h ? carried(i + w, fluxT[i + w]) : 0.0f;
                sedimentNext[i] = (std::max)(next, 0.0f);
            }
        });
        sediment.swap(sedimentNext);
        // 6. 蒸発
        const float keep = (std::max)(0.0f, 1.0f - s.evaporationRate * dt);
        for (float& d : water) {
            d *= keep;
        }
    }
    // 最後に運ばれている堆積物はその場に落とす（質量を消さない）。
    for (std::size_t i = 0u; i < count; ++i) {
        b[i] += sediment[i];
        if (maps != nullptr) {
            maps->deposition[i] += sediment[i];
        }
    }
    return true;
}

bool RunThermalErosion(ErosionField& field, const ThermalErosionSettings& requested, ErosionMaps* maps,
                       std::string& error) {
    if (!ValidateField(field, error)) {
        return false;
    }
    const ThermalErosionSettings s = JSONFIELDS::Sanitize(requested, kThermalErosionFields);
    PrepareMaps(field, maps);
    const uint32_t w = field.width;
    const uint32_t h = field.height;
    const std::size_t count = field.heights.size();
    constexpr std::array<int, 8> kDx = {-1, 0, 1, -1, 1, -1, 0, 1};
    constexpr std::array<int, 8> kDz = {-1, -1, -1, 0, 0, 1, 1, 1};
    const float talus = std::tan(s.talusAngleDegrees * 3.14159265358979f / 180.0f);
    std::array<float, 8> threshold{};
    for (std::size_t n = 0u; n < 8u; ++n) {
        threshold[n] = talus * field.spacingMeters * ((kDx[n] != 0 && kDz[n] != 0) ? 1.41421356f : 1.0f);
    }
    std::vector<float>& heights = field.heights;
    std::array<std::vector<float>, 8> outflow;
    for (std::vector<float>& lane : outflow) {
        lane.assign(count, 0.0f);
    }
    std::vector<float> next(count);
    for (uint32_t iteration = 0u; iteration < s.iterations; ++iteration) {
        // 1. 各標本が 8 近傍へ出す量（安息角を超えた分の半分 × rate を超過の比で配る）
        ForEachRow(h, [&](uint32_t z) {
            for (uint32_t x = 0u; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(z) * w + x;
                std::array<float, 8> excess{};
                float total = 0.0f;
                float largest = 0.0f;
                for (std::size_t n = 0u; n < 8u; ++n) {
                    const int64_t nx = static_cast<int64_t>(x) + kDx[n];
                    const int64_t nz = static_cast<int64_t>(z) + kDz[n];
                    if (nx < 0 || nz < 0 || nx >= w || nz >= h) {
                        continue;
                    }
                    const float drop =
                        heights[i] - heights[static_cast<std::size_t>(nz) * w + static_cast<std::size_t>(nx)];
                    excess[n] = (std::max)(0.0f, drop - threshold[n]);
                    total += excess[n];
                    largest = (std::max)(largest, excess[n]);
                }
                const float moved = s.rate * 0.5f * largest;
                for (std::size_t n = 0u; n < 8u; ++n) {
                    outflow[n][i] = total > 0.0f ? moved * excess[n] / total : 0.0f;
                }
            }
        });
        // 2. 出した分を引き、近傍から来た分を足す（総量は保存）
        ForEachRow(h, [&](uint32_t z) {
            for (uint32_t x = 0u; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(z) * w + x;
                float out = 0.0f;
                float in = 0.0f;
                for (std::size_t n = 0u; n < 8u; ++n) {
                    out += outflow[n][i];
                    const int64_t nx = static_cast<int64_t>(x) + kDx[n];
                    const int64_t nz = static_cast<int64_t>(z) + kDz[n];
                    if (nx >= 0 && nz >= 0 && nx < w && nz < h) {
                        in += outflow[7u - n][static_cast<std::size_t>(nz) * w + static_cast<std::size_t>(nx)];
                    }
                }
                next[i] = heights[i] - out + in;
                if (maps != nullptr) {
                    maps->wear[i] += out;
                    maps->deposition[i] += in;
                }
            }
        });
        heights.swap(next);
    }
    return true;
}

bool PrepareErosionInput(const TerrainData& data, uint32_t layerId, const SampleRect& rect, ErosionInput& out,
                         std::string& error) {
    const HeightLayer* layer = data.FindLayer(layerId);
    if (layer == nullptr || data.LayerValues(layerId) == nullptr ||
        (layer->blend != HeightBlendMode::Add && layer->blend != HeightBlendMode::Subtract)) {
        error = "erosion writes into an existing sculpt layer with add or subtract blending";
        return false;
    }
    if (rect.width < 3u || rect.height < 3u || rect.Count() > kMaximumErosionSamples) {
        error = "erosion rectangle must be 3..4096 samples per edge";
        return false;
    }
    out.field.width = rect.width;
    out.field.height = rect.height;
    out.field.spacingMeters = static_cast<float>(SampleSpacing(data.Grid()));
    HeightStackEvaluator evaluator(data);
    if (!evaluator.Valid()) {
        error = evaluator.Error();
        return false;
    }
    std::vector<float> layerValues{};
    return evaluator.EvaluateRect(rect, out.field.heights, &error, layerId) &&
           evaluator.EvaluateLayerValues(layerId, rect, layerValues, out.alpha, &error);
}

bool ApplyErosionToLayer(TerrainData& data, uint32_t layerId, const SampleRect& rect, const ErosionRequest& requested,
                         TerrainEditRecorder* recorder, ErosionMaps* maps, std::string& error) {
    ErosionInput input{};
    if (!PrepareErosionInput(data, layerId, rect, input, error)) {
        return false;
    }
    const ErosionRequest request = JSONFIELDS::Sanitize(requested, kErosionRequestFields);
    ErosionField field = input.field;
    if (maps != nullptr) {
        *maps = {};
    }
    // 水力（谷を刻む）→ 熱力（水が残した急な所を安息角まで崩す）の順。
    if (request.hydraulic && !RunHydraulicErosion(field, request.hydraulicSettings, maps, error)) {
        return false;
    }
    if (request.thermal && !RunThermalErosion(field, request.thermalSettings, maps, error)) {
        return false;
    }
    return CommitErosionResult(data, layerId, rect, request, input, field.heights, recorder, error);
}

bool CommitErosionResult(TerrainData& data, uint32_t layerId, const SampleRect& rect, const ErosionRequest& request,
                         const ErosionInput& input, const std::vector<float>& eroded, TerrainEditRecorder* recorder,
                         std::string& error) {
    const HeightLayer* layer = data.FindLayer(layerId);
    TiledRaster<float>* values = data.LayerValues(layerId);
    if (layer == nullptr || values == nullptr || eroded.size() != rect.Count() ||
        input.field.heights.size() != rect.Count() || input.alpha.size() != rect.Count()) {
        error = "the erosion result does not match its sculpt layer";
        return false;
    }
    const TerrainGrid& grid = data.Grid();
    if (recorder != nullptr) {
        recorder->TouchRect(TerrainChannel::LayerValues, layerId, rect);
    }
    std::vector<float> current;
    values->ReadRect(grid, rect, current);
    // 変化の無い標本は書かない（羽根の外周だけが掛かる隣のタイルを作らない）。
    std::vector<uint8_t> keep(current.size(), 0u);
    const float sign = layer->blend == HeightBlendMode::Subtract ? -1.0f : 1.0f;
    for (uint32_t z = 0u; z < rect.height; ++z) {
        for (uint32_t x = 0u; x < rect.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(z) * rect.width + x;
            const float feather = FeatherWeight(x, z, rect.width, rect.height, request.featherSamples);
            const float delta =
                sign * feather * (eroded[i] - input.field.heights[i]) / (std::max)(input.alpha[i], kMinimumLayerAlpha);
            current[i] += delta;
            keep[i] = delta != 0.0f ? 1u : 0u;
        }
    }
    values->WriteRect(grid, rect, current, &keep);
    data.MarkChanged();
    return true;
}

} // namespace HIKARI::TERRAIN::DATA
