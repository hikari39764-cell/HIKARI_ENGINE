#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"

namespace HIKARI::VFX {
    namespace {
        using namespace RENDER3D::PARTICLES;

        uint64_t HashText(std::string_view text) noexcept {
            uint64_t hash = 14695981039346656037ull;
            for (const unsigned char value : text) {
                hash ^= value;
                hash *= 1099511628211ull;
            }
            return hash;
        }

        std::string MakeProgramId(uint64_t hash) {
            std::ostringstream stream{};
            stream << "hvfx:" << std::hex << std::setfill('0')
                << std::setw(16) << hash;
            return stream.str();
        }

        ParticleDistributionMode ConvertDistributionMode(
            VfxDistributionMode mode) noexcept {

            return mode == VfxDistributionMode::RandomRange
                ? ParticleDistributionMode::RandomRange
                : ParticleDistributionMode::Constant;
        }

        ParticleDistributionFloat CompileDistribution(
            const VfxFloatDistributionData& source,
            float scale = 1.0f) noexcept {

            return {
                ConvertDistributionMode(source.mode),
                source.minimum * scale,
                source.maximum * scale,
                source.randomGroup,
            };
        }

        ParticleDistributionFloat3 CompileDistribution(
            const VfxVec3DistributionData& source,
            float scale = 1.0f) noexcept {

            return {
                ConvertDistributionMode(source.mode),
                source.minimum * scale,
                source.maximum * scale,
                source.randomGroup,
            };
        }

        ParticleDistributionFloat4 CompileDistribution(
            const VfxColorDistributionData& source) noexcept {

            return {
                ConvertDistributionMode(source.mode),
                source.minimum,
                source.maximum,
                source.randomGroup,
            };
        }

        ParticleCurveInterpolation ConvertInterpolation(
            VfxCurveInterpolation interpolation) noexcept {

            switch (interpolation) {
            case VfxCurveInterpolation::Smooth:
                return ParticleCurveInterpolation::Smooth;
            case VfxCurveInterpolation::Step:
                return ParticleCurveInterpolation::Step;
            case VfxCurveInterpolation::Linear:
            default:
                return ParticleCurveInterpolation::Linear;
            }
        }

        ParticleEasingFunction ConvertEasing(
            VfxEasingFunction easing) noexcept {

            switch (easing) {
            case VfxEasingFunction::EaseIn:
                return ParticleEasingFunction::EaseIn;
            case VfxEasingFunction::EaseOut:
                return ParticleEasingFunction::EaseOut;
            case VfxEasingFunction::EaseInOut:
                return ParticleEasingFunction::EaseInOut;
            case VfxEasingFunction::Linear:
            default:
                return ParticleEasingFunction::Linear;
            }
        }

        ParticleCurve CompileCurve(
            const VfxCurveData& source,
            float valueScale = 1.0f) {

            ParticleCurve curve{};
            curve.interpolation = ConvertInterpolation(source.interpolation);
            curve.randomOffset = CompileDistribution(
                source.randomOffset,
                valueScale);
            auto keys = source.keys;
            std::sort(
                keys.begin(),
                keys.end(),
                [](const auto& left, const auto& right) {
                    if (left.time != right.time) {
                        return left.time < right.time;
                    }
                    return left.id < right.id;
                });
            curve.keyCount = static_cast<uint32_t>((std::min)(
                keys.size(),
                static_cast<size_t>(kMaxParticleCurveKeys)));
            for (uint32_t index = 0u; index < curve.keyCount; ++index) {
                curve.keys[index] = {
                    keys[index].time,
                    keys[index].value * valueScale,
                    keys[index].inTangent * valueScale,
                    keys[index].outTangent * valueScale,
                };
            }
            return curve;
        }

        ParticleColorGradient CompileGradient(
            const VfxColorGradientData& source) {

            ParticleColorGradient gradient{};
            auto stops = source.stops;
            std::sort(
                stops.begin(),
                stops.end(),
                [](const auto& left, const auto& right) {
                    if (left.time != right.time) {
                        return left.time < right.time;
                    }
                    return left.id < right.id;
                });
            gradient.stopCount = static_cast<uint32_t>((std::min)(
                stops.size(),
                static_cast<size_t>(kMaxParticleColorStops)));
            for (uint32_t index = 0u;
                index < gradient.stopCount;
                ++index) {
                gradient.stops[index] = {
                    stops[index].time,
                    stops[index].color,
                };
            }
            return gradient;
        }

        ParticleValueMode ConvertValueMode(
            VfxAnimatedValueMode mode) noexcept {

            switch (mode) {
            case VfxAnimatedValueMode::Pva: return ParticleValueMode::Pva;
            case VfxAnimatedValueMode::Easing:
                return ParticleValueMode::Easing;
            case VfxAnimatedValueMode::Curve:
                return ParticleValueMode::Curve;
            case VfxAnimatedValueMode::Fixed:
            default:
                return ParticleValueMode::Fixed;
            }
        }

        ParticleAnimatedFloat3 CompileAnimatedVec3(
            const VfxAnimatedVec3Data& source,
            float valueScale = 1.0f) {

            ParticleAnimatedFloat3 value{};
            value.mode = ConvertValueMode(source.mode);
            switch (source.mode) {
            case VfxAnimatedValueMode::Fixed:
                value.initial = CompileDistribution(
                    source.fixed,
                    valueScale);
                break;
            case VfxAnimatedValueMode::Pva:
                value.initial = CompileDistribution(
                    source.pva.initial,
                    valueScale);
                value.velocity = CompileDistribution(
                    source.pva.velocity,
                    valueScale);
                value.acceleration = CompileDistribution(
                    source.pva.acceleration,
                    valueScale);
                break;
            case VfxAnimatedValueMode::Easing:
                value.easingStart = CompileDistribution(
                    source.easing.start,
                    valueScale);
                value.easingMiddle = CompileDistribution(
                    source.easing.middle,
                    valueScale);
                value.easingEnd = CompileDistribution(
                    source.easing.end,
                    valueScale);
                value.easingFunction = ConvertEasing(
                    source.easing.function);
                value.hasEasingMiddle = source.easing.useMiddle ? 1u : 0u;
                break;
            case VfxAnimatedValueMode::Curve:
                value.curves = {
                    CompileCurve(source.curve.x, valueScale),
                    CompileCurve(source.curve.y, valueScale),
                    CompileCurve(source.curve.z, valueScale),
                };
                break;
            }
            return value;
        }

        ParticleAnimatedColor CompileAnimatedColor(
            const VfxAnimatedColorData& source) {

            ParticleAnimatedColor value{};
            switch (source.mode) {
            case VfxAnimatedColorMode::RandomRange:
                value.mode = ParticleColorMode::RandomRange;
                value.fixed = CompileDistribution(source.fixed);
                break;
            case VfxAnimatedColorMode::Easing:
                value.mode = ParticleColorMode::Easing;
                value.easingStart = CompileDistribution(source.easing.start);
                value.easingMiddle = CompileDistribution(source.easing.middle);
                value.easingEnd = CompileDistribution(source.easing.end);
                value.easingFunction = ConvertEasing(
                    source.easing.function);
                value.hasEasingMiddle = source.easing.useMiddle ? 1u : 0u;
                break;
            case VfxAnimatedColorMode::Curve:
                value.mode = ParticleColorMode::Curve;
                value.curves = {
                    CompileCurve(source.curve.r),
                    CompileCurve(source.curve.g),
                    CompileCurve(source.curve.b),
                    CompileCurve(source.curve.a),
                };
                break;
            case VfxAnimatedColorMode::Gradient:
                value.mode = ParticleColorMode::Gradient;
                value.gradient = CompileGradient(source.gradient);
                break;
            case VfxAnimatedColorMode::Fixed:
            default:
                value.mode = ParticleColorMode::Fixed;
                value.fixed = CompileDistribution(source.fixed);
                break;
            }
            return value;
        }

        ParticleShape ConvertShape(VfxShapeType shape) noexcept {
    switch (shape) {
    case VfxShapeType::Sphere: return ParticleShape::Sphere;
    case VfxShapeType::Cone: return ParticleShape::Cone;
    case VfxShapeType::Box: return ParticleShape::Box;
    case VfxShapeType::Cylinder: return ParticleShape::Cylinder;
    case VfxShapeType::Circle: return ParticleShape::Circle;
    case VfxShapeType::Edge: return ParticleShape::Edge;
    case VfxShapeType::Point:
    default: return ParticleShape::Point;
            }
        }

        ParticleBlendMode ConvertBlend(VfxBlendMode blend) noexcept {
            return blend == VfxBlendMode::Additive
                ? ParticleBlendMode::Additive
                : ParticleBlendMode::Alpha;
        }

        ParticleBillboardMode ConvertBillboard(
            VfxBillboardMode billboard) noexcept {

            switch (billboard) {
            case VfxBillboardMode::RotatedBillboard:
                return ParticleBillboardMode::RotatedBillboard;
            case VfxBillboardMode::FixedYAxis:
                return ParticleBillboardMode::FixedYAxis;
            case VfxBillboardMode::DirectionalBillboard:
                return ParticleBillboardMode::DirectionalBillboard;
            case VfxBillboardMode::Fixed:
            default:
                return ParticleBillboardMode::Fixed;
            }
        }

        ParticleRenderShape ConvertRenderShape(
        VfxRenderShapeType shape) noexcept {
        switch (shape) {
        case VfxRenderShapeType::Ribbon:
            return ParticleRenderShape::Ribbon;
        case VfxRenderShapeType::Ring:
            return ParticleRenderShape::Ring;
        case VfxRenderShapeType::Sprite:
        default:
            return ParticleRenderShape::Sprite;
        }
        }

    }

    VfxCompileResult CompileVfxAsset(const VfxAssetData& asset) {
        VfxCompileResult result{};
        result.diagnostics = ValidateVfxAsset(asset);
        if (HasVfxErrors(result.diagnostics)) {
            return result;
        }

        std::string canonicalSource{};
        std::string serializationError{};
        if (!SERIALIZATION::SerializeVfxSourceJson(
                asset,
                canonicalSource,
                serializationError)) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.CanonicalSerializationFailed",
                std::move(serializationError),
                {},
            });
            return result;
        }

        VfxProgram program{};
        program.sourceHash = HashText(canonicalSource);
        program.programId = MakeProgramId(program.sourceHash);
        program.name = asset.name;
        program.durationSeconds = asset.durationSeconds;
        program.maximumParticleLifetimeSeconds = 0.0f;
        program.loop = asset.loop;

        auto inputs = asset.inputs;
        std::sort(
            inputs.begin(),
            inputs.end(),
            [](const auto& left, const auto& right) {
                return left.id < right.id;
            });
        std::unordered_map<std::string, uint32_t> inputIndices{};
        for (uint32_t index = 0u; index < inputs.size(); ++index) {
            inputIndices.emplace(inputs[index].id, index);
            program.inputs.push_back({
                inputs[index].id,
                inputs[index].type,
                inputs[index].defaultValue,
            });
        }

        auto emitters = asset.emitters;
        std::sort(
            emitters.begin(),
            emitters.end(),
            [](const auto& left, const auto& right) {
                if (left.order != right.order) {
                    return left.order < right.order;
                }
                return left.id < right.id;
            });
        std::unordered_map<std::string, uint32_t> emitterIndices{};
        for (uint32_t index = 0u; index < emitters.size(); ++index) {
            emitterIndices.emplace(emitters[index].id, index);
        }

        constexpr float kDegreesToRadians =
            3.14159265358979323846f / 180.0f;
        for (uint32_t emitterIndex = 0u;
            emitterIndex < emitters.size();
            ++emitterIndex) {
            const VfxEmitterData& source = emitters[emitterIndex];
            VfxProgramEmitter emitter{};
            emitter.sourceEmitterId = source.id;
            emitter.emitterIndex = emitterIndex;
            const auto parent = emitterIndices.find(source.parentEmitterId);
            if (parent != emitterIndices.end()) {
                emitter.parentEmitterIndex = parent->second;
            }
            emitter.transform = source.emitterTransform.transform;
            emitter.spawnRatePerSecond = source.spawn.ratePerSecond;
            emitter.burstCount = source.spawn.burstCount;
            emitter.authoredOverrides.enabled = source.basic.enabled ? 1u : 0u;
            emitter.simulation.capacity = source.spawn.maximumParticles;
            emitter.simulation.lifetime = CompileDistribution(
                source.basic.lifetime);
            program.maximumParticleLifetimeSeconds = (std::max)(
                program.maximumParticleLifetimeSeconds,
                source.basic.lifetime.mode == VfxDistributionMode::RandomRange
                    ? source.basic.lifetime.maximum
                    : source.basic.lifetime.minimum);
            emitter.simulation.shape = ConvertShape(source.shape.type);
            emitter.simulation.shapeRadius = CompileDistribution(
                source.shape.radius);
            emitter.simulation.coneAngleRadians =
                source.shape.coneAngleDegrees * kDegreesToRadians;
            emitter.simulation.shapeBoxHalfExtents =
                source.shape.boxHalfExtents;
            emitter.simulation.shapeCylinderHeight =
                source.shape.cylinderHeight;
            emitter.simulation.shapeCircleInnerRadius =
                source.shape.circleInnerRadius;
            emitter.simulation.shapeCircleEmitOnEdge =
                source.shape.circleEmitOnEdge ? 1u : 0u;
            emitter.simulation.shapeEdgeLength =
                source.shape.edgeLength;
            emitter.simulation.radialSpeed = CompileDistribution(
                source.position.radialSpeed);
            emitter.simulation.position = CompileAnimatedVec3(
                source.position.value);
            emitter.simulation.rotationRadians = CompileAnimatedVec3(
                source.rotation.eulerDegrees,
                kDegreesToRadians);
            emitter.simulation.scale = CompileAnimatedVec3(
                source.scale.value);
            emitter.simulation.gravity = source.forces.gravity;
            emitter.simulation.drag = source.forces.drag;
            emitter.simulation.color = CompileAnimatedColor(source.color.value);
            emitter.render.materialAssetGuid =
                source.renderCommon.materialAssetGuid.value;
            emitter.render.blendMode = ConvertBlend(
                source.renderCommon.blendMode);
            emitter.render.depthTest = source.renderCommon.depthTest;
            emitter.render.shape = ConvertRenderShape(
                source.renderShape.type);
            emitter.render.billboardMode = ConvertBillboard(
                source.renderShape.billboardMode);
            emitter.render.ribbonSegmentCount =
                source.renderShape.ribbonSegmentCount;
            emitter.render.ribbonWidth =
                source.renderShape.ribbonWidth;
            emitter.render.ringOuterRadius =
                source.renderShape.ringOuterRadius;
            emitter.render.ringInnerRadius =
                source.renderShape.ringInnerRadius;
            emitter.render.ringSegmentCount =
                source.renderShape.ringSegmentCount;
            emitter.render.ringStartAngleRadians =
                source.renderShape.ringStartAngleDegrees *
                kDegreesToRadians;
            emitter.render.ringEndAngleRadians =
                source.renderShape.ringEndAngleDegrees *
                kDegreesToRadians;

            auto links = source.inputLinks;
            std::sort(
                links.begin(),
                links.end(),
                [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
            for (const VfxInputLinkData& link : links) {
                const VfxPropertySchema* schema =
                    ResolveVfxPropertySchema(source, link.target);
                const VfxRuntimePropertyLayout layout =
                    ResolveVfxRuntimePropertyLayout(
                        schema->runtimeSemantic);
                emitter.inputLinks.push_back({
                    inputIndices.at(link.inputId),
                    schema->runtimeSemantic,
                    layout.byteOffset,
                    layout.storageType,
                    inputs[inputIndices.at(link.inputId)].type,
                    link.scale,
                    link.bias,
                });
            }
            if (source.renderCommon.materialAssetGuid.IsValid()) {
                program.dependencyGuids.push_back(
                    source.renderCommon.materialAssetGuid);
            }
            program.emitters.push_back(std::move(emitter));
        }

        std::sort(
            program.dependencyGuids.begin(),
            program.dependencyGuids.end(),
            [](const AssetGuid& left, const AssetGuid& right) {
                return left.value < right.value;
            });
        program.dependencyGuids.erase(
            std::unique(
                program.dependencyGuids.begin(),
                program.dependencyGuids.end()),
            program.dependencyGuids.end());

        std::string programValidationError{};
        if (!ValidateVfxProgram(program, programValidationError)) {
            result.diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                "Vfx.ProgramValidationFailed",
                std::move(programValidationError),
                {},
            });
            return result;
        }

        result.program = std::move(program);
        result.success = true;
        return result;
    }

} // namespace HIKARI::VFX
