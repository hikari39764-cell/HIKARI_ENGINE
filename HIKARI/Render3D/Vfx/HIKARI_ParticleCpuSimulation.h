#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <span>

#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::RENDER3D::PARTICLES {

    float EvaluateParticleCurve(std::span<const ParticleCurveKey> keys,
        ParticleCurveInterpolation interpolation, float time, float randomOffset = 0) noexcept;
    float IntegrateParticleCurve(std::span<const ParticleCurveKey> keys,
        ParticleCurveInterpolation interpolation, float time) noexcept;

    struct LayerAnimationSample {
        MATH::Vec3 position{};
        MATH::Vec3 rotationRadians{};
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    LayerAnimationSample EvaluateLayerAnimation(
        const ParticleEmitterSimulationParameters& animation,
        float seconds, float duration, uint32_t seed) noexcept;

    struct CpuResolvedAnimatedFloat3 {
        ParticleValueMode mode = ParticleValueMode::Fixed;
        MATH::Vec3 initial{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 velocity{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 acceleration{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 easingStart{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 easingMiddle{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 easingEnd{ 0.0f, 0.0f, 0.0f };
        ParticleEasingFunction easingFunction =
            ParticleEasingFunction::EaseInOut;
        bool hasEasingMiddle = false;
        std::array<float, 3u> curveRandomOffsets{};
    };

    struct CpuResolvedAnimatedColor {
        ParticleColorMode mode = ParticleColorMode::Fixed;
        MATH::Vec4 fixed{ 1.0f, 1.0f, 1.0f, 1.0f };
        MATH::Vec4 easingStart{ 1.0f, 1.0f, 1.0f, 1.0f };
        MATH::Vec4 easingMiddle{ 1.0f, 1.0f, 1.0f, 1.0f };
        MATH::Vec4 easingEnd{ 1.0f, 1.0f, 1.0f, 1.0f };
        ParticleEasingFunction easingFunction =
            ParticleEasingFunction::EaseInOut;
        bool hasEasingMiddle = false;
        std::array<float, 4u> curveRandomOffsets{};
    };

    struct CpuParticleState {
        bool alive = false;
        uint32_t seed = 0u;
        float ageSeconds = 0.0f;
        float lifetimeSeconds = 0.0f;
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 velocity{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 rotationRadians{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
        float baseSize = 1.0f;
        float size = 1.0f;
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };

        MATH::Vec3 baseWorldPosition{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 motionPosition{ 0.0f, 0.0f, 0.0f };
        MATH::Quat emitterRotation = MATH::Quat::Identity();
        MATH::Vec3 emitterScale{ 1.0f, 1.0f, 1.0f };
        CpuResolvedAnimatedFloat3 positionAnimation{};
        CpuResolvedAnimatedFloat3 rotationAnimation{};
        CpuResolvedAnimatedFloat3 scaleAnimation{};
        CpuResolvedAnimatedColor colorAnimation{};
    };

    struct CpuParticleEmitterState {
        ParticleEmitterInstanceKey key{};
        uint32_t spawnCursor = 0u;
        std::vector<CpuParticleState> particles{};
    };

    class ParticleCpuSimulation final {
    public:
        void Apply(const ParticleRenderInput& input);
        void Reset() noexcept;

        const CpuParticleEmitterState* Find(
            ParticleEmitterInstanceKey key) const noexcept;
        size_t GetEmitterCount() const noexcept;
        size_t GetAliveParticleCount(
            ParticleEmitterInstanceKey key) const noexcept;

    private:
        struct KeyHash {
            size_t operator()(
                const ParticleEmitterInstanceKey& key) const noexcept;
        };

        std::unordered_map<
            ParticleEmitterInstanceKey,
            CpuParticleEmitterState,
            KeyHash> emitters_{};
    };

} // namespace HIKARI::RENDER3D::PARTICLES
