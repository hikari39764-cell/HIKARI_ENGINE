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
        const bool composite = layer.kind == VfxLayerKind::Adjustment;
        addEffect("mix", "mix", effect.mix, 0, 1);
        for (uint32_t set = 0; set < 2; ++set) {
            const auto parameters = GetMaterialEffectParameters(effect.kind, set != 0);
            const auto& value = set ? effect.parameters2 : effect.parameters;
            const float values[]{value.x, value.y, value.z, value.w};
            for (uint32_t c = 0; c < 4; ++c) {
                const auto& info = parameters[c];
                if (!info.name || !info.animatable) continue;
                if (set == 1 &&
                    ((effect.kind == MaterialEffectKind::Wave && c == 3 && effect.parameters2.x > 0.5f) ||
                     (effect.kind == MaterialEffectKind::FlowMap && c == 1 && effect.parameters2.z <= 0.5f))) continue;
                const auto property = std::string(set ? "parameters2." : "parameters.") + "xyzw"[c];
                addEffect(property.c_str(), info.name, values[c], info.minimum, info.maximum);
            }
        }
        if (!composite && (effect.kind == MaterialEffectKind::Dissolve || effect.kind == MaterialEffectKind::Wipe))
            addEffect("progress", "Progress", effect.parameters.x, 0, 1);
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
VfxEmitterData PrepareVfxAdjustment(const VfxEmitterData& source) {
    auto result = source;
    // mix は常時バインドし、停止・非表示も同じ材質を再利用する。
    for (const auto& effect : source.effects) {
        if (!effect.enabled) continue;
        const auto ensure = [&](std::string property, float value) {
            if (std::any_of(result.parameterTracks.begin(), result.parameterTracks.end(), [&](const auto& track) {
                return track.targetId == effect.id && track.propertyId == property;
            })) return;
            VfxParameterTrackData track;
            track.targetId = effect.id; track.propertyId = property;
            track.curve.keys.push_back({effect.id + "." + property + ".constant", 0, value, 0, 0});
            result.parameterTracks.push_back(std::move(track));
        };
        ensure("mix", effect.mix);
        if (effect.kind == MaterialEffectKind::TurbulentDisplace) ensure("parameters.z", effect.parameters.z);
    }
    return result;
}
bool CompileVfxLayerCurves(const VfxEmitterData &source, VfxProgramEmitter &target, std::string &error,
                           uint32_t ownerEmitterIndex) {
    const auto append = [&](const VfxCurveData &sourceCurve, VfxLayerChannel channel, float scale,
                            uint32_t slot = 0) {
        VfxProgramLayerCurve curve;
        curve.channel = channel;
        curve.materialSlot = slot;
        curve.ownerEmitterIndex = ownerEmitterIndex;
        curve.interpolation = static_cast<ParticleCurveInterpolation>(sourceCurve.interpolation);
        for (const auto &key : sourceCurve.keys)
            curve.keys.push_back(
                {key.time, key.value * scale, key.inTangent * scale, key.outTangent * scale});
        std::sort(curve.keys.begin(), curve.keys.end(),
                  [](const auto &a, const auto &b) { return a.time < b.time; });
        target.layerCurves.push_back(std::move(curve));
    };
    if (source.kind != VfxLayerKind::Particles && ownerEmitterIndex == UINT32_MAX) {
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
        const auto runtime = std::find_if(target.runtimeEffects.begin(),target.runtimeEffects.end(),
            [&](const auto& effect) { return effect.id == track.targetId; });
        if (ownerEmitterIndex == UINT32_MAX && runtime != target.runtimeEffects.end()) {
            const bool phase = runtime->kind == MaterialEffectKind::TurbulentDisplace && track.propertyId == "parameters.z";
            uint32_t component = 0;
            if (track.propertyId != "mix") {
                const auto axis = std::string_view("xyzw").find(track.propertyId.back());
                if (axis == std::string_view::npos) { error = "Invalid runtime effect parameter"; return false; }
                component = (track.propertyId.starts_with("parameters2.") ? 5u : 1u) + uint32_t(axis);
            }
            append(track.curve,phase ? VfxLayerChannel::EffectPhase : VfxLayerChannel::EffectParameter,1,
                uint32_t(runtime-target.runtimeEffects.begin())*9u+component);
            target.layerCurves.back().minimum=parameter->minimum;
            target.layerCurves.back().maximum=parameter->maximum;
            continue;
        }
        // 調整の UV 曲線は受け手の材質へバインドする。
        if (target.kind == VfxLayerKind::Adjustment && parameter->channel == VfxLayerChannel::MaterialParameter) continue;
        if (parameter->channel == VfxLayerChannel::MaterialParameter) {
            const auto effect = std::find_if(source.effects.begin(), source.effects.end(),
                                             [&](const auto &e) { return e.id == track.targetId; });
            if (effect != source.effects.end() && !effect->enabled)
                continue;
        }
        uint32_t slot = 0;
        if (parameter->channel == VfxLayerChannel::MaterialParameter) {
            if (!target.render.effectMaterial) {
                error = "Animated material is missing";
                return false;
            }
            const auto &bindings = target.render.effectMaterial->compileResult.program.parameters;
            const auto binding = std::find_if(bindings.begin(), bindings.end(), [&](const auto &b) {
                return b.parameterId == track.targetId + "." + track.propertyId;
            });
            if (binding == bindings.end()) {
                // 貼図なしの UV 操作など、実出力へ到達しない調整のバインドは不要。
                if (ownerEmitterIndex != UINT32_MAX) continue;
                error = "Animated material binding was not compiled";
                return false;
            }
            const auto index = static_cast<uint32_t>(binding - bindings.begin());
            const auto resolved =
                std::find_if(expression.parameterValueSlots.begin(), expression.parameterValueSlots.end(),
                             [&](const auto &p) { return p.first == index; });
            if (resolved == expression.parameterValueSlots.end()) {
                if (ownerEmitterIndex != UINT32_MAX) continue;
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
        const auto channel = ownerEmitterIndex != UINT32_MAX && track.propertyId == "mix"
            ? VfxLayerChannel::MaterialEffectMix : flow ? VfxLayerChannel::MaterialUvOffset : parameter->channel;
        append(track.curve, channel, unit, slot);
        target.layerCurves.back().minimum = parameter->minimum * unit;
        target.layerCurves.back().maximum = parameter->maximum * unit;
    }
    return true;
}
std::vector<MaterialEffectStep> EvaluateVfxRuntimeEffects(const VfxProgramEmitter& layer, float seconds, float duration) {
    auto effects=layer.runtimeEffects;
    const float t=layer.animationTiming.Normalized(seconds,duration);
    for (auto& effect:effects)
        if (effect.kind==MaterialEffectKind::TurbulentDisplace)
            effect.parameters.z *= layer.animationTiming.Seconds(seconds,duration);
    for (const auto& curve:layer.layerCurves) {
        if (curve.channel!=VfxLayerChannel::EffectParameter && curve.channel!=VfxLayerChannel::EffectPhase) continue;
        const auto index=curve.materialSlot/9u, component=curve.materialSlot%9u;
        if (index>=effects.size()) continue;
        auto& effect=effects[index];
        float* values[]{&effect.mix,&effect.parameters.x,&effect.parameters.y,&effect.parameters.z,&effect.parameters.w,
            &effect.parameters2.x,&effect.parameters2.y,&effect.parameters2.z,&effect.parameters2.w};
        *values[component]=curve.channel==VfxLayerChannel::EffectPhase
            ? IntegrateParticleCurve(curve.keys,curve.interpolation,t)*layer.animationTiming.Duration(duration)
            : std::clamp(EvaluateParticleCurve(curve.keys,curve.interpolation,t),curve.minimum,curve.maximum);
    }
    return effects;
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
MATH::Quat EvaluateVfxLayerRotation(const VfxProgramEmitter& layer, const LayerAnimationSample& sample,
                                    float seconds, float duration, uint32_t seed) noexcept {
    const auto authored=MATH::Quat::FromEulerXYZ(sample.rotationRadians.x,sample.rotationRadians.y,sample.rotationRadians.z);
    if (!layer.orientAlongPath || duration <= 0) return authored;
    // 保存した XYZ 曲線を直接評価し、編集ビューと通常再生で同じ接線を使う。
    const float step=(std::max)(duration*0.0001f,0.00001f);
    const auto before=EvaluateVfxLayerAnimation(layer,std::clamp(seconds-step,0.0f,duration),duration,seed).position;
    const auto after=EvaluateVfxLayerAnimation(layer,std::clamp(seconds+step,0.0f,duration),duration,seed).position;
    const auto delta=after-before;
    if (MATH::Dot(delta,delta)<1.0e-12f) return authored;
    const auto direction=MATH::Normalize(delta);
    const auto path=direction.y < -0.99999f ? MATH::Quat{0,0,1,0} : MATH::NormalizeQ({direction.z,0,-direction.x,1+direction.y});
    return MATH::NormalizeQ({
        path.w*authored.x+path.x*authored.w+path.y*authored.z-path.z*authored.y,
        path.w*authored.y-path.x*authored.z+path.y*authored.w+path.z*authored.x,
        path.w*authored.z+path.x*authored.y-path.y*authored.x+path.z*authored.w,
        path.w*authored.w-path.x*authored.x-path.y*authored.y-path.z*authored.z});
}
void EvaluateVfxLayerRenderParameters(const VfxProgramEmitter &layer, float t,
                                      ParticleEmitterRenderParameters &render, float activeDuration) {
    render.animatedParameters.clear();
    t = layer.animationTiming.Normalized(t * activeDuration, activeDuration);
    for (const auto &curve : layer.layerCurves) {
        if (curve.ownerEmitterIndex != UINT32_MAX) continue;
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
