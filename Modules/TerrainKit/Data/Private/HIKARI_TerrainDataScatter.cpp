#include "TerrainKit/Data/HIKARI_TerrainDataScatter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "HIKARI_TerrainDataNoise.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

constexpr float kRadiansToDegrees = 180.0f / std::numbers::pi_v<float>;
constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr double kProbeMeters = 2.0;
constexpr uint8_t kOccupiedFootprint = 1u;
constexpr uint8_t kOccupiedSpline = 2u;
constexpr uint8_t kOccupiedAny = kOccupiedFootprint | kOccupiedSpline;
// これより小さな足跡は 1 m の升では表せないので、大きな足跡を避けるだけで場所は取らない（小石や花が密に並べる）。
constexpr float kMinimumFootprintMeters = 0.5f;

float RangeWeight(float value, float minimum, float maximum, float fade, float domainMinimum,
                  float domainMaximum) noexcept {
    const float below = minimum <= domainMinimum ? 1.0f : std::clamp((value - minimum) / fade + 0.5f, 0.0f, 1.0f);
    const float above = maximum >= domainMaximum ? 1.0f : std::clamp((maximum - value) / fade + 0.5f, 0.0f, 1.0f);
    return below * above;
}

float HashUnit(uint32_t hash) noexcept { return static_cast<float>(hash >> 8u) / 16777216.0f; }

uint32_t Mix(uint32_t value) noexcept {
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16u);
}

// 瓦片と余白を 1 m の升で覆う占有図。先に場所を取ったものを塗り、後の規則はそこを避ける。
class Occupancy {
public:
    Occupancy(double originX, double originZ, double sizeMeters)
        : originX_(originX), originZ_(originZ), size_(static_cast<int>(sizeMeters) + 2) {
        cells_.assign(static_cast<size_t>(size_) * size_, 0u);
    }

    bool IsFree(double x, double z, float radius, uint8_t mask) const noexcept {
        bool free = true;
        Visit(x, z, radius, [&](size_t cell) { free = free && (cells_[cell] & mask) == 0u; });
        return free;
    }

    void Mark(double x, double z, float radius, uint8_t bit) noexcept {
        Visit(x, z, radius, [&](size_t cell) { cells_[cell] |= bit; });
    }

private:
    template <class Function>
    void Visit(double x, double z, float radius, Function function) const noexcept {
        const double localX = x - originX_;
        const double localZ = z - originZ_;
        const int centerColumn = static_cast<int>(std::floor(localX));
        const int centerRow = static_cast<int>(std::floor(localZ));
        const int x0 = (std::max)(static_cast<int>(std::floor(localX - radius)), 0);
        const int x1 = (std::min)(static_cast<int>(std::floor(localX + radius)), size_ - 1);
        const int z0 = (std::max)(static_cast<int>(std::floor(localZ - radius)), 0);
        const int z1 = (std::min)(static_cast<int>(std::floor(localZ + radius)), size_ - 1);
        const auto radiusSquared = static_cast<double>(radius) * radius;
        for (int row = z0; row <= z1; ++row) {
            const double dz = row + 0.5 - localZ;
            for (int column = x0; column <= x1; ++column) {
                const double dx = column + 0.5 - localX;
                // 自分の升は半径によらず見る（小さな足跡でも、大きな足跡の中へ入らないように）。
                if (dx * dx + dz * dz <= radiusSquared || (column == centerColumn && row == centerRow)) {
                    function(static_cast<size_t>(row) * size_ + column);
                }
            }
        }
    }

    double originX_;
    double originZ_;
    int size_;
    std::vector<uint8_t> cells_{};
};

// 一枚の瓦片を解く間の共通の入力。
class TileScatter {
public:
    TileScatter(const ScatterRuleSet& set, const ScatterTile& tile, std::vector<ScatterPoint>& out)
        : set_(set), tile_(tile), out_(out), samples_(tile.surface.samples),
          stride_(tile.surface.samples + 2u * kSurfaceRuleHeightMargin), spacing_(tile.surface.spacingMeters),
          tileX0_(static_cast<double>(tile.surface.firstSampleX) * spacing_),
          tileZ0_(static_cast<double>(tile.surface.firstSampleZ) * spacing_),
          tileSize_(static_cast<double>(tile.surface.samples - 1u) * spacing_),
          marginMeters_(static_cast<double>(kSurfaceRuleHeightMargin - 2u) * spacing_),
          occupancy_(tileX0_ - marginMeters_, tileZ0_ - marginMeters_, tileSize_ + 2.0 * marginMeters_) {}

    void Run() {
        for (const ScatterReserved& reserved : tile_.reserved) {
            occupancy_.Mark(reserved.x, reserved.z, reserved.radiusMeters, kOccupiedFootprint);
        }
        for (const PreparedSpline& spline : tile_.splines) {
            MarkSpline(spline);
        }
        for (uint32_t index = 0u; index < set_.lines.size(); ++index) {
            if (set_.lines[index].enabled) {
                EvaluateLine(index);
            }
        }
        for (uint32_t index = 0u; index < set_.rules.size(); ++index) {
            if (set_.rules[index].enabled && set_.rules[index].densityPer100 > 0.0f) {
                EvaluateArea(index);
            }
        }
    }

private:
    bool InRegion(double x, double z) const noexcept {
        return x >= tileX0_ - marginMeters_ && z >= tileZ0_ - marginMeters_ &&
               x < tileX0_ + tileSize_ + marginMeters_ && z < tileZ0_ + tileSize_ + marginMeters_;
    }

    bool InTile(double x, double z) const noexcept {
        return x >= tileX0_ && z >= tileZ0_ && x < tileX0_ + tileSize_ && z < tileZ0_ + tileSize_;
    }

    float HeightAt(double x, double z) const noexcept {
        const double fx = (x - tileX0_) / spacing_ + kSurfaceRuleHeightMargin;
        const double fz = (z - tileZ0_) / spacing_ + kSurfaceRuleHeightMargin;
        const auto last = static_cast<double>(stride_ - 2u);
        const auto column = static_cast<uint32_t>(std::clamp(fx, 0.0, last));
        const auto row = static_cast<uint32_t>(std::clamp(fz, 0.0, last));
        const auto tx = static_cast<float>(fx - column);
        const auto tz = static_cast<float>(fz - row);
        const float* top = tile_.surface.heights.data() + static_cast<size_t>(row) * stride_ + column;
        const float* bottom = top + stride_;
        const float upper = top[0] + (top[1] - top[0]) * tx;
        const float lower = bottom[0] + (bottom[1] - bottom[0]) * tx;
        return upper + (lower - upper) * tz;
    }

    size_t SampleIndex(double x, double z) const noexcept {
        const auto column = static_cast<uint32_t>((x - tileX0_) / spacing_ + 0.5);
        const auto row = static_cast<uint32_t>((z - tileZ0_) / spacing_ + 0.5);
        return static_cast<size_t>((std::min)(row, samples_ - 1u)) * samples_ + (std::min)(column, samples_ - 1u);
    }

    void MarkSpline(const PreparedSpline& spline) {
        const float edge = spline.settings.edgeMeters + set_.splineMarginMeters;
        float nextAlong = 0.0f;
        for (const SplineSection& section : spline.sections) {
            const float radius = section.halfWidth + edge;
            if (section.along >= nextAlong) {
                occupancy_.Mark(section.x, section.z, radius, kOccupiedSpline);
                nextAlong = section.along + radius * 0.5f;
            }
        }
    }

    void EvaluateLine(uint32_t lineIndex) {
        const ScatterLineRule& rule = set_.lines[lineIndex];
        const auto found = std::find(tile_.splineLayers.begin(), tile_.splineLayers.end(), rule.splineLayer);
        if (found == tile_.splineLayers.end() || rule.spacingMeters <= 0.0f) {
            return;
        }
        const PreparedSpline& spline = tile_.splines[static_cast<size_t>(found - tile_.splineLayers.begin())];
        if (spline.sections.size() < 2u) {
            return;
        }
        const uint32_t ruleSeed = Mix(rule.seed * 0x9E3779B9u + 0xA11CEu);
        const float length = spline.sections.back().along;
        const auto count = static_cast<int64_t>(length / rule.spacingMeters) + 1;
        size_t segment = 0u;
        for (int64_t step = 0; step < count; ++step) {
            const float along = static_cast<float>(step) * rule.spacingMeters;
            while (segment + 2u < spline.sections.size() && spline.sections[segment + 1u].along < along) {
                ++segment;
            }
            const SplineSection& a = spline.sections[segment];
            const SplineSection& b = spline.sections[segment + 1u];
            const float t = std::clamp((along - a.along) / (std::max)(b.along - a.along, 1.0e-4f), 0.0f, 1.0f);
            const float centerX = a.x + (b.x - a.x) * t;
            const float centerZ = a.z + (b.z - a.z) * t;
            const float halfWidth = a.halfWidth + (b.halfWidth - a.halfWidth) * t;
            const float tangentX = a.tangentX + (b.tangentX - a.tangentX) * t;
            const float tangentZ = a.tangentZ + (b.tangentZ - a.tangentZ) * t;
            // 右は進行方向の右手（+Y から見て時計回り）。
            const auto place = [&](float side, int64_t sideIndex) {
                const uint32_t hash = HashLattice(step, sideIndex, ruleSeed);
                const float jitter = rule.jitterMeters * (HashUnit(Mix(hash + 1u)) * 2.0f - 1.0f);
                const float jitterSide = rule.jitterMeters * (HashUnit(Mix(hash + 2u)) * 2.0f - 1.0f);
                const float offset = side != 0.0f ? side * (halfWidth + rule.offsetMeters) + jitterSide : jitterSide;
                const double x = static_cast<double>(centerX) + tangentZ * offset + tangentX * jitter;
                const double z = static_cast<double>(centerZ) - tangentX * offset + tangentZ * jitter;
                if (!InRegion(x, z)) {
                    return;
                }
                occupancy_.Mark(x, z, rule.footprintMeters, kOccupiedFootprint);
                if (!InTile(x, z)) {
                    return;
                }
                const float yawJitter =
                    (rule.yawJitterDegrees / kRadiansToDegrees) * (HashUnit(Mix(hash + 3u)) * 2.0f - 1.0f);
                ScatterPoint point{};
                point.rule = lineIndex;
                point.line = true;
                point.seed = Mix(hash + 4u);
                point.x = static_cast<float>(x);
                point.y = HeightAt(x, z);
                point.z = static_cast<float>(z);
                GroundAlignedRotation(std::atan2(tangentX, tangentZ) + yawJitter, 0.0f, 0.0f, 0.0f, point.rotation);
                point.scale = rule.scaleMin + (rule.scaleMax - rule.scaleMin) * HashUnit(Mix(hash + 5u));
                out_.push_back(point);
            };
            if (rule.side == ScatterLineSide::Center) {
                place(0.0f, 0);
            }
            if (rule.side == ScatterLineSide::Right || rule.side == ScatterLineSide::Both) {
                place(1.0f, 1);
            }
            if (rule.side == ScatterLineSide::Left || rule.side == ScatterLineSide::Both) {
                place(-1.0f, 2);
            }
        }
    }

    void EvaluateArea(uint32_t ruleIndex) {
        const ScatterRule& rule = set_.rules[ruleIndex];
        const std::vector<float>* mask = ruleIndex < tile_.ruleMasks.size() && !tile_.ruleMasks[ruleIndex].empty()
                                             ? &tile_.ruleMasks[ruleIndex]
                                             : nullptr;
        const uint8_t blocking = rule.avoidSplines ? kOccupiedAny : kOccupiedFootprint;
        const bool takesSpace = rule.footprintMeters >= kMinimumFootprintMeters;
        // 抖動格子：一升に一候補。升の大きさで最大の密度が決まり、同じ規則の個体どうしは揃った間隔を保つ。
        const double cell = std::sqrt(100.0 / static_cast<double>(rule.densityPer100));
        const auto firstColumn = static_cast<int64_t>(std::floor((tileX0_ - marginMeters_) / cell));
        const auto lastColumn = static_cast<int64_t>(std::floor((tileX0_ + tileSize_ + marginMeters_) / cell));
        const auto firstRow = static_cast<int64_t>(std::floor((tileZ0_ - marginMeters_) / cell));
        const auto lastRow = static_cast<int64_t>(std::floor((tileZ0_ + tileSize_ + marginMeters_) / cell));
        const uint32_t ruleSeed = Mix(rule.seed * 0x9E3779B9u + 0x51D3A7u);
        for (int64_t row = firstRow; row <= lastRow; ++row) {
            for (int64_t column = firstColumn; column <= lastColumn; ++column) {
                const uint32_t hash = HashLattice(column, row, ruleSeed);
                const double x = (static_cast<double>(column) + 0.15 + 0.7 * HashUnit(hash)) * cell;
                const double z = (static_cast<double>(row) + 0.15 + 0.7 * HashUnit(Mix(hash + 1u))) * cell;
                if (!InRegion(x, z)) {
                    continue;
                }
                const float height = HeightAt(x, z);
                const float slopeX = (HeightAt(x + kProbeMeters, z) - HeightAt(x - kProbeMeters, z)) /
                                     static_cast<float>(2.0 * kProbeMeters);
                const float slopeZ = (HeightAt(x, z + kProbeMeters) - HeightAt(x, z - kProbeMeters)) /
                                     static_cast<float>(2.0 * kProbeMeters);
                const float slope = std::atan(std::sqrt(slopeX * slopeX + slopeZ * slopeZ)) * kRadiansToDegrees;
                float density =
                    RangeWeight(slope, rule.minSlopeDegrees, rule.maxSlopeDegrees, rule.slopeFadeDegrees, 0.0f, 90.0f) *
                    RangeWeight(height, rule.minHeightMeters, rule.maxHeightMeters, rule.heightFadeMeters, -100000.0f,
                                100000.0f);
                if (density <= 0.0f) {
                    continue;
                }
                if (tile_.surface.analysis != nullptr) {
                    density *= RangeWeight(tile_.surface.analysis->WetnessAt(x, z), rule.minWetness, rule.maxWetness,
                                           rule.wetnessFade, 0.0f, 1.0f) *
                               RangeWeight(tile_.surface.analysis->PositionAt(x, z), rule.minPosition, rule.maxPosition,
                                           rule.positionFade, -1.0f, 1.0f);
                }
                if (rule.clusterAmount > 0.0f) {
                    const float noise =
                        FractalNoise(x / rule.clusterScaleMeters, z / rule.clusterScaleMeters, 3, ruleSeed ^ 0x5A17u);
                    density *= std::clamp(1.0f + rule.clusterAmount * (noise * 2.5f - 0.5f), 0.0f, 1.0f);
                }
                const bool inside = InTile(x, z);
                if (inside && mask != nullptr) {
                    density *= (*mask)[SampleIndex(x, z)];
                }
                if (density <= HashUnit(Mix(hash + 2u))) {
                    continue;
                }
                if (inside && rule.surfaceMask != 0xFFFFFFFFu && !tile_.control.empty()) {
                    const ControlTexel texel = UnpackControlTexel(tile_.control[SampleIndex(x, z)]);
                    if ((rule.surfaceMask & (1u << texel.baseMaterial)) == 0u) {
                        continue;
                    }
                }
                if (!occupancy_.IsFree(x, z, rule.footprintMeters, blocking)) {
                    continue;
                }
                if (takesSpace) {
                    occupancy_.Mark(x, z, rule.footprintMeters, kOccupiedFootprint);
                }
                if (!inside) {
                    continue;
                }
                ScatterPoint point{};
                point.rule = ruleIndex;
                point.seed = Mix(hash + 3u);
                point.x = static_cast<float>(x);
                point.y = height;
                point.z = static_cast<float>(z);
                GroundAlignedRotation(HashUnit(Mix(hash + 4u)) * kTwoPi, slopeX, slopeZ, rule.alignToGround,
                                      point.rotation);
                // 林縁（密度の低い所）ほど小さくする。
                const float fullness = std::clamp(density / 0.6f, 0.0f, 1.0f);
                point.scale = (rule.scaleMin + (rule.scaleMax - rule.scaleMin) * HashUnit(Mix(hash + 5u))) *
                              (rule.edgeScale + (1.0f - rule.edgeScale) * fullness);
                out_.push_back(point);
            }
        }
    }

    const ScatterRuleSet& set_;
    const ScatterTile& tile_;
    std::vector<ScatterPoint>& out_;
    uint32_t samples_;
    uint32_t stride_;
    float spacing_;
    double tileX0_;
    double tileZ0_;
    double tileSize_;
    double marginMeters_;
    Occupancy occupancy_;
};

} // namespace

ScatterRuleSet DefaultScatterRules() {
    constexpr uint32_t kGrass = 1u << 0u;
    constexpr uint32_t kSoil = 1u << 1u;
    constexpr uint32_t kRock = 1u << 2u;
    constexpr uint32_t kDryGrass = 1u << 5u;
    constexpr uint32_t kMeadow = 1u << 6u;
    constexpr uint32_t kStonyDirt = 1u << 7u;
    constexpr uint32_t kGravel = 1u << 8u;
    constexpr uint32_t kLayeredRock = 1u << 9u;
    ScatterRuleSet set{};
    ScatterRule forest{};
    forest.name = "Forest";
    forest.seed = 11u;
    forest.instanceSet = "Forest Trees";
    forest.densityPer100 = 2.5f;
    forest.footprintMeters = 2.0f;
    forest.scaleMin = 0.75f;
    forest.scaleMax = 1.3f;
    forest.edgeScale = 0.55f;
    forest.alignToGround = 0.1f;
    forest.maxSlopeDegrees = 36.0f;
    forest.maxHeightMeters = 430.0f;
    forest.heightFadeMeters = 60.0f;
    forest.surfaceMask = kGrass | kSoil | kDryGrass | kMeadow | kStonyDirt;
    forest.clusterAmount = 0.8f;
    forest.clusterScaleMeters = 140.0f;
    set.rules.push_back(forest);
    ScatterRule shrubs{};
    shrubs.name = "Shrubs";
    shrubs.seed = 23u;
    shrubs.instanceSet = "Shrubs";
    shrubs.densityPer100 = 3.0f;
    shrubs.footprintMeters = 0.8f;
    shrubs.scaleMin = 0.6f;
    shrubs.scaleMax = 1.2f;
    shrubs.edgeScale = 0.7f;
    shrubs.alignToGround = 0.6f;
    shrubs.maxSlopeDegrees = 42.0f;
    shrubs.maxHeightMeters = 520.0f;
    shrubs.heightFadeMeters = 50.0f;
    shrubs.surfaceMask = kGrass | kSoil | kDryGrass | kMeadow | kStonyDirt;
    shrubs.clusterScaleMeters = 40.0f;
    set.rules.push_back(shrubs);
    ScatterRule boulders{};
    boulders.name = "Boulders";
    boulders.seed = 37u;
    boulders.instanceSet = "Boulders";
    boulders.densityPer100 = 0.6f;
    boulders.footprintMeters = 1.5f;
    boulders.scaleMin = 0.4f;
    boulders.scaleMax = 2.2f;
    boulders.edgeScale = 0.8f;
    boulders.alignToGround = 1.0f;
    boulders.maxSlopeDegrees = 46.0f;
    boulders.surfaceMask = kRock | kStonyDirt | kGravel | kLayeredRock;
    boulders.clusterScaleMeters = 50.0f;
    set.rules.push_back(boulders);
    return set;
}

void EvaluateScatter(const ScatterRuleSet& set, const ScatterTile& tile, std::vector<ScatterPoint>& out) {
    out.clear();
    const uint32_t samples = tile.surface.samples;
    const uint32_t stride = samples + 2u * kSurfaceRuleHeightMargin;
    if (samples < 2u || tile.surface.heights.size() != static_cast<size_t>(stride) * stride) {
        return;
    }
    TileScatter(set, tile, out).Run();
}

void GroundAlignedRotation(float yawRadians, float slopeX, float slopeZ, float align, float out[4]) noexcept {
    const float halfYaw = yawRadians * 0.5f;
    const float spinY = std::sin(halfYaw);
    const float spinW = std::cos(halfYaw);
    const float axisLength = std::sqrt(slopeX * slopeX + slopeZ * slopeZ);
    if (axisLength <= 1.0e-4f || align <= 0.0f) {
        out[0] = 0.0f;
        out[1] = spinY;
        out[2] = 0.0f;
        out[3] = spinW;
        return;
    }
    // 軸は上向きと法線（-slopeX, 1, -slopeZ）の外積。
    const float halfTilt = std::atan(axisLength) * std::clamp(align, 0.0f, 1.0f) * 0.5f;
    const float sine = std::sin(halfTilt);
    const float leanX = -slopeZ / axisLength * sine;
    const float leanZ = slopeX / axisLength * sine;
    const float leanW = std::cos(halfTilt);
    // 傾き × 方位（y 成分だけの回転との積）。
    out[0] = leanX * spinW - leanZ * spinY;
    out[1] = leanW * spinY;
    out[2] = leanX * spinY + leanZ * spinW;
    out[3] = leanW * spinW;
}

} // namespace HIKARI::TERRAIN::DATA
