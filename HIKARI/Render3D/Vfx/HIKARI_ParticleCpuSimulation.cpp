#include "Render3D/Vfx/HIKARI_ParticleCpuSimulation.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::RENDER3D::PARTICLES {
    namespace {
        constexpr uint32_t kRandomSaltLifetime = 1u;
        constexpr uint32_t kRandomSaltShapeRadius = 2u;
        constexpr uint32_t kRandomSaltShapeDirectionY = 3u;
        constexpr uint32_t kRandomSaltShapeDirectionAngle = 4u;
        constexpr uint32_t kRandomSaltShapeOffsetRadius = 5u;
        constexpr uint32_t kRandomSaltRadialSpeed = 6u;
        constexpr uint32_t kRandomSaltPosition = 16u;
        constexpr uint32_t kRandomSaltRotation = 32u;
        constexpr uint32_t kRandomSaltScale = 48u;
        constexpr uint32_t kRandomSaltColor = 64u;

        uint32_t HashRandom(uint32_t value) noexcept {
            value ^= value >> 16u;
            value *= 0x7feb352du;
            value ^= value >> 15u;
            value *= 0x846ca68bu;
            value ^= value >> 16u;
            return value;
        }

        float PropertyRandom01(
            uint32_t particleSeed,
            uint32_t randomGroup,
            uint32_t propertySalt) noexcept {

            const uint32_t stream = randomGroup != 0u
                ? randomGroup
                : propertySalt;
            const uint32_t state = HashRandom(
                particleSeed ^ ((stream + 1u) * 2246822519u));
            return static_cast<float>(state & 0x00ffffffu) *
                (1.0f / 16777216.0f);
        }

        float Lerp(float left, float right, float alpha) noexcept {
            return left + (right - left) * alpha;
        }

        MATH::Vec3 Lerp(
            const MATH::Vec3& left,
            const MATH::Vec3& right,
            float alpha) noexcept {

            return {
                Lerp(left.x, right.x, alpha),
                Lerp(left.y, right.y, alpha),
                Lerp(left.z, right.z, alpha),
            };
        }

        MATH::Vec4 Lerp(
            const MATH::Vec4& left,
            const MATH::Vec4& right,
            float alpha) noexcept {

            return {
                Lerp(left.x, right.x, alpha),
                Lerp(left.y, right.y, alpha),
                Lerp(left.z, right.z, alpha),
                Lerp(left.w, right.w, alpha),
            };
        }

        MATH::Vec3 Multiply(
            const MATH::Vec3& left,
            const MATH::Vec3& right) noexcept {

            return {
                left.x * right.x,
                left.y * right.y,
                left.z * right.z,
            };
        }

        float SampleDistribution(
            const ParticleDistributionFloat& distribution,
            uint32_t particleSeed,
            uint32_t propertySalt) noexcept {

            if (distribution.mode == ParticleDistributionMode::Constant) {
                return distribution.minimum;
            }
            return Lerp(
                distribution.minimum,
                distribution.maximum,
                PropertyRandom01(
                    particleSeed,
                    distribution.randomGroup,
                    propertySalt));
        }

        MATH::Vec3 SampleDistribution(
            const ParticleDistributionFloat3& distribution,
            uint32_t particleSeed,
            uint32_t propertySalt) noexcept {

            if (distribution.mode == ParticleDistributionMode::Constant) {
                return distribution.minimum;
            }
            return Lerp(
                distribution.minimum,
                distribution.maximum,
                PropertyRandom01(
                    particleSeed,
                    distribution.randomGroup,
                    propertySalt));
        }

        MATH::Vec4 SampleDistribution(
            const ParticleDistributionFloat4& distribution,
            uint32_t particleSeed,
            uint32_t propertySalt) noexcept {

            if (distribution.mode == ParticleDistributionMode::Constant) {
                return distribution.minimum;
            }
            return Lerp(
                distribution.minimum,
                distribution.maximum,
                PropertyRandom01(
                    particleSeed,
                    distribution.randomGroup,
                    propertySalt));
        }

        MATH::Vec3 Rotate(
            const MATH::Quat& rotation,
            const MATH::Vec3& value) noexcept {

            const MATH::Vec3 q{ rotation.x, rotation.y, rotation.z };
            const MATH::Vec3 uv{
                q.y * value.z - q.z * value.y,
                q.z * value.x - q.x * value.z,
                q.x * value.y - q.y * value.x,
            };
            const MATH::Vec3 uuv{
                q.y * uv.z - q.z * uv.y,
                q.z * uv.x - q.x * uv.z,
                q.x * uv.y - q.y * uv.x,
            };
            return value + uv * (2.0f * rotation.w) + uuv * 2.0f;
        }

        float EvaluateCurve(
            const ParticleCurve& curve,
            float time,
            float randomOffset) noexcept {

            if (curve.keyCount == 0u) return randomOffset;
            time = std::clamp(time, 0.0f, 1.0f);
            if (time <= curve.keys[0].time) {
                return curve.keys[0].value + randomOffset;
            }
            for (uint32_t index = 1u; index < curve.keyCount; ++index) {
                const ParticleCurveKey& left = curve.keys[index - 1u];
                const ParticleCurveKey& right = curve.keys[index];
                if (time > right.time) continue;
                const float width = right.time - left.time;
                if (width <= 0.0f || curve.interpolation ==
                    ParticleCurveInterpolation::Step) {
                    return left.value + randomOffset;
                }
                const float alpha = (time - left.time) / width;
                if (curve.interpolation ==
                    ParticleCurveInterpolation::Linear) {
                    return Lerp(left.value, right.value, alpha) +
                        randomOffset;
                }
                const float alpha2 = alpha * alpha;
                const float alpha3 = alpha2 * alpha;
                const float h00 = 2.0f * alpha3 - 3.0f * alpha2 + 1.0f;
                const float h10 = alpha3 - 2.0f * alpha2 + alpha;
                const float h01 = -2.0f * alpha3 + 3.0f * alpha2;
                const float h11 = alpha3 - alpha2;
                return h00 * left.value + h10 * left.outTangent * width +
                    h01 * right.value + h11 * right.inTangent * width +
                    randomOffset;
            }
            return curve.keys[curve.keyCount - 1u].value + randomOffset;
        }

        MATH::Vec4 EvaluateGradient(
            const ParticleColorGradient& gradient,
            float time) noexcept {

            if (gradient.stopCount == 0u) {
                return { 1.0f, 1.0f, 1.0f, 1.0f };
            }
            time = std::clamp(time, 0.0f, 1.0f);
            if (time <= gradient.stops[0].time) {
                return gradient.stops[0].color;
            }
            for (uint32_t index = 1u;
                index < gradient.stopCount;
                ++index) {
                const ParticleColorStop& left = gradient.stops[index - 1u];
                const ParticleColorStop& right = gradient.stops[index];
                if (time > right.time) continue;
                const float width = right.time - left.time;
                return Lerp(
                    left.color,
                    right.color,
                    width > 0.0f ? (time - left.time) / width : 0.0f);
            }
            return gradient.stops[gradient.stopCount - 1u].color;
        }

        float EvaluateEasing(
            ParticleEasingFunction function,
            float time) noexcept {

            time = std::clamp(time, 0.0f, 1.0f);
            switch (function) {
            case ParticleEasingFunction::EaseIn:
                return time * time;
            case ParticleEasingFunction::EaseOut:
                return 1.0f - (1.0f - time) * (1.0f - time);
            case ParticleEasingFunction::EaseInOut:
                return time < 0.5f
                    ? 2.0f * time * time
                    : 1.0f - std::pow(-2.0f * time + 2.0f, 2.0f) * 0.5f;
            case ParticleEasingFunction::Linear:
            default:
                return time;
            }
        }

        template<class T>
        T EvaluateEasingValue(
            const T& start,
            const T& middle,
            const T& end,
            bool hasMiddle,
            ParticleEasingFunction function,
            float time) noexcept {

            if (!hasMiddle) {
                return Lerp(start, end, EvaluateEasing(function, time));
            }
            return time < 0.5f
                ? Lerp(
                    start,
                    middle,
                    EvaluateEasing(function, time * 2.0f))
                : Lerp(
                    middle,
                    end,
                    EvaluateEasing(function, (time - 0.5f) * 2.0f));
        }

        CpuResolvedAnimatedFloat3 ResolveAnimated(
            const ParticleAnimatedFloat3& source,
            uint32_t particleSeed,
            uint32_t propertySalt) noexcept {

            CpuResolvedAnimatedFloat3 value{};
            value.mode = source.mode;
            value.initial = SampleDistribution(
                source.initial, particleSeed, propertySalt + 0u);
            value.velocity = SampleDistribution(
                source.velocity, particleSeed, propertySalt + 1u);
            value.acceleration = SampleDistribution(
                source.acceleration, particleSeed, propertySalt + 2u);
            value.easingStart = SampleDistribution(
                source.easingStart, particleSeed, propertySalt + 3u);
            value.easingMiddle = SampleDistribution(
                source.easingMiddle, particleSeed, propertySalt + 4u);
            value.easingEnd = SampleDistribution(
                source.easingEnd, particleSeed, propertySalt + 5u);
            value.easingFunction = source.easingFunction;
            value.hasEasingMiddle = source.hasEasingMiddle != 0u;
            for (size_t index = 0u; index < source.curves.size(); ++index) {
                value.curveRandomOffsets[index] = SampleDistribution(
                    source.curves[index].randomOffset,
                    particleSeed,
                    propertySalt + 6u + static_cast<uint32_t>(index));
            }
            return value;
        }

        CpuResolvedAnimatedColor ResolveAnimated(
            const ParticleAnimatedColor& source,
            uint32_t particleSeed,
            uint32_t propertySalt) noexcept {

            CpuResolvedAnimatedColor value{};
            value.mode = source.mode;
            value.fixed = SampleDistribution(
                source.fixed, particleSeed, propertySalt + 0u);
            value.easingStart = SampleDistribution(
                source.easingStart, particleSeed, propertySalt + 1u);
            value.easingMiddle = SampleDistribution(
                source.easingMiddle, particleSeed, propertySalt + 2u);
            value.easingEnd = SampleDistribution(
                source.easingEnd, particleSeed, propertySalt + 3u);
            value.easingFunction = source.easingFunction;
            value.hasEasingMiddle = source.hasEasingMiddle != 0u;
            for (size_t index = 0u; index < source.curves.size(); ++index) {
                value.curveRandomOffsets[index] = SampleDistribution(
                    source.curves[index].randomOffset,
                    particleSeed,
                    propertySalt + 4u + static_cast<uint32_t>(index));
            }
            return value;
        }

        MATH::Vec3 EvaluateAnimated(
            const ParticleAnimatedFloat3& source,
            const CpuResolvedAnimatedFloat3& resolved,
            float ageSeconds,
            float normalizedAge) noexcept {

            switch (resolved.mode) {
            case ParticleValueMode::Pva:
                return resolved.initial + resolved.velocity * ageSeconds +
                    resolved.acceleration *
                        (0.5f * ageSeconds * ageSeconds);
            case ParticleValueMode::Easing:
                return EvaluateEasingValue(
                    resolved.easingStart,
                    resolved.easingMiddle,
                    resolved.easingEnd,
                    resolved.hasEasingMiddle,
                    resolved.easingFunction,
                    normalizedAge);
            case ParticleValueMode::Curve:
                return {
                    EvaluateCurve(
                        source.curves[0],
                        normalizedAge,
                        resolved.curveRandomOffsets[0]),
                    EvaluateCurve(
                        source.curves[1],
                        normalizedAge,
                        resolved.curveRandomOffsets[1]),
                    EvaluateCurve(
                        source.curves[2],
                        normalizedAge,
                        resolved.curveRandomOffsets[2]),
                };
            case ParticleValueMode::Fixed:
            default:
                return resolved.initial;
            }
        }

        MATH::Vec4 EvaluateAnimated(
            const ParticleAnimatedColor& source,
            const CpuResolvedAnimatedColor& resolved,
            float normalizedAge) noexcept {

            switch (resolved.mode) {
            case ParticleColorMode::Easing:
                return EvaluateEasingValue(
                    resolved.easingStart,
                    resolved.easingMiddle,
                    resolved.easingEnd,
                    resolved.hasEasingMiddle,
                    resolved.easingFunction,
                    normalizedAge);
            case ParticleColorMode::Curve:
                return {
                    EvaluateCurve(
                        source.curves[0], normalizedAge,
                        resolved.curveRandomOffsets[0]),
                    EvaluateCurve(
                        source.curves[1], normalizedAge,
                        resolved.curveRandomOffsets[1]),
                    EvaluateCurve(
                        source.curves[2], normalizedAge,
                        resolved.curveRandomOffsets[2]),
                    EvaluateCurve(
                        source.curves[3], normalizedAge,
                        resolved.curveRandomOffsets[3]),
                };
            case ParticleColorMode::Gradient:
                return EvaluateGradient(source.gradient, normalizedAge);
            case ParticleColorMode::RandomRange:
            case ParticleColorMode::Fixed:
            default:
                return resolved.fixed;
            }
        }

        MATH::Vec3 SampleDirection(
            uint32_t particleSeed,
            float minimumY) noexcept {

            constexpr float kTau = 6.28318530717958647692f;
            const float y = Lerp(
                minimumY,
                1.0f,
                PropertyRandom01(
                    particleSeed,
                    0u,
                    kRandomSaltShapeDirectionY));
            const float angle = PropertyRandom01(
                particleSeed,
                0u,
                kRandomSaltShapeDirectionAngle) * kTau;
            const float radial = std::sqrt(
                (std::max)(0.0f, 1.0f - y * y));
            return {
                radial * std::cos(angle),
                y,
                radial * std::sin(angle),
            };
        }

        struct SpawnShapeSample {
            MATH::Vec3 direction{ 0.0f, 1.0f, 0.0f };
            MATH::Vec3 offset{};
        };

        SpawnShapeSample SampleSpawnShape(
            const ParticleEmitterSubmission& submission,
            uint32_t particleSeed,
            float radius) noexcept {

            SpawnShapeSample sample{};
            switch (submission.simulation.shape) {
            case ParticleShape::Sphere: {
                sample.direction = SampleDirection(particleSeed, -1.0f);
                const float distance = std::cbrt(PropertyRandom01(
                    particleSeed,
                    0u,
                    kRandomSaltShapeOffsetRadius)) * radius;
                sample.offset = sample.direction * distance;
                return sample;
            }
            case ParticleShape::Cone: {
                constexpr float kPi = 3.14159265358979323846f;
                const float halfAngle = std::clamp(
                    submission.simulation.coneAngleRadians * 0.5f,
                    0.0f,
                    kPi);
                const float cosineMinimum = std::cos(
                    halfAngle);
                sample.direction = SampleDirection(
                    particleSeed,
                    cosineMinimum);
                const float distance = std::cbrt(PropertyRandom01(
                    particleSeed,
                    0u,
                    kRandomSaltShapeOffsetRadius)) * radius;
                sample.offset = sample.direction * distance;
                return sample;
            }
            case ParticleShape::Box: {
                const MATH::Vec3& halfExtents =
                    submission.simulation.shapeBoxHalfExtents;
                const float x = Lerp(
                    -halfExtents.x,
                    halfExtents.x,
                    PropertyRandom01(
                        particleSeed, 0u, kRandomSaltShapeDirectionY));
                const float y = Lerp(
                    -halfExtents.y,
                    halfExtents.y,
                    PropertyRandom01(
                        particleSeed, 0u, kRandomSaltShapeDirectionAngle));
                const float z = Lerp(
                    -halfExtents.z,
                    halfExtents.z,
                    PropertyRandom01(
                        particleSeed, 0u, kRandomSaltShapeOffsetRadius));
                sample.offset = { x, y, z };
                sample.direction = { 0.0f, 1.0f, 0.0f };
                return sample;
            }
            case ParticleShape::Cylinder: {
                constexpr float kTau = 6.28318530717958647692f;
                const float angle = PropertyRandom01(
                    particleSeed,
                    0u,
                    kRandomSaltShapeDirectionAngle) * kTau;
                const float distance = std::sqrt(PropertyRandom01(
                    particleSeed,
                    0u,
                    kRandomSaltShapeOffsetRadius)) * radius;
                const float halfHeight =
                    submission.simulation.shapeCylinderHeight * 0.5f;
                const float y = Lerp(
                    -halfHeight,
                    halfHeight,
                    PropertyRandom01(
                        particleSeed, 0u, kRandomSaltShapeDirectionY));
                sample.offset = {
                    std::cos(angle) * distance,
                    y,
                    std::sin(angle) * distance,
                };
                sample.direction = { 0.0f, 1.0f, 0.0f };
                return sample;
            }
            case ParticleShape::Circle: {
                constexpr float kTau = 6.28318530717958647692f;
                const float angle = PropertyRandom01(
                    particleSeed,
                    0u,
                    kRandomSaltShapeDirectionAngle) * kTau;
                const float innerRadius = std::clamp(
                    submission.simulation.shapeCircleInnerRadius,
                    0.0f,
                    radius);
                float distance = radius;
                if (submission.simulation.shapeCircleEmitOnEdge == 0u &&
                    radius > innerRadius) {
                    const float u = PropertyRandom01(
                        particleSeed, 0u, kRandomSaltShapeOffsetRadius);
                    distance = std::sqrt(std::lerp(
                        innerRadius * innerRadius,
                        radius * radius,
                        u));
                }
                sample.offset = {
                    std::cos(angle) * distance,
                    0.0f,
                    std::sin(angle) * distance,
                };
                sample.direction = { 0.0f, 1.0f, 0.0f };
                return sample;
            }
            case ParticleShape::Edge: {
                const float halfLength =
                    submission.simulation.shapeEdgeLength * 0.5f;
                const float x = Lerp(
                    -halfLength,
                    halfLength,
                    PropertyRandom01(
                        particleSeed, 0u, kRandomSaltShapeDirectionY));
                sample.offset = { x, 0.0f, 0.0f };
                sample.direction = { 0.0f, 1.0f, 0.0f };
                return sample;
            }
            case ParticleShape::Point:
            default:
                return sample;
            }
        }

        void RefreshPresentation(
            CpuParticleState& particle,
            const ParticleEmitterSubmission& submission) noexcept {

            const float normalizedAge = particle.lifetimeSeconds > 0.0f
                ? std::clamp(
                    particle.ageSeconds / particle.lifetimeSeconds,
                    0.0f,
                    1.0f)
                : 1.0f;
            const MATH::Vec3 localPosition = EvaluateAnimated(
                submission.simulation.position,
                particle.positionAnimation,
                particle.ageSeconds,
                normalizedAge);
            particle.position = particle.baseWorldPosition +
                particle.motionPosition + Rotate(
                    particle.emitterRotation,
                    Multiply(localPosition, particle.emitterScale));
            particle.rotationRadians = EvaluateAnimated(
                submission.simulation.rotationRadians,
                particle.rotationAnimation,
                particle.ageSeconds,
                normalizedAge);
            particle.scale = Multiply(
                EvaluateAnimated(
                    submission.simulation.scale,
                    particle.scaleAnimation,
                    particle.ageSeconds,
                    normalizedAge),
                particle.emitterScale) *
                submission.overrides.scaleMultiplier;
            particle.baseSize = 1.0f;
            particle.size = (std::abs(particle.scale.x) +
                std::abs(particle.scale.y)) * 0.5f;
            const MATH::Vec4 color = EvaluateAnimated(
                submission.simulation.color,
                particle.colorAnimation,
                normalizedAge);
            particle.color = {
                color.x * submission.overrides.colorMultiplier.x,
                color.y * submission.overrides.colorMultiplier.y,
                color.z * submission.overrides.colorMultiplier.z,
                color.w * submission.overrides.colorMultiplier.w,
            };
        }

        void SimulateExisting(
            CpuParticleEmitterState& emitter,
            const ParticleEmitterSubmission& submission) {

            const float dt = (std::max)(0.0f, submission.deltaSeconds);
            const float drag = (std::max)(0.0f, submission.simulation.drag);
            const float damping = std::exp(-drag * dt);
            const MATH::Vec3 gravity = submission.simulation.gravity *
                submission.overrides.gravityMultiplier;
            for (CpuParticleState& particle : emitter.particles) {
                if (!particle.alive) continue;
                particle.ageSeconds += dt;
                if (particle.ageSeconds >= particle.lifetimeSeconds) {
                    particle.alive = false;
                    continue;
                }
                particle.velocity =
                    (particle.velocity + gravity * dt) * damping;
                particle.motionPosition = particle.motionPosition +
                    particle.velocity * dt;
                RefreshPresentation(particle, submission);
            }
        }

        void Spawn(
            CpuParticleEmitterState& emitter,
            const ParticleEmitterSubmission& submission) {

            if (emitter.particles.empty()) return;
            for (uint32_t count = 0u;
                count < submission.spawnCount;
                ++count) {
                CpuParticleState& particle =
                    emitter.particles[emitter.spawnCursor];
                const uint32_t particleSlot = emitter.spawnCursor;
                emitter.spawnCursor = (emitter.spawnCursor + 1u) %
                    static_cast<uint32_t>(emitter.particles.size());
                const uint32_t particleSeed = HashRandom(
                    submission.randomSeed ^
                    (particleSlot * 747796405u));
                particle = {};
                particle.alive = true;
                particle.seed = particleSeed;
                particle.lifetimeSeconds = SampleDistribution(
                    submission.simulation.lifetime,
                    particleSeed,
                    kRandomSaltLifetime);
                const float radius = SampleDistribution(
                    submission.simulation.shapeRadius,
                    particleSeed,
                    kRandomSaltShapeRadius);
                const SpawnShapeSample shape = SampleSpawnShape(
                    submission,
                    particleSeed,
                    (std::max)(radius, 0.0f));
                particle.emitterRotation = submission.worldRotation;
                particle.emitterScale = submission.worldScale;
                particle.baseWorldPosition = submission.worldPosition +
                    Rotate(
                        submission.worldRotation,
                        Multiply(shape.offset, submission.worldScale));
                const MATH::Vec3 direction = Rotate(
                    submission.worldRotation,
                    shape.direction);
                const float radialSpeed = SampleDistribution(
                    submission.simulation.radialSpeed,
                    particleSeed,
                    kRandomSaltRadialSpeed) *
                    submission.overrides.radialSpeedMultiplier;
                particle.velocity = direction * radialSpeed;
                particle.positionAnimation = ResolveAnimated(
                    submission.simulation.position,
                    particleSeed,
                    kRandomSaltPosition);
                particle.rotationAnimation = ResolveAnimated(
                    submission.simulation.rotationRadians,
                    particleSeed,
                    kRandomSaltRotation);
                particle.scaleAnimation = ResolveAnimated(
                    submission.simulation.scale,
                    particleSeed,
                    kRandomSaltScale);
                particle.colorAnimation = ResolveAnimated(
                    submission.simulation.color,
                    particleSeed,
                    kRandomSaltColor);
                RefreshPresentation(particle, submission);
            }
        }
    }

    void ParticleCpuSimulation::Apply(const ParticleRenderInput& input) {
        for (const ParticleEmitterInstanceKey key : input.retiredEmitters) {
            emitters_.erase(key);
        }
        for (const ParticleEmitterSubmission& submission : input.emitters) {
            auto [found, inserted] = emitters_.try_emplace(submission.key);
            CpuParticleEmitterState& emitter = found->second;
            if (inserted || submission.resetSimulation ||
                emitter.particles.size() !=
                    submission.simulation.capacity) {
                emitter = {};
                emitter.key = submission.key;
                emitter.particles.resize(submission.simulation.capacity);
            }
            SimulateExisting(emitter, submission);
            Spawn(emitter, submission);
        }
    }

    void ParticleCpuSimulation::Reset() noexcept {
        emitters_.clear();
    }

    const CpuParticleEmitterState* ParticleCpuSimulation::Find(
        ParticleEmitterInstanceKey key) const noexcept {

        const auto found = emitters_.find(key);
        return found == emitters_.end() ? nullptr : &found->second;
    }

    size_t ParticleCpuSimulation::GetEmitterCount() const noexcept {
        return emitters_.size();
    }

    size_t ParticleCpuSimulation::GetAliveParticleCount(
        ParticleEmitterInstanceKey key) const noexcept {

        const CpuParticleEmitterState* emitter = Find(key);
        if (emitter == nullptr) return 0u;
        return static_cast<size_t>(std::count_if(
            emitter->particles.begin(),
            emitter->particles.end(),
            [](const CpuParticleState& particle) {
                return particle.alive;
            }));
    }

    size_t ParticleCpuSimulation::KeyHash::operator()(
        const ParticleEmitterInstanceKey& key) const noexcept {

        uint64_t hash = key.playbackNamespace;
        hash ^= static_cast<uint64_t>(key.playbackSlot) +
            0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<uint64_t>(key.playbackGeneration) +
            0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        hash ^= static_cast<uint64_t>(key.emitterIndex) +
            0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
        return static_cast<size_t>(hash);
    }

} // namespace HIKARI::RENDER3D::PARTICLES
