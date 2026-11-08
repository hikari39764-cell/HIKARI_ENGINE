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
constexpr int kConcavityReach = static_cast<int>(kSurfaceRuleHeightMargin);

float RangeWeight(float value, float minimum, float maximum, float fade) noexcept {
    const float below = std::clamp((value - minimum) / fade + 0.5f, 0.0f, 1.0f);
    const float above = std::clamp((maximum - value) / fade + 0.5f, 0.0f, 1.0f);
    return below * above;
}

} // namespace

SurfaceRuleSet DefaultSurfaceRules() {
    SurfaceRuleSet set{};
    SurfaceRule soil{};
    soil.name = "Soil on slopes";
    soil.surface = 1u;
    soil.minSlopeDegrees = 24.0f;
    soil.slopeFadeDegrees = 16.0f;
    soil.strength = 0.8f;
    set.rules.push_back(soil);
    SurfaceRule sand{};
    sand.name = "Sand in low valleys";
    sand.surface = 3u;
    sand.maxHeightMeters = 12.0f;
    sand.maxSlopeDegrees = 14.0f;
    sand.heightFadeMeters = 10.0f;
    set.rules.push_back(sand);
    SurfaceRule rock{};
    rock.name = "Rock on cliffs";
    rock.surface = 2u;
    rock.minSlopeDegrees = 38.0f;
    rock.slopeFadeDegrees = 14.0f;
    set.rules.push_back(rock);
    SurfaceRule snow{};
    snow.name = "Snow on high ground";
    snow.surface = 4u;
    snow.minHeightMeters = 170.0f;
    snow.maxSlopeDegrees = 36.0f;
    snow.heightFadeMeters = 26.0f;
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
    const float slopeScale = 0.5f / tile.spacingMeters;
    std::for_each(std::execution::par, rows.begin(), rows.end(), [&](uint32_t z) {
        std::array<float, kTerrainControlMaterialCount> weights{};
        for (uint32_t x = 0u; x < samples; ++x) {
            uint32_t& out = control[static_cast<size_t>(z) * samples + x];
            ControlTexel texel = UnpackControlTexel(out);
            if (texel.handPainted) {
                continue;
            }
            const size_t center = static_cast<size_t>(z + margin) * stride + x + margin;
            const float height = tile.heights[center];
            const float gradientX = (tile.heights[center + 1u] - tile.heights[center - 1u]) * slopeScale;
            const float gradientZ = (tile.heights[center + stride] - tile.heights[center - stride]) * slopeScale;
            const float slope = std::atan(std::sqrt(gradientX * gradientX + gradientZ * gradientZ)) * kRadiansToDegrees;
            const float around =
                0.25f * (tile.heights[center - kConcavityReach] + tile.heights[center + kConcavityReach] +
                         tile.heights[center - static_cast<size_t>(kConcavityReach) * stride] +
                         tile.heights[center + static_cast<size_t>(kConcavityReach) * stride]);
            const float concavity = around - height;
            const double worldX = static_cast<double>(tile.firstSampleX + x) * tile.spacingMeters;
            const double worldZ = static_cast<double>(tile.firstSampleZ + z) * tile.spacingMeters;
            weights.fill(0.0f);
            weights[0] = 1.0f;
            for (size_t index = 0u; index < set.rules.size(); ++index) {
                const SurfaceRule& rule = set.rules[index];
                if (!rule.enabled || rule.surface >= kTerrainControlMaterialCount) {
                    continue;
                }
                const float noise = rule.noiseAmount > 0.0f
                                        ? rule.noiseAmount * LatticeValueNoise(worldX / rule.noiseScaleMeters,
                                                                               worldZ / rule.noiseScaleMeters,
                                                                               static_cast<uint32_t>(index + 1u))
                                        : 0.0f;
                float mask =
                    rule.strength *
                    RangeWeight(slope + noise * rule.slopeFadeDegrees, rule.minSlopeDegrees, rule.maxSlopeDegrees,
                                rule.slopeFadeDegrees) *
                    RangeWeight(height + noise * rule.heightFadeMeters, rule.minHeightMeters, rule.maxHeightMeters,
                                rule.heightFadeMeters) *
                    RangeWeight(concavity, rule.minConcavityMeters, rule.maxConcavityMeters, rule.concavityFadeMeters);
                if (rule.facingAmount > 0.0f) {
                    const float azimuth = rule.facingDegrees * kDegreesToRadians;
                    const float length = (std::max)(std::sqrt(gradientX * gradientX + gradientZ * gradientZ), 1.0e-4f);
                    const float facing = (-gradientX * std::sin(azimuth) - gradientZ * std::cos(azimuth)) / length;
                    mask *= 1.0f - rule.facingAmount * (0.5f - 0.5f * facing);
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

} // namespace HIKARI::TERRAIN::DATA
