#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {
    class AssetRegistry;

    namespace RENDER3D::MATERIAL {
        struct MaterialExternalValueSnapshot;
    }
}

namespace HIKARI::RENDER3D::PARTICLES {

    inline constexpr uint32_t kMaxParticleCurveKeys = 8u;
    inline constexpr uint32_t kMaxParticleColorStops = 8u;
    inline constexpr uint32_t kGpuParticlePoolCapacity = 65536u;
    inline constexpr uint32_t kMaximumVfxSeekSubsteps = 4096u;
    inline constexpr uint32_t
        kMaximumParticleEmitterSubmissionsPerFrame = 4096u;
    inline constexpr uint32_t
        kDefaultParticleEmitterUploadPageCapacity = 256u;

    // GPU material sources share one registry with scene surfaces. Particle
    // playback namespaces are tagged as odd identities so they cannot alias
    // the aligned pointer identities used by surface scene sources.
    inline uintptr_t MakeParticleMaterialSourceIdentity(
        uint64_t sourceIdentity,
        const void* fallbackIdentity) noexcept {
        const uintptr_t untagged = sourceIdentity != 0u
            ? static_cast<uintptr_t>(sourceIdentity)
            : reinterpret_cast<uintptr_t>(fallbackIdentity);
        return (untagged << 1u) | uintptr_t{1u};
    }

    struct ParticleEmitterInstanceKey {
        uint64_t playbackNamespace = 0u;
        uint32_t playbackSlot = 0u;
        uint32_t playbackGeneration = 0u;
        uint32_t emitterIndex = 0u;

        bool operator==(
            const ParticleEmitterInstanceKey&) const noexcept = default;
    };

    enum class ParticleShape : uint32_t {
        Point,
        Sphere,
        Cone,
        Box,
        Cylinder,
        Circle,
        Edge,
    };

    enum class ParticleRenderShape : uint32_t {
        Sprite,
        Ribbon,
        Ring,
    };

    enum class ParticleBlendMode : uint32_t {
        Alpha,
        Additive,
    };

    enum class ParticleMaterialMode : uint32_t {
        StandardUnlit,
        CustomMaterial,
    };

    enum class ParticleDepthMode : uint32_t {
        WorldOccluded,
        SoftIntersection,
        ContactSurface,
        ForegroundAccent,
    };

    enum class ParticleShaderStyle : uint32_t {
        Default,
        CombatCut,
        CombatImpact,
        CombatGround,
        ComicSmoke,
        ProjectileNeedle,
    };

    // Keep these values synchronized with VfxBillboardMode and the particle
    // shader constants. Values 0 and 1 preserve cooked-program compatibility.
    enum class ParticleBillboardMode : uint32_t {
        RotatedBillboard = 0u,
        FixedYAxis = 1u,
        DirectionalBillboard = 2u,
        Fixed = 3u,
    };

    static_assert(
        static_cast<uint32_t>(
            ParticleBillboardMode::RotatedBillboard) == 0u);
    static_assert(
        static_cast<uint32_t>(
            ParticleBillboardMode::FixedYAxis) == 1u);
    static_assert(
        static_cast<uint32_t>(
            ParticleBillboardMode::DirectionalBillboard) == 2u);
    static_assert(
        static_cast<uint32_t>(ParticleBillboardMode::Fixed) == 3u);

    enum class ParticleCurveInterpolation : uint32_t {
        Linear,
        Smooth,
        Step,
    };

    enum class ParticleDistributionMode : uint32_t {
        Constant,
        RandomRange,
    };

    enum class ParticleValueMode : uint32_t {
        Fixed,
        Pva,
        Easing,
        Curve,
    };

    enum class ParticleColorMode : uint32_t {
        Fixed,
        RandomRange,
        Easing,
        Curve,
        Gradient,
    };

    enum class ParticleEasingFunction : uint32_t {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut,
    };

    enum class ParticleRuntimePropertyType : uint32_t {
        Bool,
        Float,
        Float4,
    };

    struct ParticleDistributionFloat {
        ParticleDistributionMode mode =
            ParticleDistributionMode::Constant;
        float minimum = 0.0f;
        float maximum = 0.0f;
        uint32_t randomGroup = 0u;
    };

    struct ParticleDistributionFloat3 {
        ParticleDistributionMode mode =
            ParticleDistributionMode::Constant;
        MATH::Vec3 minimum{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 maximum{ 0.0f, 0.0f, 0.0f };
        uint32_t randomGroup = 0u;
    };

    struct ParticleDistributionFloat4 {
        ParticleDistributionMode mode =
            ParticleDistributionMode::Constant;
        MATH::Vec4 minimum{ 0.0f, 0.0f, 0.0f, 0.0f };
        MATH::Vec4 maximum{ 0.0f, 0.0f, 0.0f, 0.0f };
        uint32_t randomGroup = 0u;
    };

    struct ParticleCurveKey {
        float time = 0.0f;
        float value = 0.0f;
        float inTangent = 0.0f;
        float outTangent = 0.0f;
    };

    struct ParticleCurve {
        ParticleCurveInterpolation interpolation =
            ParticleCurveInterpolation::Linear;
        uint32_t keyCount = 0u;
        ParticleDistributionFloat randomOffset{};
        std::array<ParticleCurveKey, kMaxParticleCurveKeys> keys{};
    };

    struct ParticleColorStop {
        float time = 0.0f;
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct ParticleColorGradient {
        uint32_t stopCount = 0u;
        std::array<ParticleColorStop, kMaxParticleColorStops> stops{};
    };

    struct ParticleAnimatedFloat3 {
        ParticleValueMode mode = ParticleValueMode::Fixed;
        ParticleDistributionFloat3 initial{};
        ParticleDistributionFloat3 velocity{};
        ParticleDistributionFloat3 acceleration{};
        ParticleDistributionFloat3 easingStart{};
        ParticleDistributionFloat3 easingMiddle{};
        ParticleDistributionFloat3 easingEnd{};
        ParticleEasingFunction easingFunction =
            ParticleEasingFunction::EaseInOut;
        uint32_t hasEasingMiddle = 0u;
        std::array<ParticleCurve, 3u> curves{};
    };

    struct ParticleAnimatedColor {
        ParticleColorMode mode = ParticleColorMode::Fixed;
        ParticleDistributionFloat4 fixed{};
        ParticleDistributionFloat4 easingStart{};
        ParticleDistributionFloat4 easingMiddle{};
        ParticleDistributionFloat4 easingEnd{};
        ParticleEasingFunction easingFunction =
            ParticleEasingFunction::EaseInOut;
        uint32_t hasEasingMiddle = 0u;
        std::array<ParticleCurve, 4u> curves{};
        ParticleColorGradient gradient{};
    };

    struct ParticleEmitterSimulationParameters {
        uint32_t capacity = 0u;
        ParticleDistributionFloat lifetime{};
        ParticleShape shape = ParticleShape::Point;
        ParticleDistributionFloat shapeRadius{};
        float coneAngleRadians = 0.0f;
        MATH::Vec3 shapeBoxHalfExtents{ 0.5f, 0.5f, 0.5f };
        float shapeCylinderHeight = 1.0f;
        float shapeCircleInnerRadius = 0.0f;
        uint32_t shapeCircleEmitOnEdge = 0u;
        float shapeEdgeLength = 1.0f;
        ParticleDistributionFloat radialSpeed{};
        ParticleAnimatedFloat3 position{};
        ParticleAnimatedFloat3 rotationRadians{};
        ParticleAnimatedFloat3 scale{};
        MATH::Vec3 gravity{ 0.0f, 0.0f, 0.0f };
        float drag = 0.0f;
        ParticleAnimatedColor color{};
    };

    struct ParticleEmitterRuntimeOverrides {
        uint32_t enabled = 1u;
        float spawnRateMultiplier = 1.0f;
        float radialSpeedMultiplier = 1.0f;
        float scaleMultiplier = 1.0f;
        float gravityMultiplier = 1.0f;
        MATH::Vec4 colorMultiplier{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct ParticleEmitterRenderParameters {
        ParticleMaterialMode materialMode =
            ParticleMaterialMode::StandardUnlit;
        std::string colorTextureAssetGuid{};
        std::string materialAssetGuid{};
        ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
        ParticleRenderShape shape = ParticleRenderShape::Sprite;
        ParticleBillboardMode billboardMode =
            ParticleBillboardMode::RotatedBillboard;
        ParticleDepthMode depthMode = ParticleDepthMode::WorldOccluded;
        float softIntersectionDistance = 0.15f;
        float depthBias = 0.00035f;
        ParticleShaderStyle shaderStyle = ParticleShaderStyle::Default;
        MATH::Vec4 shaderOutlineColor{
            0.035f, 0.075f, 0.32f, 1.0f };
        MATH::Vec4 shaderCoreColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float shaderOutlineWidth = 0.12f;
        float shaderEdgeHardness = 0.88f;
        float shaderEmissiveIntensity = 0.0f;
        float shaderBreakupScale = 7.0f;
        float shaderBreakupStrength = 0.0f;
        uint32_t flipbookColumns = 1u;
        uint32_t flipbookRows = 1u;
        uint32_t flipbookFrameCount = 1u;
        float flipbookFramesPerSecond = 0.0f;
        bool flipbookLoop = true;
        bool flipbookRandomStart = false;
        uint32_t ribbonSegmentCount = 16u;
        float ribbonWidth = 0.1f;
        float ringOuterRadius = 1.0f;
        float ringInnerRadius = 0.5f;
        uint32_t ringSegmentCount = 32u;
        float ringStartAngleRadians = 0.0f;
        float ringEndAngleRadians = 6.28318530717958647692f;
    };

    struct ParticleEmitterSubmission {
        ParticleEmitterInstanceKey key{};
        uint32_t simulationOrdinal = 0u;
        bool drawAfterSimulation = true;
        bool resetSimulation = false;
        uint32_t spawnCount = 0u;
        uint32_t randomSeed = 0u;
        float deltaSeconds = 0.0f;
        float emitterAgeSeconds = 0.0f;
        MATH::Vec3 worldPosition{ 0.0f, 0.0f, 0.0f };
        MATH::Quat worldRotation = MATH::Quat::Identity();
        MATH::Vec3 worldScale{ 1.0f, 1.0f, 1.0f };
        ParticleEmitterRuntimeOverrides overrides{};
        ParticleEmitterSimulationParameters simulation{};
        ParticleEmitterRenderParameters render{};
    };

    struct ParticleRenderDeliveryToken {
        uint64_t sourceIdentity = 0u;
        uint64_t sequence = 0u;

        bool IsValid() const noexcept {
            return sourceIdentity != 0u && sequence != 0u;
        }

        bool operator==(
            const ParticleRenderDeliveryToken&) const noexcept = default;
    };

    struct ParticleRenderInput {
        // Stable owner identity used by the shared GPU material registry.
        // It must remain stable for the lifetime of the playback namespace.
        uint64_t sourceIdentity = 0u;
        // Monotonic delivery identity. Runtime playback retains an event-like
        // reset/spawn snapshot until the renderer reports this exact token as
        // recorded into a valid frame command list.
        uint64_t deliverySequence = 0u;
        uint64_t frameIndex = 0u;
        std::vector<ParticleEmitterInstanceKey> retiredEmitters{};
        std::vector<ParticleEmitterSubmission> emitters{};

        ParticleRenderDeliveryToken GetDeliveryToken() const noexcept {
            return { sourceIdentity, deliverySequence };
        }
    };

    // Immutable frame boundary consumed by the main GPU-driven renderer.
    // Particle simulation and drawing no longer own a public render entry
    // point; every view supplies this source through RenderViewContext.
    struct ParticleRenderFrameSource {
        const ParticleRenderInput* input = nullptr;
        const AssetRegistry* assetRegistry = nullptr;
        const MATERIAL::MaterialExternalValueSnapshot* externalValues =
            nullptr;

        bool HasDrawWork() const noexcept {
            return input != nullptr && !input->emitters.empty();
        }

        bool HasFrameWork() const noexcept {
            return input != nullptr &&
                (!input->emitters.empty() ||
                 !input->retiredEmitters.empty());
        }
    };

    static_assert(std::is_trivially_copyable_v<ParticleDistributionFloat>);
    static_assert(std::is_trivially_copyable_v<ParticleDistributionFloat3>);
    static_assert(std::is_trivially_copyable_v<ParticleDistributionFloat4>);
    static_assert(std::is_trivially_copyable_v<ParticleCurve>);
    static_assert(std::is_trivially_copyable_v<ParticleAnimatedFloat3>);
    static_assert(std::is_trivially_copyable_v<ParticleAnimatedColor>);
    static_assert(
        std::is_trivially_copyable_v<ParticleEmitterSimulationParameters>);
    static_assert(
        std::is_trivially_copyable_v<ParticleEmitterRuntimeOverrides>);

} // namespace HIKARI::RENDER3D::PARTICLES
