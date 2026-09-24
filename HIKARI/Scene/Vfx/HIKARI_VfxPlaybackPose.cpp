#include "Scene/Vfx/HIKARI_VfxPlaybackState.h"
#include <algorithm>
#include <cmath>
namespace HIKARI::VFX {
bool VfxPlaybackService::GetLayerPose(VfxPlaybackHandle handle, std::string_view layerId, VfxWorldTransform& world,
                                      VfxWorldTransform& animationFrame) const {
    const auto* instance = Find(handle);
    if (!instance)
        return false;
    for (size_t i = 0; i < instance->program->emitters.size(); ++i) {
        if (instance->program->emitters[i].sourceEmitterId != layerId)
            continue;
        if (i >= instance->animationFrames.size() || !instance->transformResolved[i])
            return false;
        world = instance->emitterTransforms[i];
        animationFrame = instance->animationFrames[i];
        return true;
    }
    return false;
}

bool VfxPlaybackService::GetLayerPoseAtTime(VfxPlaybackHandle handle, std::string_view id, float seconds,
                                            VfxWorldTransform& world, VfxWorldTransform& frame) const {
    const auto* instance = Find(handle);
    if (!instance || !std::isfinite(seconds))
        return false;
    const auto& program = *instance->program;
    auto found = std::find_if(program.emitters.begin(), program.emitters.end(),
                              [&](const auto& e) { return e.sourceEmitterId == id; });
    if (found == program.emitters.end())
        return false;
    std::vector<uint32_t> chain;
    auto index = static_cast<uint32_t>(found - program.emitters.begin());
    while (index != kInvalidVfxProgramIndex && chain.size() < program.emitters.size()) {
        chain.push_back(index);
        index = program.emitters[index].parentEmitterIndex;
    }
    world = instance->transform;
    for (auto i = chain.rbegin(); i != chain.rend(); ++i) {
        const auto& layer = program.emitters[*i];
        auto state = static_cast<const PLAYBACK::EmitterScheduleState&>(instance->emitterStates[*i]);
        frame = PLAYBACK::ResolveEmitterTransform(instance->transform, instance->ownerObject, layer, world, state,
                                                  attachmentResolver_);
        world = frame;
        if (layer.kind != VfxLayerKind::Particles) {
            const float start = PLAYBACK::EmitterStartDelay(program, layer);
            const float span = PLAYBACK::EmitterActiveEnd(program, layer) - start;
            const auto sample =
                EvaluateVfxLayerAnimation(layer, std::clamp(seconds - start, 0.0f, (std::max)(span, 0.0f)), span,
                                          instance->randomSeed + *i * 0x9e3779b9u);
            world = PLAYBACK::ComposeTransforms(
                frame,
                {sample.position,
                 EvaluateVfxLayerRotation(layer, sample, std::clamp(seconds - start, 0.0f, (std::max)(span, 0.0f)),
                                          span, instance->randomSeed + *i * 0x9e3779b9u),
                 sample.scale});
        }
    }
    return true;
}
} // namespace HIKARI::VFX
