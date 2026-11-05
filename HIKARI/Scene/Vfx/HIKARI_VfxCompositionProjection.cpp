#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackState.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackSchedule.h"
#include "Assets/Vfx/HIKARI_VfxLayerAnimation.h"
#include <algorithm>

namespace HIKARI::VFX {
void VfxPlaybackService::ProjectComposition(const Instance& instance, float submittedAge) {
    using namespace RENDER3D::PARTICLES;
    const auto& program = *instance.program;
    if (std::none_of(program.emitters.begin(), program.emitters.end(), [](const auto& e) {
            return e.matteSourceIndex != UINT32_MAX || e.matteOnly ||
                   std::any_of(e.runtimeEffects.begin(), e.runtimeEffects.end(), [](const auto& effect) {
                       return effect.enabled && effect.space == MaterialEffectSpace::LayerUv;
                   });
        }))
        return;
    const ParticleEmitterInstanceKey identity{playbackNamespace_, instance.handle.slot, instance.handle.generation, 0};
    std::erase_if(renderInput_.compositions, [&](const auto& c) { return c.instance == identity; });
    ParticleComposition composition;
    composition.instance = identity;
    for (const auto& emitter : program.emitters) {
        // 流体層は体積の步进で描き、粒子の合成（マット・調整層）には入らない。
        if (emitter.kind == VfxLayerKind::Fluid || emitter.kind == VfxLayerKind::FluidSource)
            continue;
        ParticleCompositionLayer layer;
        layer.emitterIndex = emitter.emitterIndex;
        layer.parentEmitterIndex = emitter.parentEmitterIndex;
        layer.matteSourceIndex = emitter.matteSourceIndex;
        layer.matteLuminance = emitter.matteLuminance;
        layer.matteInverted = emitter.matteInverted;
        layer.matteOnly = emitter.matteOnly;
        layer.enabled = instance.layerVisible[emitter.emitterIndex] != 0;
        if (emitter.kind == VfxLayerKind::Group)
            layer.kind = ParticleCompositionLayerKind::Group;
        if (emitter.kind == VfxLayerKind::Adjustment) {
            layer.kind = ParticleCompositionLayerKind::Adjustment;
            const float start = PLAYBACK::EmitterStartDelay(program, emitter);
            const float duration = PLAYBACK::EmitterActiveEnd(program, emitter) - start;
            layer.effects = EvaluateVfxRuntimeEffects(emitter, submittedAge - start, duration);
            std::erase_if(layer.effects, [](const auto& effect) {
                return effect.space != MaterialEffectSpace::LayerUv || !effect.enabled || effect.mix <= 0;
            });
            if (layer.effects.empty())
                continue;
        }
        composition.layers.push_back(std::move(layer));
    }
    const bool hasWork = std::any_of(composition.layers.begin(), composition.layers.end(), [](const auto& layer) {
        return layer.enabled && (layer.matteSourceIndex != UINT32_MAX || layer.matteOnly || !layer.effects.empty());
    });
    if (hasWork && std::any_of(composition.layers.begin(), composition.layers.end(), [](const auto& layer) {
            return layer.enabled && layer.kind == ParticleCompositionLayerKind::Content;
        }))
        renderInput_.compositions.push_back(std::move(composition));
}
void VfxPlaybackService::ProjectAdjustmentParameters(const Instance& instance, const VfxProgramEmitter& target,
                                                     float submittedAge,
                                                     RENDER3D::PARTICLES::ParticleEmitterRenderParameters& render) {
    const auto& program = *instance.program;
    render.spatialEffects.clear();
    for (const auto ownerIndex : target.spatialOwners) {
        if (!instance.layerVisible[ownerIndex])
            continue;
        const auto& owner = program.emitters[ownerIndex];
        const auto& transform = instance.emitterTransforms[ownerIndex];
        const float start = PLAYBACK::EmitterStartDelay(program, owner);
        const float duration = PLAYBACK::EmitterActiveEnd(program, owner) - start;
        for (const auto& effect : EvaluateVfxRuntimeEffects(owner, submittedAge - start, duration)) {
            // mix=0 でも三次元フレームと細分化を維持し、アニメーションのゼロ境界で平面を跳ばさない。
            if (!effect.enabled || effect.space != MaterialEffectSpace::Group3D)
                continue;
            RENDER3D::PARTICLES::ParticleSpatialEffect spatial;
            spatial.ownerEmitterIndex = ownerIndex;
            spatial.kind = effect.kind;
            spatial.mix = effect.mix;
            spatial.mask = effect.mask;
            spatial.maskInvert = effect.maskInvert;
            spatial.parameters = effect.parameters;
            spatial.parameters2 = effect.parameters2;
            spatial.maskParameters = effect.maskParameters;
            spatial.origin = transform.position;
            spatial.rotation = transform.rotation;
            spatial.scale = transform.scale;
            render.spatialEffects.push_back(spatial);
        }
    }
    for (const auto& curve : target.layerCurves) {
        if (curve.ownerEmitterIndex == UINT32_MAX)
            continue;
        const auto& owner = program.emitters[curve.ownerEmitterIndex];
        const float start = PLAYBACK::EmitterStartDelay(program, owner);
        const float duration = PLAYBACK::EmitterActiveEnd(program, owner) - start;
        const float t = owner.animationTiming.Normalized(submittedAge - start, duration);
        float value =
            curve.channel == VfxLayerChannel::MaterialUvOffset
                ? IntegrateParticleCurve(curve.keys, curve.interpolation, t) * owner.animationTiming.Duration(duration)
                : std::clamp(EvaluateParticleCurve(curve.keys, curve.interpolation, t), curve.minimum, curve.maximum);
        if (curve.channel == VfxLayerChannel::MaterialEffectMix && !instance.layerVisible[curve.ownerEmitterIndex])
            value = 0;
        render.animatedParameters.push_back({curve.materialSlot, value});
    }
}

} // namespace HIKARI::VFX
