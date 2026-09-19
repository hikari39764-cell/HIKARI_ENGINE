#include "Assets/Vfx/HIKARI_VfxLayerAnimation.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/Vfx/HIKARI_VfxProgram.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include <algorithm>

namespace HIKARI::VFX {
using namespace RENDER3D::PARTICLES;
std::vector<VfxAnimatableParameter> GetVfxAnimatableParameters(const VfxEmitterData &layer) {
    std::vector<VfxAnimatableParameter> result;
    const auto add = [&](std::string id, std::string property, std::string label, float value, float lo,
                         float hi, VfxLayerChannel channel = VfxLayerChannel::MaterialParameter) {
        result.push_back({std::move(id), std::move(property), std::move(label), value, lo, hi, channel});
    };
    if (layer.renderShape.type == VfxRenderShapeType::Ring) {
        add(layer.renderShape.id, "ringInnerRadius", "Inner Radius", layer.renderShape.ringInnerRadius, 0,
            100, VfxLayerChannel::RingInnerRadius);
        add(layer.renderShape.id, "ringOuterRadius", "Outer Radius", layer.renderShape.ringOuterRadius,
            0.001f, 100, VfxLayerChannel::RingOuterRadius);
        add(layer.renderShape.id, "ringStartAngleDegrees", "Start Angle",
            layer.renderShape.ringStartAngleDegrees, -360, 360, VfxLayerChannel::RingStartAngle);
        add(layer.renderShape.id, "ringEndAngleDegrees", "End Angle", layer.renderShape.ringEndAngleDegrees,
            -360, 360, VfxLayerChannel::RingEndAngle);
    }
    if (layer.renderShape.type == VfxRenderShapeType::Ribbon)
        add(layer.renderShape.id, "ribbonWidth", "Width", layer.renderShape.ribbonWidth, 0.001f, 100,
            VfxLayerChannel::RibbonWidth);
    for (const auto &effect : layer.effects) {
        const auto addEffect = [&](const char *property, const char *label, float value, float lo, float hi) {
            add(effect.id, property, label, value, lo, hi);
        };
        if (layer.kind == VfxLayerKind::Adjustment) {
            for (uint32_t set = 0; set < 2; ++set) {
                const auto names = CompositeEffectParameterNames(effect.kind, set != 0);
                const auto& values = set ? effect.parameters2 : effect.parameters;
                const float channels[]{values.x, values.y, values.z, values.w};
                for (uint32_t c = 0; c < 4 && names[c]; ++c) {
                    const auto property = std::string(set ? "parameters2." : "parameters.") + "xyzw"[c];
                    float minimum = 0, maximum = 1;
                    if (set) { minimum = -180; maximum = 180; }
                    else switch (effect.kind) {
                    case MaterialEffectKind::Pixelate: if (c == 0) { minimum = 1; maximum = 256; } break;
                    case MaterialEffectKind::Posterize: if (c == 0) { minimum = 2; maximum = 64; } break;
                    case MaterialEffectKind::Blur: maximum = 64; break;
                    case MaterialEffectKind::Glow: maximum = c == 2 ? 64.0f : 16.0f; break;
                    case MaterialEffectKind::ColorCorrect:
                        minimum = c == 3 ? -1.0f : 0.0f;
                        maximum = c == 0 ? 8.0f : c == 3 ? 1.0f : 4.0f; break;
                    case MaterialEffectKind::Ink:
                        minimum = c == 0 ? 0.25f : 0.0f;
                        maximum = c == 0 ? 12.0f : c == 1 ? 4.0f : 1.0f; break;
                    default: break;
                    }
                    addEffect(property.c_str(), names[c], channels[c], minimum, maximum);
                }
            }
            continue;
        }
        switch (effect.kind) {
        case MaterialEffectKind::FlowMap:
            addEffect("parameters.x", "Flow Strength", effect.parameters.x, 0, 2);
            addEffect("parameters.y", "Flow Speed", effect.parameters.y, -8, 8);
            addEffect("parameters.z", "Flow Repeat", effect.parameters.z, 0.001f, 20);
            addEffect("parameters.w", "Flow Mix", effect.parameters.w, 0, 1);
            break;
        case MaterialEffectKind::UvFlow:
            addEffect("parameters.x", "Flow U", effect.parameters.x, -20, 20);
            addEffect("parameters.y", "Flow V", effect.parameters.y, -20, 20);
            // タイルは乗数として評価する。積分経路を使うのは x/y のフローだけ。
            addEffect("parameters.z", "U Repeat", effect.parameters.z, 0.001f, 20);
            addEffect("parameters.w", "V Repeat", effect.parameters.w, 0.001f, 20);
            addEffect("parameters2.x", "UV Rotation", effect.parameters2.x, -360, 360);
            break;
        case MaterialEffectKind::UvDistortion:
            addEffect("parameters.x", "Distortion", effect.parameters.x, 0, 1);
            break;
        case MaterialEffectKind::Dissolve:
        case MaterialEffectKind::Wipe:
            addEffect("progress", "Progress", effect.parameters.x, 0, 1);
            break;
        case MaterialEffectKind::Emission:
            addEffect("parameters.x", "Emission", effect.parameters.x, 0, 32);
            break;
        case MaterialEffectKind::Fresnel:
            addEffect("parameters.y", "Rim Intensity", effect.parameters.y, 0, 32);
            break;
        case MaterialEffectKind::Opacity:
            addEffect("parameters.x", "Amount", effect.parameters.x, 0, 1);
            break;
        case MaterialEffectKind::Glow:
            addEffect("parameters.x", "Threshold", effect.parameters.x, 0, 4);
            addEffect("parameters.y", "Intensity", effect.parameters.y, 0, 32);
            break;
        case MaterialEffectKind::Blur:
            addEffect("parameters.x", "Radius", effect.parameters.x, 0, 0.5f);
            break;
        case MaterialEffectKind::LightSweep:
            addEffect("parameters.x", "Center", effect.parameters.x, -1, 2);
            addEffect("parameters.y", "Width", effect.parameters.y, 0, 2);
            addEffect("parameters.w", "Intensity", effect.parameters.w, 0, 32);
            break;
        case MaterialEffectKind::LuminanceMask:
            addEffect("parameters.x", "Threshold", effect.parameters.x, 0, 1);
            addEffect("parameters.y", "Softness", effect.parameters.y, 0.001f, 1);
            break;
        case MaterialEffectKind::RadialBlur:
            addEffect("parameters.x", "Radius", effect.parameters.x, 0, 0.5f);
            addEffect("parameters.y", "Center U", effect.parameters.y, -1, 2);
            addEffect("parameters.z", "Center V", effect.parameters.z, -1, 2);
            break;
        case MaterialEffectKind::TurbulentDisplace:
            addEffect("parameters.x", "Amount", effect.parameters.x, 0, 1);
            addEffect("parameters.y", "Frequency", effect.parameters.y, 0.01f, 32);
            addEffect("parameters.z", "Speed", effect.parameters.z, -8, 8);
            break;
        case MaterialEffectKind::ColorCorrect:
            addEffect("parameters.x", "Exposure", effect.parameters.x, 0, 8);
            addEffect("parameters.y", "Contrast", effect.parameters.y, 0, 4);
            addEffect("parameters.z", "Saturation", effect.parameters.z, 0, 4);
            addEffect("parameters.w", "Black Level", effect.parameters.w, -1, 1);
            addEffect("parameters2.x", "Hue", effect.parameters2.x, -180, 180);
            break;
        case MaterialEffectKind::Streak:
            addEffect("parameters.x", "Length", effect.parameters.x, 0, 0.5f);
            addEffect("parameters.y", "Angle", effect.parameters.y, -180, 180);
            addEffect("parameters.w", "Decay", effect.parameters.w, 0, 1);
            break;
        default:
            break;
        }
    }
    return result;
}
std::vector<std::string> VfxAnimatedMaterialParameters(const VfxEmitterData &layer) {
    std::vector<std::string> result;
    const auto parameters = GetVfxAnimatableParameters(layer);
    for (const auto &track : layer.parameterTracks)
        for (const auto &parameter : parameters)
            if (parameter.channel == VfxLayerChannel::MaterialParameter &&
                parameter.targetId == track.targetId && parameter.propertyId == track.propertyId)
                result.push_back(track.targetId + "." + track.propertyId);
    return result;
}
bool CompileVfxLayerCurves(const VfxEmitterData &source, VfxProgramEmitter &target, std::string &error) {
    const auto append = [&](const VfxCurveData &sourceCurve, VfxLayerChannel channel, float scale,
                            uint32_t slot = 0) {
        VfxProgramLayerCurve curve;
        curve.channel = channel;
        curve.materialSlot = slot;
        curve.interpolation = static_cast<ParticleCurveInterpolation>(sourceCurve.interpolation);
        for (const auto &key : sourceCurve.keys)
            curve.keys.push_back(
                {key.time, key.value * scale, key.inTangent * scale, key.outTangent * scale});
        std::sort(curve.keys.begin(), curve.keys.end(),
                  [](const auto &a, const auto &b) { return a.time < b.time; });
        target.layerCurves.push_back(std::move(curve));
    };
    if (source.kind != VfxLayerKind::Particles) {
        const VfxAnimatedVec3Data *values[]{&source.position.value, &source.rotation.eulerDegrees,
                                            &source.scale.value};
        for (uint32_t i = 0; i < 3; ++i)
            if (values[i]->mode == VfxAnimatedValueMode::Curve) {
                const VfxCurveData *curves[]{&values[i]->curve.x, &values[i]->curve.y, &values[i]->curve.z};
                for (uint32_t c = 0; c < 3; ++c)
                    append(*curves[c], static_cast<VfxLayerChannel>(i * 3 + c), i == 1 ? 0.01745329252f : 1);
            }
        if (source.color.value.mode == VfxAnimatedColorMode::Curve) {
            const auto &color = source.color.value.curve;
            const VfxCurveData *curves[]{&color.r, &color.g, &color.b, &color.a};
            for (uint32_t c = 0; c < 4; ++c)
                append(*curves[c], static_cast<VfxLayerChannel>(9 + c), 1);
        }
    }
    RENDER3D::MATERIAL::MaterialExpressionRuntimeProgram expression;
    if (!source.parameterTracks.empty() && target.render.effectMaterial &&
        !RENDER3D::MATERIAL::BuildMaterialExpressionRuntimeProgram(
            target.render.effectMaterial->compileResult.program, expression, error))
        return false;
    const auto parameters = GetVfxAnimatableParameters(source);
    for (const auto &track : source.parameterTracks) {
        const auto parameter = std::find_if(parameters.begin(), parameters.end(), [&](const auto &p) {
            return p.targetId == track.targetId && p.propertyId == track.propertyId;
        });
        if (parameter == parameters.end()) {
            error = "Unknown animated parameter";
            return false;
        }
        if (parameter->channel == VfxLayerChannel::MaterialParameter) {
            const auto effect = std::find_if(source.effects.begin(), source.effects.end(),
                                             [&](const auto &e) { return e.id == track.targetId; });
            if (effect != source.effects.end() && !effect->enabled)
                continue;
        }
        uint32_t slot = 0;
        if (parameter->channel == VfxLayerChannel::MaterialParameter && source.kind == VfxLayerKind::Adjustment) {
            const auto effect = std::find_if(source.effects.begin(), source.effects.end(),
                [&](const auto& e) { return e.id == track.targetId; });
            const size_t component = std::string_view("xyzw").find(track.propertyId.back());
            if (effect == source.effects.end() || component == std::string_view::npos) {
                error = "Invalid adjustment parameter"; return false;
            }
            slot = static_cast<uint32_t>(effect - source.effects.begin()) * 8u +
                (track.propertyId.starts_with("parameters2.") ? 4u : 0u) + static_cast<uint32_t>(component);
        } else if (parameter->channel == VfxLayerChannel::MaterialParameter) {
            if (!target.render.effectMaterial) {
                error = "Animated material is missing";
                return false;
            }
            const auto &bindings = target.render.effectMaterial->compileResult.program.parameters;
            const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const auto &b) {
                return b.parameterId == track.targetId + "." + track.propertyId;
            });
            if (binding == bindings.end()) {
                error = "Animated material binding was not compiled";
                return false;
            }
            const auto index = static_cast<uint32_t>(binding - bindings.begin());
            const auto resolved =
                std::find_if(expression.parameterValueSlots.begin(), expression.parameterValueSlots.end(),
                             [&](const auto &p) { return p.first == index; });
            if (resolved == expression.parameterValueSlots.end()) {
                error = "Animated material has no value slot";
                return false;
            }
            slot = resolved->second;
        }
        const bool angle = parameter->channel == VfxLayerChannel::RingStartAngle ||
                           parameter->channel == VfxLayerChannel::RingEndAngle;
        const auto effect = std::find_if(source.effects.begin(), source.effects.end(),
                                         [&](const auto &e) { return e.id == track.targetId; });
        // フロー偏移だけが積分済みオフセット経路を使う。タイルなどの乗数は生の値。
        const bool flow = effect != source.effects.end() &&
                          ((effect->kind == MaterialEffectKind::UvFlow &&
                            (track.propertyId == "parameters.x" || track.propertyId == "parameters.y")) ||
                           (effect->kind == MaterialEffectKind::TurbulentDisplace && track.propertyId == "parameters.z") ||
                           (effect->kind == MaterialEffectKind::FlowMap && track.propertyId == "parameters.y"));
        const float unit = angle ? 0.01745329252f : 1;
        append(track.curve, flow ? VfxLayerChannel::MaterialUvOffset : parameter->channel, unit, slot);
        target.layerCurves.back().minimum = parameter->minimum * unit;
        target.layerCurves.back().maximum = parameter->maximum * unit;
    }
    return true;
}
LayerAnimationSample EvaluateVfxLayerAnimation(const VfxProgramEmitter &layer, float seconds, float duration,
                                               uint32_t seed) noexcept {
    seconds = layer.animationTiming.Seconds(seconds, duration);
    duration = layer.animationTiming.Duration(duration);
    auto sample = EvaluateLayerAnimation(layer.simulation, seconds, duration, seed);
    const float t = duration > 0 ? std::clamp(seconds / duration, 0.0f, 1.0f) : 0;
    float *values[]{&sample.position.x,
                    &sample.position.y,
                    &sample.position.z,
                    &sample.rotationRadians.x,
                    &sample.rotationRadians.y,
                    &sample.rotationRadians.z,
                    &sample.scale.x,
                    &sample.scale.y,
                    &sample.scale.z,
                    &sample.color.x,
                    &sample.color.y,
                    &sample.color.z,
                    &sample.color.w};
    for (const auto &curve : layer.layerCurves) {
        const auto index = static_cast<size_t>(curve.channel);
        if (index >= 13)
            continue;
        const auto &gpu = index < 3   ? layer.simulation.position.curves[index]
                          : index < 6 ? layer.simulation.rotationRadians.curves[index - 3]
                          : index < 9 ? layer.simulation.scale.curves[index - 6]
                                      : layer.simulation.color.curves[index - 9];
        // 既存の確定的random offsetを保持し、固定長GPU曲線の値だけ差し替える。
        *values[index] +=
            EvaluateParticleCurve(curve.keys, curve.interpolation, t) -
            EvaluateParticleCurve(std::span(gpu.keys.data(), gpu.keyCount), gpu.interpolation, t);
    }
    sample.color.w = std::clamp(sample.color.w, 0.0f, 1.0f);
    return sample;
}
void EvaluateVfxLayerRenderParameters(const VfxProgramEmitter &layer, float t,
                                      ParticleEmitterRenderParameters &render, float activeDuration) {
    render.animatedParameters.clear();
    t = layer.animationTiming.Normalized(t * activeDuration, activeDuration);
    for (const auto &curve : layer.layerCurves) {
        const float value = std::clamp(EvaluateParticleCurve(curve.keys, curve.interpolation, t),
                                       curve.minimum, curve.maximum);
        switch (curve.channel) {
        case VfxLayerChannel::RingInnerRadius:
            render.ringInnerRadius = (std::max)(value, 0.0f);
            break;
        case VfxLayerChannel::RingOuterRadius:
            render.ringOuterRadius = (std::max)(value, 0.001f);
            break;
        case VfxLayerChannel::RingStartAngle:
            render.ringStartAngleRadians = value;
            break;
        case VfxLayerChannel::RingEndAngle:
            render.ringEndAngleRadians = value;
            break;
        case VfxLayerChannel::RibbonWidth:
            render.ribbonWidth = (std::max)(value, 0.001f);
            break;
        case VfxLayerChannel::MaterialParameter:
            render.animatedParameters.push_back({curve.materialSlot, value});
            break;
        case VfxLayerChannel::MaterialUvOffset:
            render.animatedParameters.push_back(
                {curve.materialSlot, IntegrateParticleCurve(curve.keys, curve.interpolation, t) *
                                         layer.animationTiming.Duration(activeDuration)});
            break;
        default:
            break;
        }
    }
    render.ringInnerRadius = (std::min)(render.ringInnerRadius, render.ringOuterRadius);
}
} // namespace HIKARI::VFX
