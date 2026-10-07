#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Core/Math/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::TERRAIN {

// 地形との接触を生成した機能を、オブジェクトだけでなく安定した
// feature 単位でも識別する。左右の足や複数ホイールを同じ object に
// 持たせても履歴を混同しない。
struct TerrainInteractionSourceKey {
    RuntimeObjectHandle object{};
    uint64_t featureId = 0u;

    bool operator==(const TerrainInteractionSourceKey& rhs) const noexcept {
        return object == rhs.object && featureId == rhs.featureId;
    }
};

enum class TerrainInteractionPhase : uint8_t {
    Begin,
    Persist,
    End,
};

enum class TerrainInteractionSourceKind : uint8_t {
    RigidBody,
    CharacterFoot,
    Wheel,
    FollowerCompatibility,
    Custom,
};

enum class TerrainFootprintKind : uint8_t {
    Sphere,
    Ellipse,
    Capsule,
};

struct TerrainInteractionFootprint {
    TerrainFootprintKind kind = TerrainFootprintKind::Ellipse;
    // ワールド空間での定向半長。x は tangentWS 方向、y は
    // contact plane 上の bitangent 方向。Capsule では x が両端の
    // 円帽を含む全半長、y が円帽半径。
    MATH::Vec2 halfExtentsWS{0.25f, 0.12f};
    // Sphere の縦半径。Ellipse/Capsule では明示 depth を使う。
    float verticalRadiusWS = 0.12f;
    MATH::Vec3 tangentWS{1.0f, 0.0f, 0.0f};
    float edgeSoftness = 0.35f;
};

// 自動接触 producer が 1 fixed tick で発行できる sweep を制限する。
// Kinematic の teleport / 不正な大移動を通常移動として細分化すると、
// downstream の terrain chunk work が移動距離に比例して無制限に増える。
inline constexpr uint32_t kTerrainInteractionMaxSweepSamples = 8u;

struct TerrainInteractionSweepPlan {
    uint32_t sampleCount = 1u;
    bool discontinuity = false;
};

inline TerrainInteractionSweepPlan PlanTerrainInteractionSweep(float travel, float footprintRadius,
                                                               float stepSeconds) noexcept {
    const float safeTravel = std::isfinite(travel) ? (std::max)(travel, 0.0f) : 0.0f;
    const float safeRadius = std::isfinite(footprintRadius) ? (std::max)(footprintRadius, 0.01f) : 0.01f;
    const float safeStepSeconds = std::isfinite(stepSeconds) ? (std::max)(stepSeconds, 0.0f) : 0.0f;
    const float discontinuityDistance = (std::max)({
        2.0f,
        safeRadius * 8.0f,
        safeStepSeconds * 80.0f,
    });
    if (!std::isfinite(travel) || safeTravel > discontinuityDistance) {
        return {1u, true};
    }

    const float sampleSpacing = (std::max)(safeRadius * 0.35f, 0.01f);
    const double requestedSamples =
        safeTravel > 1.0e-5f ? std::ceil(static_cast<double>(safeTravel) / static_cast<double>(sampleSpacing)) : 1.0;
    const uint32_t boundedSamples = requestedSamples >= static_cast<double>(kTerrainInteractionMaxSweepSamples)
                                        ? kTerrainInteractionMaxSweepSamples
                                        : (std::max)(1u, static_cast<uint32_t>(requestedSamples));
    return {
        boundedSamples,
        false,
    };
}

// Physics、character pose、wheel、gameplay の各 adapter が発行し、
// Terrain/Sand response だけが消費する唯一の実行時接触契約。
// positionWS は Sphere では球心、その他では接触面の中心を表す。
struct TerrainInteractionSample {
    TerrainInteractionSourceKey source{};
    // 自動 interaction は必ず実接触した surface を指定する。invalid target は
    // terrain consumer が拒否し、重なった surface へ broadcast しない。
    RuntimeObjectHandle targetSurface{};
    TerrainInteractionPhase phase = TerrainInteractionPhase::Persist;
    TerrainInteractionSourceKind sourceKind = TerrainInteractionSourceKind::Custom;
    MATH::Vec3 positionWS{};
    MATH::Vec3 normalWS{0.0f, 1.0f, 0.0f};
    MATH::Vec3 relativeVelocityWS{};
    TerrainInteractionFootprint footprint{};
    // Ellipse/Capsule 用の明示的な沈み込み量。Sphere は接触形状から
    // penetration を求めるため 0 のままでよい。
    float penetrationDepthWS = 0.0f;
    float normalImpulseNs = 0.0f;
    float normalLoadN = 0.0f;
    // 0 は active contact を維持する hold-only sample。consumer は
    // recovery hold を更新するが、新しい elastic/plastic 量を加えない。
    float strength = 1.0f;
    // strength と独立した時間沈降の許可。自動 producer は active な
    // Begin/Persist（hold-only を含む）で true、End で false にする。
    // Custom source は明示的に opt-in しない限り従来通り false。
    bool allowSettling = false;
    // この接触が Begin してからの累積時間。Begin sample は 0、
    // Persist sample は接触が維持された時間を単調に累積する。
    // contactSeconds は引き続き「この sample が担当する時間幅」。
    float contactAgeSeconds = 0.0f;
    float contactSeconds = 0.0f;
    uint64_t fixedTickIndex = 0u;
};

inline bool IsFiniteTerrainInteractionSample(const TerrainInteractionSample& sample) noexcept {
    const auto finiteVec3 = [](const MATH::Vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    return finiteVec3(sample.positionWS) && finiteVec3(sample.normalWS) && finiteVec3(sample.relativeVelocityWS) &&
           finiteVec3(sample.footprint.tangentWS) && std::isfinite(sample.footprint.halfExtentsWS.x) &&
           std::isfinite(sample.footprint.halfExtentsWS.y) && std::isfinite(sample.footprint.verticalRadiusWS) &&
           std::isfinite(sample.footprint.edgeSoftness) && std::isfinite(sample.penetrationDepthWS) &&
           std::isfinite(sample.normalImpulseNs) && std::isfinite(sample.normalLoadN) &&
           std::isfinite(sample.strength) && std::isfinite(sample.contactAgeSeconds) &&
           std::isfinite(sample.contactSeconds);
}

inline TerrainInteractionSample SanitizeTerrainInteractionSample(TerrainInteractionSample sample) noexcept {
    const auto finiteOr = [](float value, float fallback) noexcept { return std::isfinite(value) ? value : fallback; };
    const auto finiteVecOr = [](const MATH::Vec3& value, const MATH::Vec3& fallback) noexcept {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) ? value : fallback;
    };
    sample.positionWS = finiteVecOr(sample.positionWS, {});
    sample.relativeVelocityWS = finiteVecOr(sample.relativeVelocityWS, {});
    sample.normalWS = finiteVecOr(sample.normalWS, {0.0f, 1.0f, 0.0f});
    sample.normalWS = MATH::Dot(sample.normalWS, sample.normalWS) > 1.0e-10f ? MATH::Normalize(sample.normalWS)
                                                                             : MATH::Vec3{0.0f, 1.0f, 0.0f};
    sample.footprint.tangentWS = finiteVecOr(sample.footprint.tangentWS, {1.0f, 0.0f, 0.0f});
    sample.footprint.tangentWS =
        sample.footprint.tangentWS - sample.normalWS * MATH::Dot(sample.footprint.tangentWS, sample.normalWS);
    if (MATH::Dot(sample.footprint.tangentWS, sample.footprint.tangentWS) <= 1.0e-10f) {
        const MATH::Vec3 fallback =
            std::fabs(sample.normalWS.y) < 0.9f ? MATH::Vec3{0.0f, 1.0f, 0.0f} : MATH::Vec3{1.0f, 0.0f, 0.0f};
        sample.footprint.tangentWS = MATH::Normalize(MATH::Cross(fallback, sample.normalWS));
    } else {
        sample.footprint.tangentWS = MATH::Normalize(sample.footprint.tangentWS);
    }
    sample.footprint.halfExtentsWS.x = std::clamp(finiteOr(sample.footprint.halfExtentsWS.x, 0.0f), 0.0f, 100.0f);
    sample.footprint.halfExtentsWS.y = std::clamp(finiteOr(sample.footprint.halfExtentsWS.y, 0.0f), 0.0f, 100.0f);
    sample.footprint.verticalRadiusWS = std::clamp(finiteOr(sample.footprint.verticalRadiusWS, 0.0f), 0.0f, 100.0f);
    sample.footprint.edgeSoftness = std::clamp(finiteOr(sample.footprint.edgeSoftness, 0.35f), 0.0f, 1.0f);
    sample.penetrationDepthWS = (std::max)(finiteOr(sample.penetrationDepthWS, 0.0f), 0.0f);
    sample.normalImpulseNs = (std::max)(finiteOr(sample.normalImpulseNs, 0.0f), 0.0f);
    sample.normalLoadN = (std::max)(finiteOr(sample.normalLoadN, 0.0f), 0.0f);
    sample.strength = std::clamp(finiteOr(sample.strength, 1.0f), 0.0f, 8.0f);
    sample.contactAgeSeconds = (std::max)(finiteOr(sample.contactAgeSeconds, 0.0f), 0.0f);
    sample.contactSeconds = (std::max)(finiteOr(sample.contactSeconds, 0.0f), 0.0f);
    if (sample.phase == TerrainInteractionPhase::End) {
        sample.allowSettling = false;
    }
    return sample;
}

} // namespace HIKARI::TERRAIN
