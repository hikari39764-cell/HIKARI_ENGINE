#include "Assets/Vfx/HIKARI_VfxProgram.h"

#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace HIKARI::VFX {
    namespace {
        using namespace RENDER3D::PARTICLES;

        bool IsFinite(float value) noexcept {
            return std::isfinite(value);
        }

        bool IsFinite(const MATH::Vec3& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y) &&
                IsFinite(value.z);
        }

        bool IsFinite(const MATH::Vec4& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y) &&
                IsFinite(value.z) && IsFinite(value.w);
        }

        bool IsValidDistribution(
            const ParticleDistributionFloat& value) noexcept {

            return IsFinite(value.minimum) && IsFinite(value.maximum) &&
                (value.mode == ParticleDistributionMode::Constant ||
                 (value.mode == ParticleDistributionMode::RandomRange &&
                  value.minimum <= value.maximum));
        }

        bool IsValidDistribution(
            const ParticleDistributionFloat3& value) noexcept {

            return IsFinite(value.minimum) && IsFinite(value.maximum) &&
                (value.mode == ParticleDistributionMode::Constant ||
                 (value.mode == ParticleDistributionMode::RandomRange &&
                  value.minimum.x <= value.maximum.x &&
                  value.minimum.y <= value.maximum.y &&
                  value.minimum.z <= value.maximum.z));
        }

        bool IsValidDistribution(
            const ParticleDistributionFloat4& value) noexcept {

            return IsFinite(value.minimum) && IsFinite(value.maximum) &&
                (value.mode == ParticleDistributionMode::Constant ||
                 (value.mode == ParticleDistributionMode::RandomRange &&
                  value.minimum.x <= value.maximum.x &&
                  value.minimum.y <= value.maximum.y &&
                  value.minimum.z <= value.maximum.z &&
                  value.minimum.w <= value.maximum.w));
        }

        bool IsValidCurve(const ParticleCurve& curve) noexcept {
            if (curve.keyCount == 0u ||
                curve.keyCount > kMaxParticleCurveKeys ||
                (curve.interpolation !=
                    ParticleCurveInterpolation::Linear &&
                 curve.interpolation !=
                    ParticleCurveInterpolation::Smooth &&
                 curve.interpolation !=
                    ParticleCurveInterpolation::Step) ||
                !IsValidDistribution(curve.randomOffset)) {
                return false;
            }
            float previousTime = -1.0f;
            for (uint32_t index = 0u; index < curve.keyCount; ++index) {
                const ParticleCurveKey& key = curve.keys[index];
                if (!IsFinite(key.time) || !IsFinite(key.value) ||
                    !IsFinite(key.inTangent) ||
                    !IsFinite(key.outTangent) || key.time < 0.0f ||
                    key.time > 1.0f || key.time <= previousTime) {
                    return false;
                }
                previousTime = key.time;
            }
            return true;
        }

        bool IsValidEasing(ParticleEasingFunction value) noexcept {
            return value == ParticleEasingFunction::Linear ||
                value == ParticleEasingFunction::EaseIn ||
                value == ParticleEasingFunction::EaseOut ||
                value == ParticleEasingFunction::EaseInOut;
        }

        bool IsValidAnimated(
            const ParticleAnimatedFloat3& value) noexcept {

            switch (value.mode) {
            case ParticleValueMode::Fixed:
                return IsValidDistribution(value.initial);
            case ParticleValueMode::Pva:
                return IsValidDistribution(value.initial) &&
                    IsValidDistribution(value.velocity) &&
                    IsValidDistribution(value.acceleration);
            case ParticleValueMode::Easing:
                return IsValidDistribution(value.easingStart) &&
                    (!value.hasEasingMiddle ||
                     IsValidDistribution(value.easingMiddle)) &&
                    IsValidDistribution(value.easingEnd) &&
                    IsValidEasing(value.easingFunction) &&
                    value.hasEasingMiddle <= 1u;
            case ParticleValueMode::Curve:
                return std::all_of(
                    value.curves.begin(),
                    value.curves.end(),
                    IsValidCurve);
            default:
                return false;
            }
        }

        bool IsValidAnimated(
            const ParticleAnimatedColor& value) noexcept {

            switch (value.mode) {
            case ParticleColorMode::Fixed:
            case ParticleColorMode::RandomRange:
                return IsValidDistribution(value.fixed);
            case ParticleColorMode::Easing:
                return IsValidDistribution(value.easingStart) &&
                    (!value.hasEasingMiddle ||
                     IsValidDistribution(value.easingMiddle)) &&
                    IsValidDistribution(value.easingEnd) &&
                    IsValidEasing(value.easingFunction) &&
                    value.hasEasingMiddle <= 1u;
            case ParticleColorMode::Curve:
                return std::all_of(
                    value.curves.begin(),
                    value.curves.end(),
                    IsValidCurve);
            case ParticleColorMode::Gradient:
                if (value.gradient.stopCount == 0u ||
                    value.gradient.stopCount > kMaxParticleColorStops) {
                    return false;
                }
                for (uint32_t index = 0u;
                     index < value.gradient.stopCount;
                     ++index) {
                    const ParticleColorStop& stop =
                        value.gradient.stops[index];
                    if (!IsFinite(stop.time) || !IsFinite(stop.color) ||
                        stop.time < 0.0f || stop.time > 1.0f ||
                        (index > 0u && stop.time <=
                            value.gradient.stops[index - 1u].time)) {
                        return false;
                    }
                }
                return true;
            default:
                return false;
            }
        }

        bool IsValidValue(const VfxValue& value) noexcept {
            switch (value.type) {
            case VfxValueType::Bool:
            case VfxValueType::Int:
                return true;
            case VfxValueType::Float:
            case VfxValueType::Float2:
            case VfxValueType::Float3:
            case VfxValueType::Float4:
            case VfxValueType::Color:
                return IsFinite(value.numeric);
            default:
                return false;
            }
        }

        VfxValueType ExpectedInputType(
            VfxRuntimeBindingSemantic semantic) noexcept {

            switch (semantic) {
            case VfxRuntimeBindingSemantic::EmitterEnabled:
                return VfxValueType::Bool;
            case VfxRuntimeBindingSemantic::ColorMultiplier:
                return VfxValueType::Color;
            case VfxRuntimeBindingSemantic::SpawnRateMultiplier:
            case VfxRuntimeBindingSemantic::RadialSpeedMultiplier:
            case VfxRuntimeBindingSemantic::ScaleMultiplier:
            case VfxRuntimeBindingSemantic::GravityMultiplier:
            default:
                return VfxValueType::Float;
            }
        }

        uint32_t StorageSize(ParticleRuntimePropertyType type) noexcept {
            switch (type) {
            case ParticleRuntimePropertyType::Bool:
            case ParticleRuntimePropertyType::Float:
                return sizeof(uint32_t);
            case ParticleRuntimePropertyType::Float4:
                return sizeof(MATH::Vec4);
            default:
                return 0u;
            }
        }
    }

    bool ValidateVfxProgram(
        const VfxProgram& program,
        std::string& outError) {

        if (program.version != kVfxProgramVersion ||
            program.sourceVersion != kVfxSourceVersion ||
            program.sourceHash == 0u || program.programId.empty() ||
            program.programId.size() > kMaximumVfxProgramStringBytes ||
            program.name.empty() ||
            program.name.size() > kMaximumVfxProgramStringBytes ||
            program.emitters.empty() ||
            !IsFinite(program.durationSeconds) ||
            program.durationSeconds <= 0.0f ||
            !IsFinite(program.maximumParticleLifetimeSeconds) ||
            program.maximumParticleLifetimeSeconds <= 0.0f) {
            outError = "VFX program header fields are invalid";
            return false;
        }
        if (program.inputs.size() > kMaximumVfxProgramInputs ||
            program.emitters.size() > kMaximumVfxProgramEmitters ||
            program.dependencyGuids.size() >
                kMaximumVfxProgramDependencies) {
            outError = "VFX program table exceeds the runtime limit";
            return false;
        }

        std::unordered_set<std::string> inputIds{};
        for (const VfxProgramInput& input : program.inputs) {
            if (input.id.empty() ||
                input.id.size() > kMaximumVfxProgramStringBytes ||
                !inputIds.emplace(input.id).second ||
                input.defaultValue.type != input.type ||
                !IsValidValue(input.defaultValue)) {
                outError = "VFX program input table is invalid";
                return false;
            }
        }

        uint64_t totalCapacity = 0u;
        uint64_t totalInputLinks = 0u;
        std::unordered_set<std::string> emitterIds{};
        for (uint32_t index = 0u;
             index < program.emitters.size();
             ++index) {
            const VfxProgramEmitter& emitter = program.emitters[index];
            const ParticleEmitterSimulationParameters& simulation =
                emitter.simulation;
            const float maximumLifetime =
                simulation.lifetime.mode ==
                    ParticleDistributionMode::RandomRange
                ? simulation.lifetime.maximum
                : simulation.lifetime.minimum;
            if (emitter.emitterIndex != index ||
                emitter.sourceEmitterId.empty() ||
                emitter.sourceEmitterId.size() >
                    kMaximumVfxProgramStringBytes ||
                !emitterIds.emplace(emitter.sourceEmitterId).second ||
                (emitter.parentEmitterIndex != kInvalidVfxProgramIndex &&
                 emitter.parentEmitterIndex >= program.emitters.size()) ||
                emitter.parentEmitterIndex == index ||
                !IsFinite(emitter.transform.position) ||
                !IsFinite(emitter.transform.rotationEulerDegrees) ||
                !IsFinite(emitter.transform.scale) ||
                emitter.transform.scale.x == 0.0f ||
                emitter.transform.scale.y == 0.0f ||
                emitter.transform.scale.z == 0.0f ||
                simulation.capacity == 0u ||
                simulation.capacity > kGpuParticlePoolCapacity ||
                !IsValidDistribution(simulation.lifetime) ||
                simulation.lifetime.minimum <= 0.0f ||
                maximumLifetime >
                    program.maximumParticleLifetimeSeconds ||
                !IsValidDistribution(simulation.shapeRadius) ||
                (simulation.shape != ParticleShape::Point &&
                 simulation.shape != ParticleShape::Sphere &&
                 simulation.shape != ParticleShape::Cone &&
                 simulation.shape != ParticleShape::Box &&
                 simulation.shape != ParticleShape::Cylinder &&
                 simulation.shape != ParticleShape::Circle &&
                 simulation.shape != ParticleShape::Edge) ||
                !IsFinite(simulation.coneAngleRadians) ||
                simulation.coneAngleRadians < 0.0f ||
                simulation.coneAngleRadians > 3.14159265358979323846f ||
                !IsValidDistribution(simulation.radialSpeed) ||
                !IsValidAnimated(simulation.position) ||
                !IsValidAnimated(simulation.rotationRadians) ||
                !IsValidAnimated(simulation.scale) ||
                !IsFinite(simulation.gravity) ||
                !IsFinite(simulation.drag) || simulation.drag < 0.0f ||
                !IsValidAnimated(simulation.color) ||
                !IsFinite(emitter.spawnRatePerSecond) ||
                emitter.spawnRatePerSecond < 0.0f ||
                emitter.burstCount > simulation.capacity ||
                emitter.authoredOverrides.enabled > 1u ||
                !IsFinite(
                    emitter.authoredOverrides.spawnRateMultiplier) ||
                !IsFinite(
                    emitter.authoredOverrides.radialSpeedMultiplier) ||
                !IsFinite(
                    emitter.authoredOverrides.scaleMultiplier) ||
                !IsFinite(
                    emitter.authoredOverrides.gravityMultiplier) ||
                !IsFinite(
                    emitter.authoredOverrides.colorMultiplier) ||
                (emitter.render.materialMode !=
                    ParticleMaterialMode::StandardUnlit &&
                 emitter.render.materialMode !=
                    ParticleMaterialMode::CustomMaterial) ||
                (emitter.render.blendMode != ParticleBlendMode::Alpha &&
                 emitter.render.blendMode !=
                    ParticleBlendMode::Additive) ||
                (emitter.render.shape != ParticleRenderShape::Sprite &&
                 emitter.render.shape != ParticleRenderShape::Ribbon &&
                 emitter.render.shape != ParticleRenderShape::Ring) ||
                (emitter.render.billboardMode !=
                    ParticleBillboardMode::RotatedBillboard &&
                 emitter.render.billboardMode !=
                    ParticleBillboardMode::FixedYAxis &&
                 emitter.render.billboardMode !=
                    ParticleBillboardMode::DirectionalBillboard &&
                 emitter.render.billboardMode !=
                    ParticleBillboardMode::Fixed) ||
                emitter.render.ribbonSegmentCount < 2u ||
                emitter.render.ribbonSegmentCount > 64u ||
                emitter.render.ringSegmentCount < 3u ||
                emitter.render.ringSegmentCount > 64u ||
                emitter.render.flipbookColumns == 0u ||
                emitter.render.flipbookColumns > 64u ||
                emitter.render.flipbookRows == 0u ||
                emitter.render.flipbookRows > 64u ||
                emitter.render.flipbookFrameCount == 0u ||
                emitter.render.flipbookFrameCount >
                    static_cast<uint64_t>(
                        emitter.render.flipbookColumns) *
                    static_cast<uint64_t>(
                        emitter.render.flipbookRows) ||
                !IsFinite(
                    emitter.render.flipbookFramesPerSecond) ||
                emitter.render.flipbookFramesPerSecond < 0.0f ||
                emitter.render.flipbookFramesPerSecond > 240.0f ||
                (!emitter.render.colorTextureAssetGuid.empty() &&
                 (emitter.render.colorTextureAssetGuid.size() >
                        kMaximumVfxProgramStringBytes ||
                  (!IsVfxBuiltinTextureAssetId(
                        emitter.render.colorTextureAssetGuid) &&
                   !IsValidAssetGuid(
                        emitter.render.colorTextureAssetGuid)))) ||
                (!emitter.render.materialAssetGuid.empty() &&
                 (emitter.render.materialAssetGuid.size() >
                        kMaximumVfxProgramStringBytes ||
                  !IsValidAssetGuid(
                    emitter.render.materialAssetGuid)))) {
                outError = "VFX program emitter table is invalid";
                return false;
            }
            totalCapacity += simulation.capacity;

            if (emitter.inputLinks.size() >
                kMaximumVfxProgramLinksPerEmitter) {
                outError = "VFX program input link table is too large";
                return false;
            }
            totalInputLinks += emitter.inputLinks.size();
            if (totalInputLinks > kMaximumVfxProgramTableItems) {
                outError =
                    "VFX program total input link table is too large";
                return false;
            }
            std::unordered_set<uint32_t> boundSemantics{};
            for (const VfxProgramInputLink& link : emitter.inputLinks) {
                if (link.semantic <
                        VfxRuntimeBindingSemantic::EmitterEnabled ||
                    link.semantic >
                        VfxRuntimeBindingSemantic::ColorMultiplier) {
                    outError = "VFX program input semantic is invalid";
                    return false;
                }
                const uint32_t semantic =
                    static_cast<uint32_t>(link.semantic);
                const VfxRuntimePropertyLayout expected =
                    ResolveVfxRuntimePropertyLayout(link.semantic);
                const uint32_t storageSize = StorageSize(
                    expected.storageType);
                if (!boundSemantics.emplace(semantic).second ||
                    link.inputIndex >= program.inputs.size() ||
                    link.inputType !=
                        program.inputs[link.inputIndex].type ||
                    !IsVfxInputTypeCompatible(
                        link.inputType,
                        ExpectedInputType(link.semantic)) ||
                    link.byteOffset != expected.byteOffset ||
                    link.storageType != expected.storageType ||
                    storageSize == 0u ||
                    expected.byteOffset >
                        sizeof(ParticleEmitterRuntimeOverrides) -
                            storageSize ||
                    !IsFinite(link.scale) || !IsFinite(link.bias)) {
                    outError = "VFX program input link is invalid";
                    return false;
                }
            }
        }
        if (totalCapacity > kGpuParticlePoolCapacity) {
            outError = "VFX program particle capacity exceeds the pool";
            return false;
        }

        std::unordered_set<uint32_t> visited{};
        for (uint32_t emitterIndex = 0u;
             emitterIndex < program.emitters.size();
             ++emitterIndex) {
            visited.clear();
            uint32_t current = emitterIndex;
            while (current != kInvalidVfxProgramIndex) {
                if (current >= program.emitters.size() ||
                    !visited.emplace(current).second) {
                    outError =
                        "VFX program emitter hierarchy is invalid";
                    return false;
                }
                current =
                    program.emitters[current].parentEmitterIndex;
            }
        }

        std::unordered_set<std::string> dependencies{};
        for (const AssetGuid& dependency : program.dependencyGuids) {
            if (!dependency.IsValid() ||
                !dependencies.emplace(dependency.value).second) {
                outError = "VFX program dependency table is invalid";
                return false;
            }
        }
        outError.clear();
        return true;
    }

} // namespace HIKARI::VFX
