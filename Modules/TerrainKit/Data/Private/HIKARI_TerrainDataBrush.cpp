#include "TerrainKit/Data/HIKARI_TerrainDataBrush.h"

#include <algorithm>
#include <cmath>
#include <execution>
#include <memory>
#include <numeric>

#include "HIKARI_TerrainDataNoise.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

// 一本の筆跡が置ける dab の上限（暴走した入力で止まらなくならないように）。
constexpr std::size_t kMaximumBrushDabs = 100000u;
// 加算の層で合成後の高さを目標へ寄せる時、層の不透明度で割る下限。
constexpr float kMinimumLayerAlpha = 0.05f;

bool FinitePath(std::span<const MATH::Vec2> path) noexcept {
    return std::all_of(path.begin(), path.end(),
                       [](const MATH::Vec2& p) { return std::isfinite(p.x) && std::isfinite(p.y); });
}

// 行を並列に処理する（半径の大きい筆でも一フレームに収める）。行どうしは同じ標本へ書かない。
template <class RowFunction>
void ForEachRow(uint32_t rows, RowFunction function) {
    static thread_local std::vector<uint32_t> indices{};
    indices.resize(rows);
    std::iota(indices.begin(), indices.end(), 0u);
    std::for_each(std::execution::par, indices.begin(), indices.end(), function);
}

// 筆の形を (-1..1, -1..1) で読む（双線形）。
float SampleBrushShape(const BrushShape& shape, float u, float v) noexcept {
    const float scale = static_cast<float>(shape.size - 1u);
    const float x = std::clamp((u * 0.5f + 0.5f) * scale, 0.0f, scale);
    const float y = std::clamp((v * 0.5f + 0.5f) * scale, 0.0f, scale);
    const uint32_t x0 = static_cast<uint32_t>(x);
    const uint32_t y0 = static_cast<uint32_t>(y);
    const uint32_t x1 = (std::min)(x0 + 1u, shape.size - 1u);
    const uint32_t y1 = (std::min)(y0 + 1u, shape.size - 1u);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const auto at = [&](uint32_t column, uint32_t row) { return shape.values[row * shape.size + column]; };
    const float top = at(x0, y0) + (at(x1, y0) - at(x0, y0)) * fx;
    const float bottom = at(x0, y1) + (at(x1, y1) - at(x0, y1)) * fx;
    return top + (bottom - top) * fy;
}

// dab ごとの形の回し方（黄金角で並びを崩す）。
float DabRotation(std::size_t index) noexcept { return static_cast<float>(index) * 2.39996f; }

// dab 1 つの重み（矩形の標本ごと）。全て 0 なら false。
bool DabWeights(const TerrainGrid& grid, const SampleRect& rect, MATH::Vec2 center, float radius, float hardness,
                std::vector<float>& weights, const BrushShape* shape = nullptr, float rotation = 0.0f) {
    const double spacing = SampleSpacing(grid);
    const float cosine = std::cos(rotation);
    const float sine = std::sin(rotation);
    const bool shaped = shape != nullptr && shape->size > 1u && shape->values.size() == shape->size * shape->size;
    weights.assign(rect.Count(), 0.0f);
    ForEachRow(rect.height, [&](uint32_t z) {
        const double dz = static_cast<double>(rect.z0 + z) * spacing - center.y;
        for (uint32_t x = 0u; x < rect.width; ++x) {
            const double dx = static_cast<double>(rect.x0 + x) * spacing - center.x;
            const float distance = static_cast<float>(std::sqrt(dx * dx + dz * dz)) / radius;
            float weight = BrushFalloff(distance, hardness);
            if (shaped && weight > 0.0f) {
                const float u = static_cast<float>(dx) / radius;
                const float v = static_cast<float>(dz) / radius;
                weight *= SampleBrushShape(*shape, u * cosine - v * sine, u * sine + v * cosine);
            }
            weights[static_cast<std::size_t>(z) * rect.width + x] = weight;
        }
    });
    return std::any_of(weights.begin(), weights.end(), [](float weight) { return weight > 0.0f; });
}

// 矩形内の 3×3 加重平均（1 2 1）。矩形の端は内側の標本だけで平均する。
float Blur3(const std::vector<float>& values, const SampleRect& rect, uint32_t x, uint32_t z) noexcept {
    float sum = 0.0f;
    float weight = 0.0f;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int64_t sx = static_cast<int64_t>(x) + dx;
            const int64_t sz = static_cast<int64_t>(z) + dz;
            if (sx < 0 || sz < 0 || sx >= rect.width || sz >= rect.height) {
                continue;
            }
            const float w = static_cast<float>((dx == 0 ? 2 : 1) * (dz == 0 ? 2 : 1));
            sum += values[static_cast<std::size_t>(sz) * rect.width + static_cast<std::size_t>(sx)] * w;
            weight += w;
        }
    }
    return sum / weight;
}

// 傾き（度）と高さの範囲の重み。範囲の縁から少し外まで緩やかに 0 へ落とす。
constexpr float kLimitSlopeFadeDegrees = 4.0f;
constexpr float kLimitHeightFadeMeters = 4.0f;

float RangeWeight(float value, float minimum, float maximum, float fade) noexcept {
    const float below = std::clamp((value - (minimum - fade)) / fade, 0.0f, 1.0f);
    const float above = std::clamp(((maximum + fade) - value) / fade, 0.0f, 1.0f);
    return below * above;
}

float SlopeDegrees(const std::vector<float>& heights, const SampleRect& rect, uint32_t x, uint32_t z,
                   double spacing) noexcept {
    const uint32_t xa = x > 0u ? x - 1u : x;
    const uint32_t xb = x + 1u < rect.width ? x + 1u : x;
    const uint32_t za = z > 0u ? z - 1u : z;
    const uint32_t zb = z + 1u < rect.height ? z + 1u : z;
    const std::size_t stride = rect.width;
    const float dx = (heights[z * stride + xb] - heights[z * stride + xa]) /
                     (static_cast<float>(xb - xa) * static_cast<float>(spacing) + 1e-6f);
    const float dz = (heights[zb * stride + x] - heights[za * stride + x]) /
                     (static_cast<float>(zb - za) * static_cast<float>(spacing) + 1e-6f);
    return std::atan(std::sqrt(dx * dx + dz * dz)) * 57.29578f;
}

std::vector<uint8_t> KeepMask(const std::vector<float>& weights) {
    std::vector<uint8_t> keep(weights.size());
    for (std::size_t i = 0u; i < weights.size(); ++i) {
        keep[i] = weights[i] > 0.0f ? 1u : 0u;
    }
    return keep;
}

} // namespace

float BrushFalloff(float normalizedDistance, float hardness) noexcept {
    if (!(normalizedDistance < 1.0f)) {
        return 0.0f;
    }
    const float inner = std::clamp(hardness, 0.0f, 0.999f);
    if (normalizedDistance <= inner) {
        return 1.0f;
    }
    const float t = (normalizedDistance - inner) / (1.0f - inner);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

std::vector<MATH::Vec2> PlaceBrushDabs(std::span<const MATH::Vec2> path, float radiusMeters, float spacing) {
    std::vector<MATH::Vec2> dabs;
    if (path.empty()) {
        return dabs;
    }
    const float step = (std::max)(radiusMeters * spacing, 0.01f);
    dabs.push_back(path.front());
    float remaining = step;
    for (std::size_t i = 1u; i < path.size() && dabs.size() < kMaximumBrushDabs; ++i) {
        const MATH::Vec2 a = path[i - 1u];
        const MATH::Vec2 segment = path[i] - a;
        const float length = std::sqrt(segment.x * segment.x + segment.y * segment.y);
        if (!(length > 0.0f)) {
            continue;
        }
        float travelled = 0.0f;
        while (length - travelled >= remaining && dabs.size() < kMaximumBrushDabs) {
            travelled += remaining;
            dabs.push_back(a + segment * (travelled / length));
            remaining = step;
        }
        remaining -= length - travelled;
    }
    return dabs;
}

bool ApplyHeightBrush(TerrainData& data, uint32_t layerId, const HeightBrushSettings& requested,
                      std::span<const MATH::Vec2> path, TerrainEditRecorder* recorder, BrushEditResult& result,
                      std::string& error, const BrushShape* shape) {
    result = {};
    const HeightLayer* layer = data.FindLayer(layerId);
    TiledRaster<float>* values = data.LayerValues(layerId);
    if (layer == nullptr || values == nullptr) {
        error = "height brush needs an existing sculpt layer";
        return false;
    }
    if (path.empty() || !FinitePath(path)) {
        error = "height brush path is empty or not finite";
        return false;
    }
    const HeightBrushSettings settings = JSONFIELDS::Sanitize(requested, kHeightBrushFields);
    const TerrainGrid& grid = data.Grid();
    const bool additive = layer->blend == HeightBlendMode::Add || layer->blend == HeightBlendMode::Subtract;
    // 減算の層は値の向きが逆なので、全ての操作で符号を合わせる（合成後の高さが筆刷の向きに動く）。
    const float sign = layer->blend == HeightBlendMode::Subtract ? -1.0f : 1.0f;
    const bool limited = settings.limitMinSlopeDegrees > 0.0f || settings.limitMaxSlopeDegrees < 90.0f ||
                         settings.limitMinHeightMeters > -100000.0f || settings.limitMaxHeightMeters < 100000.0f;
    const bool towardTarget = settings.op == HeightBrushOp::Smooth || settings.op == HeightBrushOp::Flatten ||
                              settings.op == HeightBrushOp::Ramp || settings.op == HeightBrushOp::Terrace;
    const bool needsComposite = limited || (additive && towardTarget);
    const float fraction = std::clamp(settings.strength, 0.0f, 1.0f);
    const double spacing = SampleSpacing(grid);
    const std::vector<MATH::Vec2> dabs = PlaceBrushDabs(path, settings.radiusMeters, settings.spacing);
    // 評価器は層の画素を参照で読むので、dab ごとの書き込みが次の dab の合成に入る。
    std::unique_ptr<HeightStackEvaluator> evaluator;
    if (needsComposite) {
        evaluator = std::make_unique<HeightStackEvaluator>(data);
        if (!evaluator->Valid()) {
            error = evaluator->Error();
            return false;
        }
    }
    std::vector<float> weights, current, composite, layerValues, alpha;
    // 坂の始点と終点の高さ。一本の筆跡の間は動かさない。
    const MATH::Vec2 rampStart = path.front();
    const MATH::Vec2 rampAxis = path.back() - path.front();
    const float rampLength = std::sqrt(rampAxis.x * rampAxis.x + rampAxis.y * rampAxis.y);
    float rampStartHeight = 0.0f;
    float rampEndHeight = 0.0f;
    if (settings.op == HeightBrushOp::Ramp) {
        const auto groundAt = [&](MATH::Vec2 point, float& out) {
            const SampleRect one = SampleRectAround(grid, point.x, point.y, 0.0f);
            const SampleRect sample{one.x0, one.z0, 1u, 1u};
            std::vector<float> height{};
            if (additive) {
                if (!evaluator->EvaluateRect(sample, height, &error, layerId)) {
                    return false;
                }
            } else {
                values->ReadRect(grid, sample, height);
            }
            out = height.empty() ? 0.0f : height.front();
            return true;
        };
        if (!groundAt(rampStart, rampStartHeight) || !groundAt(path.back(), rampEndHeight)) {
            return false;
        }
        if (settings.rampSlopeDegrees != 0.0f) {
            rampEndHeight = rampStartHeight + rampLength * std::tan(settings.rampSlopeDegrees * 0.0174533f);
        }
    }
    std::size_t dabIndex = 0u;
    for (const MATH::Vec2& dab : dabs) {
        const SampleRect rect = SampleRectAround(grid, dab.x, dab.y, settings.radiusMeters);
        if (rect.Count() > kTerrainMaximumRectSamples) {
            error = "height brush radius covers too many samples";
            return false;
        }
        if (!DabWeights(grid, rect, dab, settings.radiusMeters, settings.hardness, weights, shape,
                        DabRotation(dabIndex++))) {
            continue;
        }
        if (recorder != nullptr) {
            recorder->TouchRect(TerrainChannel::LayerValues, layerId, rect);
        }
        values->ReadRect(grid, rect, current);
        if (needsComposite && (!evaluator->EvaluateRect(rect, composite, &error, layerId) ||
                               !evaluator->EvaluateLayerValues(layerId, rect, layerValues, alpha, &error))) {
            return false;
        }
        std::vector<float> next = current;
        ForEachRow(rect.height, [&](uint32_t z) {
            for (uint32_t x = 0u; x < rect.width; ++x) {
                const std::size_t i = static_cast<std::size_t>(z) * rect.width + x;
                float w = weights[i];
                if (w <= 0.0f) {
                    continue;
                }
                if (limited) {
                    w *= RangeWeight(SlopeDegrees(composite, rect, x, z, spacing), settings.limitMinSlopeDegrees,
                                     settings.limitMaxSlopeDegrees, kLimitSlopeFadeDegrees) *
                         RangeWeight(composite[i], settings.limitMinHeightMeters, settings.limitMaxHeightMeters,
                                     kLimitHeightFadeMeters);
                    if (w <= 0.0f) {
                        continue;
                    }
                }
                // 合成後の高さ（加算の層）か層の値を target へ寄せる。
                const auto moveToward = [&](float compositeTarget, float layerTarget) {
                    if (additive) {
                        next[i] += sign * fraction * w * (compositeTarget - composite[i]) /
                                   (std::max)(alpha[i], kMinimumLayerAlpha);
                    } else {
                        next[i] += fraction * w * (layerTarget - current[i]);
                    }
                };
                switch (settings.op) {
                case HeightBrushOp::Raise: next[i] += sign * settings.strength * w; break;
                case HeightBrushOp::Lower: next[i] -= sign * settings.strength * w; break;
                case HeightBrushOp::Noise: {
                    const double sx = static_cast<double>(rect.x0 + x) * spacing / settings.noiseScaleMeters;
                    const double sz = static_cast<double>(rect.z0 + z) * spacing / settings.noiseScaleMeters;
                    next[i] += sign * settings.strength * w * LatticeValueNoise(sx, sz, settings.noiseSeed);
                    break;
                }
                case HeightBrushOp::Flatten:
                    moveToward(settings.targetHeightMeters, settings.targetHeightMeters);
                    break;
                case HeightBrushOp::Smooth:
                    moveToward(additive ? Blur3(composite, rect, x, z) : 0.0f,
                               additive ? 0.0f : Blur3(current, rect, x, z));
                    break;
                case HeightBrushOp::Ramp: {
                    const float px = static_cast<float>(static_cast<double>(rect.x0 + x) * spacing) - rampStart.x;
                    const float pz = static_cast<float>(static_cast<double>(rect.z0 + z) * spacing) - rampStart.y;
                    const float t =
                        rampLength > 0.0f
                            ? std::clamp((px * rampAxis.x + pz * rampAxis.y) / (rampLength * rampLength), 0.0f, 1.0f)
                            : 0.0f;
                    const float height = rampStartHeight + (rampEndHeight - rampStartHeight) * t;
                    moveToward(height, height);
                    break;
                }
                case HeightBrushOp::Terrace: {
                    const float step = settings.terraceStepMeters;
                    if (additive) {
                        moveToward(std::round(composite[i] / step) * step, 0.0f);
                    } else {
                        moveToward(0.0f, std::round(current[i] / step) * step);
                    }
                    break;
                }
                }
            }
        });
        const std::vector<uint8_t> keep = KeepMask(weights);
        values->WriteRect(grid, rect, next, &keep);
        result.touched = UnionRects(result.touched, rect);
        ++result.dabs;
    }
    data.MarkChanged();
    return true;
}

bool ApplyControlBrush(TerrainData& data, const ControlBrushSettings& requested, std::span<const MATH::Vec2> path,
                       TerrainEditRecorder* recorder, BrushEditResult& result, std::string& error,
                       const BrushShape* shape) {
    result = {};
    if (path.empty() || !FinitePath(path)) {
        error = "control brush path is empty or not finite";
        return false;
    }
    const ControlBrushSettings settings = JSONFIELDS::Sanitize(requested, kControlBrushFields);
    const TerrainGrid& grid = data.Grid();
    TiledRaster<uint32_t>& control = data.Control();
    std::vector<float> weights;
    std::vector<uint32_t> texels;
    std::size_t dabIndex = 0u;
    for (const MATH::Vec2& dab : PlaceBrushDabs(path, settings.radiusMeters, settings.spacing)) {
        const SampleRect rect = SampleRectAround(grid, dab.x, dab.y, settings.radiusMeters);
        if (rect.Count() > kTerrainMaximumRectSamples) {
            error = "control brush radius covers too many samples";
            return false;
        }
        if (!DabWeights(grid, rect, dab, settings.radiusMeters, settings.hardness, weights, shape,
                        DabRotation(dabIndex++))) {
            continue;
        }
        if (recorder != nullptr) {
            recorder->TouchRect(TerrainChannel::Control, 0u, rect);
        }
        control.ReadRect(grid, rect, texels);
        for (std::size_t i = 0u; i < texels.size(); ++i) {
            const float w = weights[i] * settings.strength;
            if (weights[i] <= 0.0f) {
                continue;
            }
            ControlTexel texel = UnpackControlTexel(texels[i]);
            switch (settings.op) {
            case ControlBrushOp::Paint:
                texel = PaintControlMaterial(texel, static_cast<uint8_t>(settings.material), w);
                break;
            case ControlBrushOp::Hole: texel.hole = texel.hole || w >= 0.5f; break;
            case ControlBrushOp::Fill: texel.hole = texel.hole && w < 0.5f; break;
            case ControlBrushOp::Unlock: texel.handPainted = texel.handPainted && w < 0.5f; break;
            }
            PackControlTexel(texel, texels[i]);
        }
        const std::vector<uint8_t> keep = KeepMask(weights);
        control.WriteRect(grid, rect, texels, &keep);
        result.touched = UnionRects(result.touched, rect);
        ++result.dabs;
    }
    data.MarkChanged();
    return true;
}

} // namespace HIKARI::TERRAIN::DATA
