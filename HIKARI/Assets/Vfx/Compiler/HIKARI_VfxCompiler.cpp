#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Assets/HIKARI_AssetTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <unordered_map>

#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
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

        ParticleMaterialMode ConvertMaterialMode(
            VfxMaterialMode mode) noexcept {

            switch (mode) {
            case VfxMaterialMode::CustomMaterial: return ParticleMaterialMode::CustomMaterial;
            case VfxMaterialMode::StandardLit: return ParticleMaterialMode::StandardLit;
            case VfxMaterialMode::CustomLit: return ParticleMaterialMode::CustomLit;
            default: return ParticleMaterialMode::StandardUnlit;
            }
        }

        ParticleDepthMode ConvertDepthMode(
            VfxParticleDepthMode mode) noexcept {

            switch (mode) {
            case VfxParticleDepthMode::SoftIntersection:
                return ParticleDepthMode::SoftIntersection;
            case VfxParticleDepthMode::ContactSurface:
                return ParticleDepthMode::ContactSurface;
            case VfxParticleDepthMode::ForegroundAccent:
                return ParticleDepthMode::ForegroundAccent;
            case VfxParticleDepthMode::WorldOccluded:
            default:
                return ParticleDepthMode::WorldOccluded;
            }
        }



        uint32_t CompileParticleCapacity(
            const VfxAssetData& asset,
            const VfxEmitterData& emitter) noexcept {

            if (emitter.spawn.capacityMode ==
                VfxParticleCapacityMode::Fixed) {
                return emitter.spawn.maximumParticles;
            }

            const double maximumLifetime =
                emitter.basic.lifetime.mode ==
                    VfxDistributionMode::RandomRange
                ? emitter.basic.lifetime.maximum
                : emitter.basic.lifetime.minimum;
            const double emitterWindow = emitter.activeDuration > 0.0f
                ? static_cast<double>(emitter.activeDuration)
                : (std::max)(
                    0.0,
                    static_cast<double>(asset.durationSeconds) -
                        static_cast<double>(emitter.startDelay));
            const double continuousWindow = asset.loop
                ? maximumLifetime
                : (std::min)(
                    maximumLifetime,
                    emitterWindow);
            const uint64_t continuousCapacity =
                emitter.spawn.ratePerSecond > 0.0f
                ? static_cast<uint64_t>(std::ceil(
                    static_cast<double>(emitter.spawn.ratePerSecond) *
                    continuousWindow)) + 2u
                : 0u;

            uint64_t overlappingBursts = 1u;
            uint64_t burstTotal = emitter.spawn.burstTimeline.empty()
                ? emitter.spawn.burstCount
                : 0u;
            if (!emitter.spawn.burstTimeline.empty()) {
                for (const VfxBurstKeyData& key :
                     emitter.spawn.burstTimeline) {
                    burstTotal += key.count;
                }
            }
            if (asset.loop && burstTotal > 0u &&
                asset.durationSeconds > 0.0f) {
                overlappingBursts = (std::max)(
                    uint64_t{ 1u },
                    static_cast<uint64_t>(std::ceil(
                        maximumLifetime /
                        static_cast<double>(asset.durationSeconds))));
            }
            const uint64_t burstCapacity =
                burstTotal *
                overlappingBursts;
            const uint64_t required = (std::max)(
                uint64_t{ 1u },
                continuousCapacity + burstCapacity);
            // Match the GPU simulation/compaction thread-group granularity so
            // the authored automatic capacity reflects the actual pool range.
            constexpr uint64_t kAllocationGranularity = 64u;
            const uint64_t aligned =
                (required + kAllocationGranularity - 1u) /
                kAllocationGranularity * kAllocationGranularity;
            return static_cast<uint32_t>((std::min)(
                aligned,
                static_cast<uint64_t>(
                    emitter.spawn.maximumParticles)));
        }

        std::vector<VfxBurstKeyData> CompileBurstTimeline(
            const VfxSpawnModuleData& source) {

            auto keys = source.burstTimeline;
            if (keys.empty() && source.burstCount > 0u) {
                keys.push_back({
                    source.id + ".burst.legacy",
                    0.0f,
                    source.burstCount });
            }
            std::sort(
                keys.begin(),
                keys.end(),
                [](const auto& left, const auto& right) {
                    if (left.time != right.time) {
                        return left.time < right.time;
                    }
                    return left.id < right.id;
                });
            return keys;
        }

        uint32_t LegacyBurstCount(
            const std::vector<VfxBurstKeyData>& keys) noexcept {

            const auto found = std::find_if(
                keys.begin(), keys.end(),
                [](const VfxBurstKeyData& key) {
                    return key.time == 0.0f;
                });
            return found == keys.end() ? 0u : found->count;
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
        case VfxRenderShapeType::MeshBox:
            return ParticleRenderShape::MeshBox;
        case VfxRenderShapeType::MeshSphere:
            return ParticleRenderShape::MeshSphere;
        case VfxRenderShapeType::MeshCylinder:
            return ParticleRenderShape::MeshCylinder;
        case VfxRenderShapeType::MeshCone:
            return ParticleRenderShape::MeshCone;
        case VfxRenderShapeType::MeshHemisphere:
            return ParticleRenderShape::MeshHemisphere;
        case VfxRenderShapeType::CrossPlane:
            return ParticleRenderShape::CrossPlane;
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
            emitter.kind = source.kind;
            emitter.history = source.history;
            emitter.animationTiming = source.animationTiming;
            emitter.emitterIndex = emitterIndex;
            const auto parent = emitterIndices.find(source.parentEmitterId);
            if (parent != emitterIndices.end()) {
                emitter.parentEmitterIndex = parent->second;
            }
            emitter.transform = source.emitterTransform.transform;
            emitter.startDelay = source.startDelay;
            emitter.activeDuration = source.activeDuration;
            emitter.attachment = source.attachment;
            emitter.spawnRatePerSecond = source.spawn.ratePerSecond;
            emitter.burstTimeline = CompileBurstTimeline(source.spawn);
            emitter.burstCount = LegacyBurstCount(emitter.burstTimeline);
            emitter.authoredOverrides.enabled = source.basic.enabled ? 1u : 0u;
            emitter.simulation.capacity =
                CompileParticleCapacity(asset, source);
            emitter.simulation.lifetime = CompileDistribution(
                source.basic.lifetime);
            if (source.kind == VfxLayerKind::Particles) program.maximumParticleLifetimeSeconds = (std::max)(
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
            emitter.simulation.localSimulation = source.basic.localSimulation ? 1u : 0u;
            emitter.simulation.collisionPlaneEnabled = source.forces.collisionPlaneEnabled ? 1u : 0u;
            emitter.simulation.collisionPlaneHeight = source.forces.collisionPlaneHeight;
            emitter.simulation.restitution = source.forces.restitution;
            emitter.simulation.friction = source.forces.friction;
            emitter.simulation.color = CompileAnimatedColor(source.color.value);
            if (source.kind != VfxLayerKind::Particles) {
                emitter.spawnRatePerSecond = 0.0f;
                emitter.burstCount = 0u;
                emitter.burstTimeline.clear();
                emitter.simulation.capacity = source.history.mode == VfxHistoryMode::None ? 1u : source.history.samples;
                if (source.history.mode != VfxHistoryMode::None)
                    program.maximumParticleLifetimeSeconds = (std::max)(program.maximumParticleLifetimeSeconds, source.history.duration);
            }
            emitter.render.materialMode = ConvertMaterialMode(
                source.renderCommon.materialMode);
            if (!source.effects.empty()) {
                auto material = std::make_shared<MaterialAssetDescriptor>();
                material->sourceData = BuildMaterialEffectStack(source.renderCommon.colorTextureAssetGuid,
                    source.effects, source.renderCommon.materialMode == VfxMaterialMode::StandardLit,
                    source.renderCommon.flipbookColumns, source.renderCommon.flipbookRows, VfxAnimatedMaterialParameters(source));
                material->compileResult = CompileMaterialAsset(material->sourceData);
                if (!material->compileResult.success) {
                    for (const auto& diagnostic : material->compileResult.diagnostics)
                        result.diagnostics.push_back({VfxDiagnosticSeverity::Error,
                            "Vfx.MaterialEffectCompile", diagnostic.message, source.id});
                    return result;
                }
                emitter.render.effectMaterial = std::move(material);
                emitter.render.effectMaterialKey = program.programId + ":" + source.id;
            }
            std::string animationError;
            if (!CompileVfxLayerCurves(source, emitter, animationError)) {
                result.diagnostics.push_back({VfxDiagnosticSeverity::Error,"Vfx.AnimationCompile",animationError,source.id});
                return result;
            }
            emitter.render.colorTextureAssetGuid =
                source.renderCommon.colorTextureAssetGuid.value;
            emitter.render.materialAssetGuid =
                source.renderCommon.materialAssetGuid.value;
            emitter.render.blendMode = ConvertBlend(
                source.renderCommon.blendMode);
            emitter.render.depthMode = ConvertDepthMode(
                source.renderCommon.depthMode);
            emitter.render.softIntersectionDistance =
                source.renderCommon.softIntersectionDistance;
            emitter.render.depthBias = source.renderCommon.depthBias;
            emitter.render.flipbookColumns =
                source.renderCommon.flipbookColumns;
            emitter.render.flipbookRows =
                source.renderCommon.flipbookRows;
            emitter.render.flipbookFrameCount =
                source.renderCommon.flipbookFrameCount;
            emitter.render.flipbookFramesPerSecond =
                source.renderCommon.flipbookFramesPerSecond;
            emitter.render.flipbookLoop =
                source.renderCommon.flipbookLoop;
            emitter.render.flipbookRandomStart =
                source.renderCommon.flipbookRandomStart;
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
            emitter.specialOutputs = source.specialOutputs;
            std::sort(
                emitter.specialOutputs.begin(),
                emitter.specialOutputs.end(),
                [](const VfxSpecialOutputData& left,
                    const VfxSpecialOutputData& right) {
                    return left.id < right.id;
                });

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
            if (IsCustomVfxMaterial(source.renderCommon.materialMode) &&
                source.renderCommon.materialAssetGuid.IsValid()) {
                program.dependencyGuids.push_back(
                    source.renderCommon.materialAssetGuid);
            }
            if (!IsCustomVfxMaterial(source.renderCommon.materialMode) &&
                source.renderCommon.colorTextureAssetGuid.IsValid() &&
                !IsVfxBuiltinTextureAssetId(
                    source.renderCommon.colorTextureAssetGuid.value)) {
                program.dependencyGuids.push_back(
                    source.renderCommon.colorTextureAssetGuid);
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
