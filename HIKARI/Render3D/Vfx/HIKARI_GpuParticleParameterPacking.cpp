#include "Render3D/Vfx/HIKARI_GpuParticleParameterPacking.h"

#include <algorithm>
#include <bit>

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {
    namespace {
        inline constexpr uint32_t kEmitterHeaderCount = 15u;
        inline constexpr uint32_t kFloat3DistributionValueCount = 2u;
        inline constexpr uint32_t kFloat4DistributionValueCount = 3u;
        inline constexpr uint32_t kAnimatedFloat3DistributionCount = 6u;
        inline constexpr uint32_t kCurveValueCount =
            2u + kMaxParticleCurveKeys;
        inline constexpr uint32_t kAnimatedFloat3CurveCount = 3u;
        inline constexpr uint32_t kAnimatedFloat3ValueCount =
            kFloat3DistributionValueCount *
                kAnimatedFloat3DistributionCount +
            kCurveValueCount * kAnimatedFloat3CurveCount;
        inline constexpr uint32_t kPositionValues = kEmitterHeaderCount;
        inline constexpr uint32_t kRotationValues =
            kPositionValues + kAnimatedFloat3ValueCount;
        inline constexpr uint32_t kScaleValues =
            kRotationValues + kAnimatedFloat3ValueCount;
        inline constexpr uint32_t kColorDistributionValues =
            kScaleValues + kAnimatedFloat3ValueCount;
        inline constexpr uint32_t kColorCurveValues =
            kColorDistributionValues +
            kFloat4DistributionValueCount * 4u;
        inline constexpr uint32_t kColorGradientValues =
            kColorCurveValues + kCurveValueCount * 4u;
        inline constexpr uint32_t kGradientValueCount =
            1u + kMaxParticleColorStops * 2u;

        static_assert(
            kColorGradientValues + kGradientValueCount ==
            kGpuParticleEmitterValueCount);

        float EncodeUint(uint32_t value) noexcept {
            return std::bit_cast<float>(value);
        }

        void StoreDistribution(
            GpuParticleEmitterParameters& target,
            uint32_t index,
            const ParticleDistributionFloat& distribution) {

            target.values[index] = MakeGpuParticleFloat4(
                distribution.minimum,
                distribution.maximum,
                EncodeUint(static_cast<uint32_t>(distribution.mode)),
                EncodeUint(distribution.randomGroup));
        }

        void StoreDistribution(
            GpuParticleEmitterParameters& target,
            uint32_t index,
            const ParticleDistributionFloat3& distribution) {

            target.values[index] = MakeGpuParticleFloat4(
                distribution.minimum.x,
                distribution.minimum.y,
                distribution.minimum.z,
                EncodeUint(static_cast<uint32_t>(distribution.mode)));
            target.values[index + 1u] = MakeGpuParticleFloat4(
                distribution.maximum.x,
                distribution.maximum.y,
                distribution.maximum.z,
                EncodeUint(distribution.randomGroup));
        }

        void StoreDistribution(
            GpuParticleEmitterParameters& target,
            uint32_t index,
            const ParticleDistributionFloat4& distribution) {

            target.values[index] = MakeGpuParticleFloat4(
                distribution.minimum.x,
                distribution.minimum.y,
                distribution.minimum.z,
                distribution.minimum.w);
            target.values[index + 1u] = MakeGpuParticleFloat4(
                distribution.maximum.x,
                distribution.maximum.y,
                distribution.maximum.z,
                distribution.maximum.w);
            target.values[index + 2u] = MakeGpuParticleFloat4(
                EncodeUint(static_cast<uint32_t>(distribution.mode)),
                EncodeUint(distribution.randomGroup),
                0.0f,
                0.0f);
        }

        void StoreCurve(
            GpuParticleEmitterParameters& target,
            uint32_t index,
            const ParticleCurve& curve) {

            const uint32_t keyCount = (std::min)(
                curve.keyCount,
                kMaxParticleCurveKeys);
            target.values[index] = MakeGpuParticleFloat4(
                EncodeUint(static_cast<uint32_t>(curve.interpolation)),
                EncodeUint(keyCount),
                EncodeUint(static_cast<uint32_t>(
                    curve.randomOffset.mode)),
                EncodeUint(curve.randomOffset.randomGroup));
            target.values[index + 1u] = MakeGpuParticleFloat4(
                curve.randomOffset.minimum,
                curve.randomOffset.maximum,
                0.0f,
                0.0f);
            for (uint32_t keyIndex = 0u;
                 keyIndex < keyCount;
                 ++keyIndex) {
                const ParticleCurveKey& key = curve.keys[keyIndex];
                target.values[index + 2u + keyIndex] =
                    MakeGpuParticleFloat4(
                        key.time,
                        key.value,
                        key.inTangent,
                        key.outTangent);
            }
        }

        void StoreAnimatedFloat3(
            GpuParticleEmitterParameters& target,
            uint32_t index,
            const ParticleAnimatedFloat3& animation) {

            const ParticleDistributionFloat3* distributions[] = {
                &animation.initial,
                &animation.velocity,
                &animation.acceleration,
                &animation.easingStart,
                &animation.easingMiddle,
                &animation.easingEnd,
            };
            for (uint32_t distributionIndex = 0u;
                 distributionIndex <
                    kAnimatedFloat3DistributionCount;
                 ++distributionIndex) {
                StoreDistribution(
                    target,
                    index + distributionIndex *
                        kFloat3DistributionValueCount,
                    *distributions[distributionIndex]);
            }
            const uint32_t curveBase = index +
                kAnimatedFloat3DistributionCount *
                    kFloat3DistributionValueCount;
            for (uint32_t curveIndex = 0u;
                 curveIndex < animation.curves.size();
                 ++curveIndex) {
                StoreCurve(
                    target,
                    curveBase + curveIndex * kCurveValueCount,
                    animation.curves[curveIndex]);
            }
        }
    }

    GpuParticleEmitterParameters BuildGpuParticleEmitterParameters(
        const ParticleEmitterSubmission& submission) {

        const ParticleEmitterSimulationParameters& simulation =
            submission.simulation;
        GpuParticleEmitterParameters result{};
        if (submission.directShape) {
            const uint32_t count = submission.overrides.enabled ? (submission.shapePoses.empty() ?
                1u : static_cast<uint32_t>((std::min)(submission.shapePoses.size(), size_t{16}))) : 0u;
            result.values[0].x = EncodeUint(count);
            static_assert(1 + 16 * 9 <= kGpuParticleEmitterValueCount);
            for (uint32_t i = 0; i < count; ++i) {
                const auto pose = submission.shapePoses.empty() ? CurrentShapePose(submission) : submission.shapePoses[i];
                const auto q = MATH::NormalizeQ(pose.rotation);
                const uint32_t base = 1 + i * 9;
                result.values[base] = {pose.position.x, pose.position.y, pose.position.z, pose.age};
                result.values[base+1] = {q.x,q.y,q.z,q.w};
                result.values[base+2] = {pose.scale.x,pose.scale.y,pose.scale.z,pose.duration};
                result.values[base+3] = {pose.color.x,pose.color.y,pose.color.z,pose.color.w};
                for (uint32_t corner = 0; corner < 4; ++corner) {
                    const auto p = pose.corners[corner];
                    result.values[base+4+corner] = {p.x,p.y,p.z,corner < 2 ? pose.edgeAlpha.x : pose.edgeAlpha.y};
                }
                result.values[base+8] = {pose.uvRange.x,pose.uvRange.y,0,0};
            }
            return result;
        }
        result.values[0] = MakeGpuParticleFloat4(
            submission.worldPosition.x,
            submission.worldPosition.y,
            submission.worldPosition.z,
            simulation.shapeRadius.minimum);
        const MATH::Quat rotation = MATH::NormalizeQ(
            submission.worldRotation);
        result.values[1] = MakeGpuParticleFloat4(
            rotation.x, rotation.y, rotation.z, rotation.w);
        result.values[2] = MakeGpuParticleFloat4(
            submission.worldScale.x,
            submission.worldScale.y,
            submission.worldScale.z,
            simulation.shapeRadius.maximum);
        result.values[3] = MakeGpuParticleFloat4(
            simulation.lifetime.minimum,
            simulation.lifetime.maximum,
            simulation.radialSpeed.minimum,
            simulation.radialSpeed.maximum);
        result.values[4] = MakeGpuParticleFloat4(
            EncodeUint(static_cast<uint32_t>(simulation.shape)),
            std::clamp(
                simulation.coneAngleRadians,
                0.0f,
                3.1415926535f),
            EncodeUint(static_cast<uint32_t>(
                simulation.shapeRadius.mode)),
            EncodeUint(simulation.shapeRadius.randomGroup));
        result.values[5] = MakeGpuParticleFloat4(
            simulation.gravity.x,
            simulation.gravity.y,
            simulation.gravity.z,
            (std::max)(simulation.drag, 0.0f));
        result.values[6] = MakeGpuParticleFloat4(
            submission.overrides.colorMultiplier.x,
            submission.overrides.colorMultiplier.y,
            submission.overrides.colorMultiplier.z,
            submission.overrides.colorMultiplier.w);
        result.values[7] = MakeGpuParticleFloat4(
            EncodeUint(static_cast<uint32_t>(
                simulation.position.mode)),
            EncodeUint(static_cast<uint32_t>(
                simulation.rotationRadians.mode)),
            EncodeUint(static_cast<uint32_t>(
                simulation.scale.mode)),
            EncodeUint(static_cast<uint32_t>(
                simulation.color.mode)));
        result.values[8] = MakeGpuParticleFloat4(
            EncodeUint(static_cast<uint32_t>(
                simulation.position.easingFunction)),
            EncodeUint(simulation.position.hasEasingMiddle),
            EncodeUint(static_cast<uint32_t>(
                simulation.rotationRadians.easingFunction)),
            EncodeUint(simulation.rotationRadians.hasEasingMiddle));
        result.values[9] = MakeGpuParticleFloat4(
            EncodeUint(static_cast<uint32_t>(
                simulation.scale.easingFunction)),
            EncodeUint(simulation.scale.hasEasingMiddle),
            EncodeUint(static_cast<uint32_t>(
                simulation.color.easingFunction)),
            EncodeUint(simulation.color.hasEasingMiddle));
        result.values[10] = MakeGpuParticleFloat4(
            submission.overrides.radialSpeedMultiplier,
            submission.overrides.scaleMultiplier,
            submission.overrides.gravityMultiplier,
            EncodeUint(submission.overrides.enabled));
        result.values[11] = MakeGpuParticleFloat4(
            EncodeUint(static_cast<uint32_t>(
                simulation.lifetime.mode)),
            EncodeUint(simulation.lifetime.randomGroup),
            EncodeUint(static_cast<uint32_t>(
                simulation.radialSpeed.mode)),
            EncodeUint(simulation.radialSpeed.randomGroup));
        result.values[12] = MakeGpuParticleFloat4(
            simulation.shapeBoxHalfExtents.x,
            simulation.shapeBoxHalfExtents.y,
            simulation.shapeBoxHalfExtents.z,
            simulation.shapeCylinderHeight);
        result.values[13] = MakeGpuParticleFloat4(
            simulation.shapeCircleInnerRadius,
            EncodeUint(simulation.shapeCircleEmitOnEdge),
            simulation.shapeEdgeLength,
            // 放出角度範囲。0 のままの古い資産は HLSL 側で全周として扱う。
            simulation.shapeCircleArcDegrees);
        result.values[14] = MakeGpuParticleFloat4(simulation.collisionPlaneHeight,
            simulation.restitution,simulation.friction,
            EncodeUint((simulation.localSimulation ? 1u : 0u) | (simulation.collisionPlaneEnabled ? 2u : 0u)));

        StoreAnimatedFloat3(
            result, kPositionValues, simulation.position);
        StoreAnimatedFloat3(
            result, kRotationValues, simulation.rotationRadians);
        StoreAnimatedFloat3(
            result, kScaleValues, simulation.scale);

        StoreDistribution(
            result,
            kColorDistributionValues,
            simulation.color.fixed);
        StoreDistribution(
            result,
            kColorDistributionValues +
                kFloat4DistributionValueCount,
            simulation.color.easingStart);
        StoreDistribution(
            result,
            kColorDistributionValues +
                kFloat4DistributionValueCount * 2u,
            simulation.color.easingMiddle);
        StoreDistribution(
            result,
            kColorDistributionValues +
                kFloat4DistributionValueCount * 3u,
            simulation.color.easingEnd);
        for (uint32_t curveIndex = 0u;
             curveIndex < simulation.color.curves.size();
             ++curveIndex) {
            StoreCurve(
                result,
                kColorCurveValues + curveIndex * kCurveValueCount,
                simulation.color.curves[curveIndex]);
        }
        const uint32_t colorStopCount = (std::min)(
            simulation.color.gradient.stopCount,
            kMaxParticleColorStops);
        result.values[kColorGradientValues] =
            MakeGpuParticleFloat4(
                EncodeUint(colorStopCount),
                0.0f,
                0.0f,
                0.0f);
        for (uint32_t stopIndex = 0u;
             stopIndex < colorStopCount;
             ++stopIndex) {
            const ParticleColorStop& stop =
                simulation.color.gradient.stops[stopIndex];
            result.values[
                kColorGradientValues + 1u + stopIndex * 2u] =
                MakeGpuParticleFloat4(
                    stop.time, 0.0f, 0.0f, 0.0f);
            result.values[
                kColorGradientValues + 2u + stopIndex * 2u] =
                MakeGpuParticleFloat4(
                    stop.color.x,
                    stop.color.y,
                    stop.color.z,
                    stop.color.w);
        }
        return result;
    }

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
