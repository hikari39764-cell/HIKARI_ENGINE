#pragma once

#include <algorithm>
#include <cstdint>
#include <cmath>

#include "Scene/Terrain/HIKARI_SurfacePreset.h"

namespace HIKARI::TERRAIN {

enum class TerrainSurfaceDisplacementKind : uint8_t {
    Depression = 0u,
    Rim = 1u,
};

// runtime response は elastic / plastic の 2 channel を持つが、地形の
// 最終変位上限は両者の合計に対する契約である。chunk 境界で channel を
// 別々の source から選ぶと同じ凹みを二重計上するため、常に pair で扱う。
struct TerrainSurfaceDisplacementChannels {
    float elastic = 0.0f;
    float plastic = 0.0f;

    float Combined() const noexcept { return elastic + plastic; }
};

inline TerrainSurfaceDisplacementChannels ClampTerrainSurfaceDisplacementChannels(
    TerrainSurfaceDisplacementChannels channels, float minimumHeight, float maximumHeight) noexcept {
    float safeMinimum = std::isfinite(minimumHeight) ? minimumHeight : 0.0f;
    float safeMaximum = std::isfinite(maximumHeight) ? maximumHeight : 0.0f;
    if (safeMinimum > safeMaximum) {
        std::swap(safeMinimum, safeMaximum);
    }

    channels.plastic = std::clamp(std::isfinite(channels.plastic) ? channels.plastic : 0.0f, safeMinimum, safeMaximum);
    channels.elastic = std::clamp(std::isfinite(channels.elastic) ? channels.elastic : 0.0f,
                                  safeMinimum - channels.plastic, safeMaximum - channels.plastic);
    return channels;
}

// 凹みは rim より優先し、凹み同士なら深い方、rim 同士なら高い方を
// 採用する。同じ合計値では elastic が少ない pair を選び、保持済みの痕跡を
// transient response へ戻したり、相殺 channel を温存したりしない。
inline TerrainSurfaceDisplacementChannels SelectTerrainSurfaceDisplacementChannels(
    TerrainSurfaceDisplacementChannels current, TerrainSurfaceDisplacementChannels candidate) noexcept {
    const float currentTotal = current.Combined();
    const float candidateTotal = candidate.Combined();
    const bool depressionPresent = currentTotal < 0.0f || candidateTotal < 0.0f;
    if ((depressionPresent && candidateTotal < currentTotal) || (!depressionPresent && candidateTotal > currentTotal) ||
        (candidateTotal == currentTotal && std::fabs(candidate.elastic) < std::fabs(current.elastic))) {
        return candidate;
    }
    return current;
}

// 移動 stamp の rim が一つ前の depression を持ち上げると、軌跡が
// 現在位置だけへ置換される。凹みを優先して合成し、rim は未変形面または
// 既存 rim だけを押し上げる。順番に依存しない animation response 契約。
inline float AccumulateTerrainSurfaceDisplacement(float currentHeight, float desiredHeight,
                                                  TerrainSurfaceDisplacementKind kind) noexcept {
    if (!std::isfinite(currentHeight) || !std::isfinite(desiredHeight)) {
        return std::isfinite(currentHeight) ? currentHeight : 0.0f;
    }
    if (kind == TerrainSurfaceDisplacementKind::Depression) {
        return (std::min)(currentHeight, desiredHeight);
    }
    return currentHeight < 0.0f ? currentHeight : (std::max)(currentHeight, desiredHeight);
}

// 物理 contact は「変形してよい」という gate に限定し、見た目の深さは
// surface profile が決める。これにより球、足、車輪、任意 gameplay source が
// 同じ response を使い、solver の微小侵入量へ依存しない。
struct TerrainSurfaceResponseInput {
    float verticalRadiusLocal = 0.25f;
    float penetrationDepthLocal = 0.0f;
    float approachSpeedLocal = 0.0f;
    float contactAgeSeconds = 0.0f;
    float contactSeconds = 0.0f;
    float strength = 1.0f;
};

struct TerrainSurfaceResponseResult {
    float targetDepthLocal = 0.0f;
    float settleWeight = 0.0f;
    bool holdOnly = false;
};

inline TerrainSurfaceResponseResult EvaluateTerrainSurfaceResponse(const DeformParams& profile,
                                                                   const TerrainSurfaceResponseInput& input) noexcept {
    TerrainSurfaceResponseResult result{};
    if (profile.behavior == DeformBehaviorId::None) {
        return result;
    }

    const float strength = std::clamp(std::isfinite(input.strength) ? input.strength : 0.0f, 0.0f, 4.0f);
    if (strength <= 0.0f) {
        result.holdOnly = true;
        return result;
    }

    const float radius = (std::max)(std::isfinite(input.verticalRadiusLocal) ? input.verticalRadiusLocal : 0.0f, 0.0f);
    const float penetration =
        (std::max)(std::isfinite(input.penetrationDepthLocal) ? input.penetrationDepthLocal : 0.0f, 0.0f);
    const float approach = (std::max)(std::isfinite(input.approachSpeedLocal) ? input.approachSpeedLocal : 0.0f, 0.0f);
    const float age = (std::max)((std::isfinite(input.contactAgeSeconds) ? input.contactAgeSeconds : 0.0f) +
                                     (std::isfinite(input.contactSeconds) ? input.contactSeconds : 0.0f),
                                 0.0f);

    const float settleSeconds = (std::max)(std::isfinite(profile.settleSeconds) ? profile.settleSeconds : 0.0f, 0.0f);
    const float settleStart =
        std::clamp(std::isfinite(profile.settleStartRatio) ? profile.settleStartRatio : 0.65f, 0.0f, 1.0f);
    const float settleProgress = settleSeconds <= 1.0e-5f ? 1.0f : 1.0f - std::exp(-4.0f * age / settleSeconds);
    result.settleWeight = settleStart + (1.0f - settleStart) * settleProgress;
    result.settleWeight = std::clamp(result.settleWeight, 0.0f, 1.0f);

    const float restDepth =
        (std::max)((std::max)(profile.minimumDepth, 0.0f), radius * (std::max)(profile.restDepthScale, 0.0f));
    const float impactDepth = approach * (std::max)(profile.impactDepthScale, 0.0f);
    const float unbounded = (std::max)(restDepth, penetration) + impactDepth;
    result.targetDepthLocal =
        std::clamp(unbounded * strength * result.settleWeight, 0.0f, (std::max)(profile.maxDepth, 0.0f));
    return result;
}

} // namespace HIKARI::TERRAIN
