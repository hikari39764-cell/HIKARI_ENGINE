#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackState.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackSchedule.h"
#include "Assets/Vfx/HIKARI_VfxLayerAnimation.h"
#include <algorithm>

namespace HIKARI::VFX {
    void VfxPlaybackService::ProjectComposition(const Instance& instance, float submittedAge) {
        using namespace RENDER3D::PARTICLES;
        const auto& program = *instance.program;
        if (std::none_of(program.emitters.begin(), program.emitters.end(),
            [](const auto& e) { return e.kind == VfxLayerKind::Adjustment; })) return;
        const ParticleEmitterInstanceKey identity{playbackNamespace_, instance.handle.slot, instance.handle.generation, 0};
        std::erase_if(renderInput_.compositions, [&](const auto& c) { return c.instance == identity; });
        ParticleComposition composition;
        composition.instance = identity;
        for (const auto& emitter : program.emitters) {
            ParticleCompositionLayer layer;
            layer.emitterIndex = emitter.emitterIndex;
            layer.parentEmitterIndex = emitter.parentEmitterIndex;
            layer.enabled = instance.layerVisible[emitter.emitterIndex] != 0;
            if (emitter.kind == VfxLayerKind::Group) layer.kind = ParticleCompositionLayerKind::Group;
            if (emitter.kind == VfxLayerKind::Adjustment) {
                layer.kind = ParticleCompositionLayerKind::Adjustment;
                layer.effects = emitter.compositeEffects;
                const float start = PLAYBACK::EmitterStartDelay(program, emitter);
                const float duration = PLAYBACK::EmitterActiveEnd(program, emitter) - start;
                const float t = emitter.animationTiming.Normalized(submittedAge - start, duration);
                for (const auto& curve : emitter.layerCurves) {
                    if (curve.channel != VfxLayerChannel::MaterialParameter || curve.materialSlot / 8u >= layer.effects.size()) continue;
                    auto& effect = layer.effects[curve.materialSlot / 8u];
                    auto& values = curve.materialSlot % 8u >= 4u ? effect.parameters2 : effect.parameters;
                    float* channels[]{&values.x, &values.y, &values.z, &values.w};
                    *channels[curve.materialSlot % 4u] = std::clamp(
                        EvaluateParticleCurve(curve.keys, curve.interpolation, t), curve.minimum, curve.maximum);
                }
            }
            composition.layers.push_back(std::move(layer));
        }
        const bool hasWork = std::any_of(composition.layers.begin(), composition.layers.end(), [](const auto& layer) {
            return layer.enabled && layer.kind == ParticleCompositionLayerKind::Adjustment &&
                std::any_of(layer.effects.begin(), layer.effects.end(), [](const auto& effect) { return effect.enabled; });
        });
        if (hasWork) renderInput_.compositions.push_back(std::move(composition));
    }
}
