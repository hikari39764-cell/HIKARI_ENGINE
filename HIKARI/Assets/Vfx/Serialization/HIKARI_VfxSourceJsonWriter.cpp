#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJsonInternal.h"

#include <algorithm>
#include <utility>

namespace HIKARI::VFX::SERIALIZATION::INTERNAL {
    namespace {
        using Json = nlohmann::json;

        Json WriteVec3(const MATH::Vec3& value) {
            return Json::array({ value.x, value.y, value.z });
        }

        Json WriteVec4(const MATH::Vec4& value) {
            return Json::array({ value.x, value.y, value.z, value.w });
        }

        Json WriteValue(const VfxValue& value) {
            switch (value.type) {
            case VfxValueType::Bool: return value.boolean;
            case VfxValueType::Int: return value.integer;
            case VfxValueType::Float: return value.numeric.x;
            case VfxValueType::Float2:
                return Json::array({ value.numeric.x, value.numeric.y });
            case VfxValueType::Float3:
                return Json::array({
                    value.numeric.x,
                    value.numeric.y,
                    value.numeric.z,
                });
            case VfxValueType::Float4:
            case VfxValueType::Color:
            default:
                return WriteVec4(value.numeric);
            }
        }

        Json WriteDistribution(const VfxFloatDistributionData& value) {
            return {
                { "mode", ToString(value.mode) },
                { "minimum", value.minimum },
                { "maximum", value.maximum },
                { "randomGroup", value.randomGroup },
            };
        }

        Json WriteDistribution(const VfxVec3DistributionData& value) {
            return {
                { "mode", ToString(value.mode) },
                { "minimum", WriteVec3(value.minimum) },
                { "maximum", WriteVec3(value.maximum) },
                { "randomGroup", value.randomGroup },
            };
        }

        Json WriteDistribution(const VfxColorDistributionData& value) {
            return {
                { "mode", ToString(value.mode) },
                { "minimum", WriteVec4(value.minimum) },
                { "maximum", WriteVec4(value.maximum) },
                { "randomGroup", value.randomGroup },
            };
        }

        Json WriteCurve(VfxCurveData curve) {
            std::sort(
                curve.keys.begin(),
                curve.keys.end(),
                [](const auto& left, const auto& right) {
                    if (left.time != right.time) return left.time < right.time;
                    return left.id < right.id;
                });
            Json keys = Json::array();
            for (const VfxCurveKeyData& key : curve.keys) {
                keys.push_back({
                    { "id", key.id },
                    { "time", key.time },
                    { "value", key.value },
                    { "inTangent", key.inTangent },
                    { "outTangent", key.outTangent },
                });
            }
            return {
                { "interpolation", ToString(curve.interpolation) },
                { "randomOffset", WriteDistribution(curve.randomOffset) },
                { "keys", std::move(keys) },
            };
        }

        Json WriteVec3Curve(const VfxVec3CurveData& curve) {
            return {
                { "x", WriteCurve(curve.x) },
                { "y", WriteCurve(curve.y) },
                { "z", WriteCurve(curve.z) },
            };
        }

        Json WriteColorCurve(const VfxColorCurveData& curve) {
            return {
                { "r", WriteCurve(curve.r) },
                { "g", WriteCurve(curve.g) },
                { "b", WriteCurve(curve.b) },
                { "a", WriteCurve(curve.a) },
            };
        }

        Json WriteGradient(VfxColorGradientData gradient) {
            std::sort(
                gradient.stops.begin(),
                gradient.stops.end(),
                [](const auto& left, const auto& right) {
                    if (left.time != right.time) return left.time < right.time;
                    return left.id < right.id;
                });
            Json stops = Json::array();
            for (const VfxColorStopData& stop : gradient.stops) {
                stops.push_back({
                    { "id", stop.id },
                    { "time", stop.time },
                    { "color", WriteVec4(stop.color) },
                });
            }
            return { { "stops", std::move(stops) } };
        }

        Json WriteEasing(const VfxEasingData<MATH::Vec3>& value) {
            return {
                { "start", WriteDistribution(value.start) },
                { "middle", WriteDistribution(value.middle) },
                { "end", WriteDistribution(value.end) },
                { "function", ToString(value.function) },
                { "useMiddle", value.useMiddle },
            };
        }

        Json WriteEasing(const VfxEasingData<MATH::Vec4>& value) {
            return {
                { "start", WriteDistribution(value.start) },
                { "middle", WriteDistribution(value.middle) },
                { "end", WriteDistribution(value.end) },
                { "function", ToString(value.function) },
                { "useMiddle", value.useMiddle },
            };
        }

        Json WriteAnimatedVec3(const VfxAnimatedVec3Data& value) {
            return {
                { "mode", ToString(value.mode) },
                { "fixed", WriteDistribution(value.fixed) },
                { "pva", {
                    { "initial", WriteDistribution(value.pva.initial) },
                    { "velocity", WriteDistribution(value.pva.velocity) },
                    { "acceleration", WriteDistribution(
                        value.pva.acceleration) },
                } },
                { "easing", WriteEasing(value.easing) },
                { "curve", WriteVec3Curve(value.curve) },
            };
        }

        Json WriteAnimatedColor(const VfxAnimatedColorData& value) {
            return {
                { "mode", ToString(value.mode) },
                { "fixed", WriteDistribution(value.fixed) },
                { "easing", WriteEasing(value.easing) },
                { "curve", WriteColorCurve(value.curve) },
                { "gradient", WriteGradient(value.gradient) },
            };
        }

        Json WriteTransform(const VfxTransformData& value) {
            return {
                { "position", WriteVec3(value.position) },
                { "rotationEulerDegrees", WriteVec3(
                    value.rotationEulerDegrees) },
                { "scale", WriteVec3(value.scale) },
            };
        }

        Json WriteAttachment(const VfxAttachmentData& value) {
            return {
                { "id", value.id },
                { "mode", ToString(value.mode) },
                { "follow", ToString(value.follow) },
                { "socketName", value.socketName },
                { "offset", WriteTransform(value.offset) },
            };
        }

        Json WriteSpecialOutput(const VfxSpecialOutputData& value) {
            return {
                { "id", value.id },
                { "type", ToString(value.type) },
                { "enabled", value.enabled },
                { "weaponPathDistortion", {
                    { "distortion", value.weaponPathDistortion.distortion },
                    { "blur", value.weaponPathDistortion.blur },
                    { "opacity", value.weaponPathDistortion.opacity },
                    { "emissive", value.weaponPathDistortion.emissive },
                    { "flow", value.weaponPathDistortion.flow },
                    { "width", value.weaponPathDistortion.width },
                    { "lifetimeSeconds",
                        value.weaponPathDistortion.lifetimeSeconds },
                    { "bodyColor", WriteVec4(
                        value.weaponPathDistortion.bodyColor) },
                    { "edgeColor", WriteVec4(
                        value.weaponPathDistortion.edgeColor) },
                } },
                { "screenImpulse", {
                    { "durationSeconds", value.screenImpulse.durationSeconds },
                    { "maxRadius", value.screenImpulse.maxRadius },
                    { "ringThickness", value.screenImpulse.ringThickness },
                    { "distortion", value.screenImpulse.distortion },
                    { "chromaticAberration",
                        value.screenImpulse.chromaticAberration },
                    { "flash", value.screenImpulse.flash },
                    { "intensity", value.screenImpulse.intensity },
                    { "color", WriteVec4(value.screenImpulse.color) },
                    { "waveFrequency", value.screenImpulse.waveFrequency },
                    { "waveAmplitude", value.screenImpulse.waveAmplitude },
                    { "waveDamping", value.screenImpulse.waveDamping },
                    { "waveCount", value.screenImpulse.waveCount },
                } },
                { "skinnedAfterimage", {
                    { "sampleCount", value.skinnedAfterimage.sampleCount },
                    { "lifetimeSeconds",
                        value.skinnedAfterimage.lifetimeSeconds },
                    { "opacity", value.skinnedAfterimage.opacity },
                    { "fresnel", value.skinnedAfterimage.fresnel },
                    { "dissolve", value.skinnedAfterimage.dissolve },
                    { "shellOffset", value.skinnedAfterimage.shellOffset },
                    { "color", WriteVec4(value.skinnedAfterimage.color) },
                    { "intensity", value.skinnedAfterimage.intensity },
                } },
            };
        }

        Json WriteEmitter(const VfxEmitterData& emitter) {
            Json effects = Json::array();
            for (const auto& effect : emitter.effects) {
                effects.push_back({{"id", effect.id}, {"kind", MaterialEffectName(effect.kind)},
                    {"enabled", effect.enabled}, {"parameters", WriteVec4(effect.parameters)},
                    {"colorA", WriteVec4(effect.colorA)}, {"colorB", WriteVec4(effect.colorB)}});
            }
            Json links = Json::array();
            auto sortedLinks = emitter.inputLinks;
            std::sort(
                sortedLinks.begin(),
                sortedLinks.end(),
                [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
            for (const VfxInputLinkData& link : sortedLinks) {
                links.push_back({
                    { "id", link.id },
                    { "inputId", link.inputId },
                    { "target", {
                        { "emitterId", link.target.emitterId },
                        { "moduleId", link.target.moduleId },
                        { "propertyId", link.target.propertyId },
                    } },
                    { "scale", link.scale },
                    { "bias", link.bias },
                });
            }

            auto sortedBursts = emitter.spawn.burstTimeline;
            if (sortedBursts.empty() && emitter.spawn.burstCount > 0u) {
                sortedBursts.push_back({
                    emitter.spawn.id + ".burst.legacy",
                    0.0f,
                    emitter.spawn.burstCount });
            }
            std::sort(
                sortedBursts.begin(),
                sortedBursts.end(),
                [](const auto& left, const auto& right) {
                    if (left.time != right.time) {
                        return left.time < right.time;
                    }
                    return left.id < right.id;
                });
            Json burstTimeline = Json::array();
            uint32_t legacyBurstCount = 0u;
            for (const VfxBurstKeyData& key : sortedBursts) {
                burstTimeline.push_back({
                    { "id", key.id },
                    { "time", key.time },
                    { "count", key.count },
                });
                if (legacyBurstCount == 0u && key.time == 0.0f) {
                    legacyBurstCount = key.count;
                }
            }

            auto sortedOutputs = emitter.specialOutputs;
            std::sort(
                sortedOutputs.begin(),
                sortedOutputs.end(),
                [](const auto& left, const auto& right) {
                    return left.id < right.id;
                });
            Json specialOutputs = Json::array();
            for (const VfxSpecialOutputData& output : sortedOutputs) {
                specialOutputs.push_back(WriteSpecialOutput(output));
            }

            Json parameterTracks = Json::array();
            for (const auto& track : emitter.parameterTracks)
                parameterTracks.push_back({{"id",track.id},{"targetId",track.targetId},
                    {"propertyId",track.propertyId},{"curve",WriteCurve(track.curve)}});
            return {
                { "parameterTracks", std::move(parameterTracks) },
                { "id", emitter.id },
                { "name", emitter.name },
                { "parentEmitterId", emitter.parentEmitterId },
                { "order", emitter.order },
                { "startDelay", emitter.startDelay },
                { "kind", emitter.kind == VfxLayerKind::Shape ? "Shape" :
                    emitter.kind == VfxLayerKind::Group ? "Group" : "Particles" },
                { "activeDuration", emitter.activeDuration },
                {"animationTiming",{{"sourceDuration",emitter.animationTiming.sourceDuration},{"offset",emitter.animationTiming.offset},{"rate",emitter.animationTiming.rate}}},
                { "history", {{"mode", emitter.history.mode == VfxHistoryMode::Afterimage ? "Afterimage" :
                    emitter.history.mode == VfxHistoryMode::Trail ? "Trail" : "None"},
                    {"duration", emitter.history.duration}, {"samples", emitter.history.samples},
                    {"breakDistance", emitter.history.breakDistance}} },
                { "modules", {
                    { "basic", {
                        { "id", emitter.basic.id },
                        { "enabled", emitter.basic.enabled },
                        { "localSimulation", emitter.basic.localSimulation },
                        { "lifetime", WriteDistribution(
                            emitter.basic.lifetime) },
                    } },
                    { "emitterTransform", {
                        { "id", emitter.emitterTransform.id },
                        { "transform", WriteTransform(
                            emitter.emitterTransform.transform) },
                    } },
                    { "attachment", WriteAttachment(emitter.attachment) },
                    { "spawn", {
                        { "id", emitter.spawn.id },
                        { "ratePerSecond", emitter.spawn.ratePerSecond },
                        // Keep the v3 field for old tools; burstTimeline is
                        // authoritative whenever it contains keys.
                        { "burstCount", legacyBurstCount },
                        { "burstTimeline", std::move(burstTimeline) },
                        { "capacityMode", ToString(
                            emitter.spawn.capacityMode) },
                        { "maximumParticles",
                            emitter.spawn.maximumParticles },
                    } },
                    { "shape", {
                        { "id", emitter.shape.id },
                        { "type", ToString(emitter.shape.type) },
                        { "radius", WriteDistribution(
                            emitter.shape.radius) },
                        { "coneAngleDegrees",
                            emitter.shape.coneAngleDegrees },
                        { "boxHalfExtents",
                            WriteVec3(emitter.shape.boxHalfExtents) },
                        { "cylinderHeight",
                            emitter.shape.cylinderHeight },
                        { "circleInnerRadius",
                            emitter.shape.circleInnerRadius },
                        { "circleEmitOnEdge",
                            emitter.shape.circleEmitOnEdge },
                        { "edgeLength",
                            emitter.shape.edgeLength },
                    } },
                    { "position", {
                        { "id", emitter.position.id },
                        { "radialSpeed", WriteDistribution(
                            emitter.position.radialSpeed) },
                        { "value", WriteAnimatedVec3(
                            emitter.position.value) },
                    } },
                    { "rotation", {
                        { "id", emitter.rotation.id },
                        { "eulerDegrees", WriteAnimatedVec3(
                            emitter.rotation.eulerDegrees) },
                    } },
                    { "scale", {
                        { "id", emitter.scale.id },
                        { "value", WriteAnimatedVec3(emitter.scale.value) },
                    } },
                    { "forces", {
                        { "id", emitter.forces.id },
                        { "gravity", WriteVec3(emitter.forces.gravity) },
                        { "drag", emitter.forces.drag },
                        { "collisionPlaneEnabled", emitter.forces.collisionPlaneEnabled },
                        { "collisionPlaneHeight", emitter.forces.collisionPlaneHeight },
                        { "restitution", emitter.forces.restitution },
                        { "friction", emitter.forces.friction },
                    } },
                    { "color", {
                        { "id", emitter.color.id },
                        { "value", WriteAnimatedColor(emitter.color.value) },
                    } },
                    { "renderCommon", {
                        { "id", emitter.renderCommon.id },
                        { "materialMode", ToString(
                            emitter.renderCommon.materialMode) },
                        { "colorTextureAssetGuid",
                            emitter.renderCommon.colorTextureAssetGuid.value },
                        { "materialAssetGuid",
                            emitter.renderCommon.materialAssetGuid.value },
                        { "blendMode", ToString(
                            emitter.renderCommon.blendMode) },
                        { "depthMode", ToString(
                            emitter.renderCommon.depthMode) },
                        { "softIntersectionDistance",
                            emitter.renderCommon.softIntersectionDistance },
                        { "depthBias", emitter.renderCommon.depthBias },
                        { "flipbookColumns",
                            emitter.renderCommon.flipbookColumns },
                        { "flipbookRows",
                            emitter.renderCommon.flipbookRows },
                        { "flipbookFrameCount",
                            emitter.renderCommon.flipbookFrameCount },
                        { "flipbookFramesPerSecond",
                            emitter.renderCommon.flipbookFramesPerSecond },
                        { "flipbookLoop",
                            emitter.renderCommon.flipbookLoop },
                        { "flipbookRandomStart",
                            emitter.renderCommon.flipbookRandomStart },
                    } },
                    { "renderShape", {
                        { "id", emitter.renderShape.id },
                        { "type", ToString(emitter.renderShape.type) },
                        { "billboardMode", ToString(
                            emitter.renderShape.billboardMode) },
                        { "ribbonSegmentCount",
                            emitter.renderShape.ribbonSegmentCount },
                        { "ribbonWidth",
                            emitter.renderShape.ribbonWidth },
                        { "ringOuterRadius",
                            emitter.renderShape.ringOuterRadius },
                        { "ringInnerRadius",
                            emitter.renderShape.ringInnerRadius },
                        { "ringSegmentCount",
                            emitter.renderShape.ringSegmentCount },
                        { "ringStartAngleDegrees",
                            emitter.renderShape.ringStartAngleDegrees },
                        { "ringEndAngleDegrees",
                            emitter.renderShape.ringEndAngleDegrees },
                    } },
                } },
                { "specialOutputs", std::move(specialOutputs) },
                { "effects", std::move(effects) },
                { "inputLinks", std::move(links) },
            };
        }
    }

    Json WriteVfxSourceDocument(const VfxAssetData& asset) {
        Json inputs = Json::array();
        auto sortedInputs = asset.inputs;
        std::sort(
            sortedInputs.begin(),
            sortedInputs.end(),
            [](const auto& left, const auto& right) {
                return left.id < right.id;
            });
        for (const VfxExposedInputData& input : sortedInputs) {
            inputs.push_back({
                { "id", input.id },
                { "name", input.name },
                { "type", ToString(input.type) },
                { "defaultValue", WriteValue(input.defaultValue) },
            });
        }

        Json emitters = Json::array();
        auto sortedEmitters = asset.emitters;
        std::sort(
            sortedEmitters.begin(),
            sortedEmitters.end(),
            [](const auto& left, const auto& right) {
                if (left.order != right.order) return left.order < right.order;
                return left.id < right.id;
            });
        for (const VfxEmitterData& emitter : sortedEmitters) {
            emitters.push_back(WriteEmitter(emitter));
        }
        return {
            { "version", kVfxSourceVersion },
            { "name", asset.name },
            { "durationSeconds", asset.durationSeconds },
            { "loop", asset.loop },
            { "inputs", std::move(inputs) },
            { "emitters", std::move(emitters) },
        };
    }

} // namespace HIKARI::VFX::SERIALIZATION::INTERNAL
