#include "TerrainKit/Data/HIKARI_TerrainDataRules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>
#include <numeric>

#include "HIKARI_TerrainDataNoise.h"
#include "TerrainKit/Data/HIKARI_TerrainDataControl.h"

namespace HIKARI::TERRAIN::DATA {
namespace {

constexpr float kRadiansToDegrees = 57.29578f;
constexpr float kDegreesToRadians = 0.0174532925f;
constexpr int kConcavityReach = 8;
// 傾きは 2 標本先との差で見る（1 m 間隔の細かい起伏で材質がちらつかないように）。
constexpr int kSlopeReach = 2;

// 値の取りうる端に置いた限界は「限界なし」（端の標本が半分の強さにならないように）。
float RangeWeight(float value, float minimum, float maximum, float fade, float domainMinimum,
                  float domainMaximum) noexcept {
    const float below = minimum <= domainMinimum ? 1.0f : std::clamp((value - minimum) / fade + 0.5f, 0.0f, 1.0f);
    const float above = maximum >= domainMaximum ? 1.0f : std::clamp((maximum - value) / fade + 0.5f, 0.0f, 1.0f);
    return below * above;
}

} // namespace

SurfaceRuleSet DefaultSurfaceRules() {
    SurfaceRuleSet set{};
    SurfaceRule dry{};
    dry.name = "Dry grass on exposed ground";
    dry.surface = 5u;
    dry.strength = 0.85f;
    dry.maxWetness = 0.45f;
    dry.wetnessFade = 0.3f;
    dry.maxPosition = 0.3f;
    dry.positionFade = 0.6f;
    dry.facingDegrees = 180.0f;
    dry.facingAmount = 0.3f;
    dry.maxSlopeDegrees = 50.0f;
    dry.slopeFadeDegrees = 12.0f;
    dry.noiseAmount = 0.0f;
    dry.coverage = 0.7f;
    dry.coverageScaleMeters = 300.0f;
    dry.coverageSoftness = 0.5f;
    set.rules.push_back(dry);
    SurfaceRule meadow{};
    meadow.name = "Meadow in wet hollows";
    meadow.surface = 6u;
    meadow.minWetness = 0.6f;
    meadow.wetnessFade = 0.25f;
    meadow.minPosition = 0.05f;
    meadow.positionFade = 0.5f;
    meadow.maxSlopeDegrees = 28.0f;
    meadow.slopeFadeDegrees = 10.0f;
    meadow.maxHeightMeters = 520.0f;
    meadow.heightFadeMeters = 120.0f;
    meadow.noiseAmount = 0.0f;
    set.rules.push_back(meadow);
    SurfaceRule patches{};
    patches.name = "Bare soil patches";
    patches.surface = 1u;
    patches.strength = 0.9f;
    patches.noiseAmount = 0.0f;
    patches.coverage = 0.4f;
    patches.coverageScaleMeters = 110.0f;
    patches.coverageSoftness = 0.25f;
    set.rules.push_back(patches);
    SurfaceRule worn{};
    worn.name = "Worn ground on ridges";
    worn.surface = 10u;
    worn.strength = 0.8f;
    worn.maxPosition = -0.2f;
    worn.positionFade = 0.4f;
    worn.minSlopeDegrees = 15.0f;
    worn.slopeFadeDegrees = 8.0f;
    worn.noiseAmount = 0.0f;
    set.rules.push_back(worn);
    SurfaceRule scree{};
    scree.name = "Scree on high slopes";
    scree.surface = 7u;
    scree.minSlopeDegrees = 28.0f;
    scree.slopeFadeDegrees = 10.0f;
    scree.minHeightMeters = 260.0f;
    scree.heightFadeMeters = 140.0f;
    scree.noiseScaleMeters = 120.0f;
    scree.noiseAmount = 1.0f;
    scree.coverage = 0.7f;
    scree.coverageScaleMeters = 140.0f;
    scree.coverageSoftness = 0.3f;
    set.rules.push_back(scree);
    SurfaceRule soil{};
    soil.name = "Soil on steep lower slopes";
    soil.surface = 1u;
    soil.minSlopeDegrees = 44.0f;
    soil.slopeFadeDegrees = 8.0f;
    soil.maxHeightMeters = 450.0f;
    soil.heightFadeMeters = 120.0f;
    soil.noiseScaleMeters = 30.0f;
    soil.noiseAmount = 0.8f;
    set.rules.push_back(soil);
    SurfaceRule alpine{};
    alpine.name = "Rock above the tree line";
    alpine.surface = 2u;
    alpine.minHeightMeters = 430.0f;
    alpine.heightFadeMeters = 170.0f;
    alpine.noiseScaleMeters = 260.0f;
    alpine.noiseAmount = 0.9f;
    set.rules.push_back(alpine);
    SurfaceRule rock{};
    rock.name = "Rock on cliffs";
    rock.surface = 2u;
    rock.minSlopeDegrees = 50.0f;
    rock.slopeFadeDegrees = 8.0f;
    rock.noiseScaleMeters = 18.0f;
    rock.noiseAmount = 0.8f;
    set.rules.push_back(rock);
    SurfaceRule layered{};
    layered.name = "Layered rock on high cliffs";
    layered.surface = 9u;
    layered.minSlopeDegrees = 54.0f;
    layered.slopeFadeDegrees = 6.0f;
    layered.noiseAmount = 0.0f;
    layered.coverage = 0.5f;
    layered.coverageScaleMeters = 180.0f;
    layered.coverageSoftness = 0.3f;
    set.rules.push_back(layered);
    SurfaceRule gravel{};
    gravel.name = "Gravel in river beds";
    gravel.surface = 8u;
    gravel.minWetness = 0.8f;
    gravel.wetnessFade = 0.1f;
    gravel.maxSlopeDegrees = 12.0f;
    gravel.slopeFadeDegrees = 5.0f;
    gravel.noiseAmount = 0.0f;
    set.rules.push_back(gravel);
    SurfaceRule snow{};
    snow.name = "Snow on the summit";
    snow.surface = 4u;
    snow.minHeightMeters = 640.0f;
    snow.heightFadeMeters = 50.0f;
    snow.noiseScaleMeters = 90.0f;
    snow.noiseAmount = 1.0f;
    snow.maxSlopeDegrees = 40.0f;
    snow.slopeFadeDegrees = 8.0f;
    set.rules.push_back(snow);
    return set;
}

void EvaluateSurfaceRules(const SurfaceRuleSet& set, const SurfaceRuleTile& tile, std::vector<uint32_t>& control) {
    const uint32_t samples = tile.samples;
    const uint32_t margin = kSurfaceRuleHeightMargin;
    const uint32_t stride = samples + 2u * margin;
    if (samples < 2u || tile.heights.size() != static_cast<size_t>(stride) * stride ||
        control.size() != static_cast<size_t>(samples) * samples) {
        return;
    }
    std::vector<uint32_t> rows(samples);
    std::iota(rows.begin(), rows.end(), 0u);
    const float slopeScale = 0.5f / (tile.spacingMeters * kSlopeReach);
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](uint32_t z) {
        std::array<float, kTerrainControlMaterialCount> weights{};
        for (uint32_t x = 0u; x < samples; ++x) {
            uint32_t& out = control[static_cast<size_t>(z) * samples + x];
            ControlTexel texel = UnpackControlTexel(out);
            if (texel.handPainted) {
                continue;
            }
            texel.wetness = 0u;
            const size_t center = static_cast<size_t>(z + margin) * stride + x + margin;
            const float height = tile.heights[center];
            const float gradientX =
                (tile.heights[center + kSlopeReach] - tile.heights[center - kSlopeReach]) * slopeScale;
            const float gradientZ = (tile.heights[center + static_cast<size_t>(kSlopeReach) * stride] -
                                     tile.heights[center - static_cast<size_t>(kSlopeReach) * stride]) *
                                    slopeScale;
            const float slope = std::atan(std::sqrt(gradientX * gradientX + gradientZ * gradientZ)) * kRadiansToDegrees;
            const float around =
                0.25f * (tile.heights[center - kConcavityReach] + tile.heights[center + kConcavityReach] +
                         tile.heights[center - static_cast<size_t>(kConcavityReach) * stride] +
                         tile.heights[center + static_cast<size_t>(kConcavityReach) * stride]);
            const float concavity = around - height;
            const double worldX = static_cast<double>(tile.firstSampleX + x) * tile.spacingMeters;
            const double worldZ = static_cast<double>(tile.firstSampleZ + z) * tile.spacingMeters;
            const float wetness = tile.analysis != nullptr ? tile.analysis->WetnessAt(worldX, worldZ) : 0.5f;
            const float position = tile.analysis != nullptr ? tile.analysis->PositionAt(worldX, worldZ) : 0.0f;
            weights.fill(0.0f);
            weights[0] = 1.0f;
            for (size_t index = 0u; index < set.rules.size(); ++index) {
                const SurfaceRule& rule = set.rules[index];
                if (!rule.enabled || rule.surface >= kTerrainControlMaterialCount) {
                    continue;
                }
                // 大きな塊と細かい崩れの二段（一段だと境界が丸い島になる）。
                const float noise =
                    rule.noiseAmount > 0.0f
                        ? rule.noiseAmount *
                              (0.7f * LatticeValueNoise(worldX / rule.noiseScaleMeters, worldZ / rule.noiseScaleMeters,
                                                        static_cast<uint32_t>(index + 1u)) +
                               0.3f * LatticeValueNoise(worldX * 3.7 / rule.noiseScaleMeters,
                                                        worldZ * 3.7 / rule.noiseScaleMeters,
                                                        static_cast<uint32_t>(index + 101u)))
                        : 0.0f;
                float mask = rule.strength *
                             RangeWeight(slope + noise * rule.slopeFadeDegrees, rule.minSlopeDegrees,
                                         rule.maxSlopeDegrees, rule.slopeFadeDegrees, 0.0f, 90.0f) *
                             RangeWeight(height + noise * rule.heightFadeMeters, rule.minHeightMeters,
                                         rule.maxHeightMeters, rule.heightFadeMeters, -100000.0f, 100000.0f) *
                             RangeWeight(concavity, rule.minConcavityMeters, rule.maxConcavityMeters,
                                         rule.concavityFadeMeters, -1000.0f, 1000.0f) *
                             RangeWeight(wetness, rule.minWetness, rule.maxWetness, rule.wetnessFade, 0.0f, 1.0f) *
                             RangeWeight(position, rule.minPosition, rule.maxPosition, rule.positionFade, -1.0f, 1.0f);
                if (rule.facingAmount > 0.0f) {
                    const float azimuth = rule.facingDegrees * kDegreesToRadians;
                    const float length = (std::max)(std::sqrt(gradientX * gradientX + gradientZ * gradientZ), 1.0e-4f);
                    const float facing = (-gradientX * std::sin(azimuth) - gradientZ * std::cos(azimuth)) / length;
                    mask *= 1.0f - rule.facingAmount * (0.5f - 0.5f * facing);
                }
                if (rule.coverage < 1.0f) {
                    const double scale = rule.coverageScaleMeters;
                    const float patch =
                        0.5f + 0.5f * (0.68f * LatticeValueNoise(worldX / scale, worldZ / scale,
                                                                 static_cast<uint32_t>(index + 201u)) +
                                       0.32f * LatticeValueNoise(worldX * 3.1 / scale, worldZ * 3.1 / scale,
                                                                 static_cast<uint32_t>(index + 301u)));
                    mask *= std::clamp((patch - (1.0f - rule.coverage)) / rule.coverageSoftness + 0.5f, 0.0f, 1.0f);
                }
                if (mask <= 0.0f) {
                    continue;
                }
                for (float& weight : weights) {
                    weight *= 1.0f - mask;
                }
                weights[rule.surface] += mask;
            }
            uint32_t first = 0u;
            uint32_t second = 0u;
            float firstWeight = -1.0f;
            float secondWeight = 0.0f;
            for (uint32_t material = 0u; material < kTerrainControlMaterialCount; ++material) {
                const float weight = weights[material];
                if (weight > firstWeight) {
                    second = first;
                    secondWeight = (std::max)(firstWeight, 0.0f);
                    first = material;
                    firstWeight = weight;
                } else if (weight > secondWeight) {
                    second = material;
                    secondWeight = weight;
                }
            }
            const float blend = secondWeight / (std::max)(firstWeight + secondWeight, 1.0e-4f);
            texel.baseMaterial = static_cast<uint8_t>(first);
            texel.overlayMaterial = static_cast<uint8_t>(second);
            texel.blend = static_cast<uint8_t>(std::clamp(blend, 0.0f, 1.0f) * 255.0f + 0.5f);
            texel.uvRotation = 0u;
            texel.uvScale = 0u;
            (void)PackControlTexel(texel, out);
        }
    });
}

void BakeSurfaceColorMap(const SurfaceRuleSet& set, const SurfaceRuleTile& tile, std::span<const uint32_t> control,
                         std::span<const SurfaceMapStyle> styles, uint32_t size, std::vector<float>& out) {
    const uint32_t samples = tile.samples;
    const uint32_t margin = kSurfaceRuleHeightMargin;
    const uint32_t stride = samples + 2u * margin;
    out.assign(static_cast<size_t>(size) * size * 4u, 0.5f);
    if (samples < 2u || size < 2u || styles.empty() || tile.heights.size() != static_cast<size_t>(stride) * stride ||
        control.size() != static_cast<size_t>(samples) * samples) {
        return;
    }
    const auto smooth = [](float low, float high, float value) {
        const float t = std::clamp((value - low) / (high - low), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const auto reachOf = [&](float meters) {
        return static_cast<size_t>(
            std::clamp(static_cast<int>(std::lround(meters / tile.spacingMeters)), 1, static_cast<int>(margin)));
    };
    const size_t nearReach = reachOf(4.0f);
    const size_t wideReach = reachOf(20.0f);
    const SurfaceMapStyle& debrisStyle = styles[(std::min)(static_cast<size_t>(set.debrisSurface), styles.size() - 1u)];
    for (uint32_t z = 0u; z < size; ++z) {
        const uint32_t sourceZ = z * (samples - 1u) / (size - 1u);
        for (uint32_t x = 0u; x < size; ++x) {
            const uint32_t sourceX = x * (samples - 1u) / (size - 1u);
            const size_t center = static_cast<size_t>(sourceZ + margin) * stride + sourceX + margin;
            const float height = tile.heights[center];
            const auto hollow = [&](size_t reach) {
                return 0.25f * (tile.heights[center - reach] + tile.heights[center + reach] +
                                tile.heights[center - reach * stride] + tile.heights[center + reach * stride]) -
                       height;
            };
            const float nearHollow = hollow(nearReach);
            const float wideHollow = hollow(wideReach);
            const double worldX = static_cast<double>(tile.firstSampleX + sourceX) * tile.spacingMeters;
            const double worldZ = static_cast<double>(tile.firstSampleZ + sourceZ) * tile.spacingMeters;
            const float broad = FractalNoise(worldX / 420.0, worldZ / 420.0, 4, 7001u);
            const float middle = FractalNoise(worldX / 110.0, worldZ / 110.0, 5, 7002u);
            const float fine = FractalNoise(worldX / 16.0, worldZ / 16.0, 4, 7003u);
            // 細かい粒：材質の図を読まない遠くでも面がのっぺりしない。
            const float grain = RidgedNoise(worldX / 6.0, worldZ / 6.0, 3, 7004u) - 0.42f;
            const float shade = 1.0f + 0.3f * std::clamp(fine + 0.4f * middle, -1.0f, 1.0f) + 0.5f * grain;
            const auto styleColor = [&](uint32_t slot) {
                const SurfaceMapStyle& style = styles[(std::min)(static_cast<size_t>(slot), styles.size() - 1u)];
                const float second = smooth(-0.25f, 0.35f, broad + 0.35f * middle);
                const float third = smooth(0.15f, 0.55f, 0.8f * broad + 0.6f * middle) * 0.75f;
                float brightness = shade;
                if (style.bandMeters > 0.0f && style.bandAmount > 0.0f) {
                    const float coordinate = height / style.bandMeters + broad * 3.0f + middle * 0.8f;
                    const float band = 0.5f + 0.5f * std::sin(coordinate + 1.7f * std::sin(coordinate * 0.37f));
                    brightness *= 0.82f + 0.36f * band * style.bandAmount;
                }
                std::array<float, 3> color{};
                for (size_t channel = 0u; channel < 3u; ++channel) {
                    const float a = style.colors[0][channel];
                    const float b = style.colors[1][channel];
                    const float c = style.colors[2][channel];
                    const float mixed = a + (b - a) * second;
                    color[channel] = (mixed + (c - mixed) * third) * brightness;
                }
                return color;
            };
            const ControlTexel texel = UnpackControlTexel(control[static_cast<size_t>(sourceZ) * samples + sourceX]);
            const std::array<float, 3> first = styleColor(texel.baseMaterial);
            const std::array<float, 3> second = styleColor(texel.overlayMaterial);
            const float blend = static_cast<float>(texel.blend) / 255.0f;
            const float debris = tile.analysis != nullptr ? tile.analysis->DebrisAt(worldX, worldZ) * 0.75f : 0.0f;
            // 凹みを暗く、稜を明るく。専門の地形ソフトが色へ光を焼き込むのと同じ考え方で、遠くでも形が読める。
            const float dark = smooth(0.0f, 3.5f, nearHollow) * 0.22f + smooth(0.0f, 4.0f, wideHollow) * 0.3f;
            const float light = smooth(0.0f, 1.2f, -nearHollow) * 0.1f + smooth(0.0f, 5.0f, -wideHollow) * 0.2f;
            float* out4 = out.data() + (static_cast<size_t>(z) * size + x) * 4u;
            for (size_t channel = 0u; channel < 3u; ++channel) {
                const float surface = first[channel] + (second[channel] - first[channel]) * blend;
                const float carried = surface + (debrisStyle.colors[0][channel] - surface) * debris;
                out4[channel] = carried * (1.0f - dark) * (1.0f + light);
            }
            out4[3] = 1.0f - 0.6f * dark;
        }
    }
}

} // namespace HIKARI::TERRAIN::DATA
