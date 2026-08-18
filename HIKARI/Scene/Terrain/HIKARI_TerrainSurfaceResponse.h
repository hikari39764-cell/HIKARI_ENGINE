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

    // 移動 stamp の rim が一つ前の depression を持ち上げると、軌跡が
    // 現在位置だけへ置換される。凹みを優先して合成し、rim は未変形面または
    // 既存 rim だけを押し上げる。順番に依存しない animation response 契約。
    inline float AccumulateTerrainSurfaceDisplacement(
        float currentHeight,
        float desiredHeight,
        TerrainSurfaceDisplacementKind kind) noexcept {

        if (!std::isfinite(currentHeight) ||
            !std::isfinite(desiredHeight)) {
            return std::isfinite(currentHeight) ? currentHeight : 0.0f;
        }
        if (kind == TerrainSurfaceDisplacementKind::Depression) {
            return (std::min)(currentHeight, desiredHeight);
        }
        return currentHeight < 0.0f
            ? currentHeight
            : (std::max)(currentHeight, desiredHeight);
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

    inline TerrainSurfaceResponseResult EvaluateTerrainSurfaceResponse(
        const DeformParams& profile,
        const TerrainSurfaceResponseInput& input) noexcept {

        TerrainSurfaceResponseResult result{};
        if (profile.behavior == DeformBehaviorId::None) {
            return result;
        }

        const float strength = std::clamp(
            std::isfinite(input.strength) ? input.strength : 0.0f,
            0.0f,
            4.0f);
        if (strength <= 0.0f) {
            result.holdOnly = true;
            return result;
        }

        const float radius = (std::max)(
            std::isfinite(input.verticalRadiusLocal)
                ? input.verticalRadiusLocal
                : 0.0f,
            0.0f);
        const float penetration = (std::max)(
            std::isfinite(input.penetrationDepthLocal)
                ? input.penetrationDepthLocal
                : 0.0f,
            0.0f);
        const float approach = (std::max)(
            std::isfinite(input.approachSpeedLocal)
                ? input.approachSpeedLocal
                : 0.0f,
            0.0f);
        const float age = (std::max)(
            (std::isfinite(input.contactAgeSeconds)
                ? input.contactAgeSeconds
                : 0.0f) +
            (std::isfinite(input.contactSeconds)
                ? input.contactSeconds
                : 0.0f),
            0.0f);

        const float settleSeconds = (std::max)(
            std::isfinite(profile.settleSeconds)
                ? profile.settleSeconds
                : 0.0f,
            0.0f);
        const float settleStart = std::clamp(
            std::isfinite(profile.settleStartRatio)
                ? profile.settleStartRatio
                : 0.65f,
            0.0f,
            1.0f);
        const float settleProgress = settleSeconds <= 1.0e-5f
            ? 1.0f
            : 1.0f - std::exp(-4.0f * age / settleSeconds);
        result.settleWeight = settleStart +
            (1.0f - settleStart) * settleProgress;
        result.settleWeight = std::clamp(
            result.settleWeight, 0.0f, 1.0f);

        const float restDepth = (std::max)(
            (std::max)(profile.minimumDepth, 0.0f),
            radius * (std::max)(profile.restDepthScale, 0.0f));
        const float impactDepth = approach *
            (std::max)(profile.impactDepthScale, 0.0f);
        const float unbounded =
            (std::max)(restDepth, penetration) + impactDepth;
        result.targetDepthLocal = std::clamp(
            unbounded * strength * result.settleWeight,
            0.0f,
            (std::max)(profile.maxDepth, 0.0f));
        return result;
    }

} // namespace HIKARI::TERRAIN
