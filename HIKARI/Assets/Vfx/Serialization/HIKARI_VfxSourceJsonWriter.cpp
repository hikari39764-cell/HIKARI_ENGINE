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

        Json WriteEmitter(const VfxEmitterData& emitter) {
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

            return {
                { "id", emitter.id },
                { "name", emitter.name },
                { "parentEmitterId", emitter.parentEmitterId },
                { "order", emitter.order },
                { "modules", {
                    { "basic", {
                        { "id", emitter.basic.id },
                        { "enabled", emitter.basic.enabled },
                        { "lifetime", WriteDistribution(
                            emitter.basic.lifetime) },
                    } },
                    { "emitterTransform", {
                        { "id", emitter.emitterTransform.id },
                        { "transform", WriteTransform(
                            emitter.emitterTransform.transform) },
                    } },
                    { "spawn", {
                        { "id", emitter.spawn.id },
                        { "ratePerSecond", emitter.spawn.ratePerSecond },
                        { "burstCount", emitter.spawn.burstCount },
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
                    } },
                    { "color", {
                        { "id", emitter.color.id },
                        { "value", WriteAnimatedColor(emitter.color.value) },
                    } },
                    { "renderCommon", {
                        { "id", emitter.renderCommon.id },
                        { "materialAssetGuid",
                            emitter.renderCommon.materialAssetGuid.value },
                        { "blendMode", ToString(
                            emitter.renderCommon.blendMode) },
                        { "depthTest", emitter.renderCommon.depthTest },
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
            { "version", asset.version },
            { "name", asset.name },
            { "durationSeconds", asset.durationSeconds },
            { "loop", asset.loop },
            { "inputs", std::move(inputs) },
            { "emitters", std::move(emitters) },
        };
    }

} // namespace HIKARI::VFX::SERIALIZATION::INTERNAL
