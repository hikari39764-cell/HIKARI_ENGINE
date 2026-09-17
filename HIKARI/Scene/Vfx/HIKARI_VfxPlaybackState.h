#pragma once
// Playback service の内部状態。公開APIは不透明ハンドルのみ。
#include "Render3D/Vfx/HIKARI_ParticleCpuSimulation.h"
#include "Scene/Vfx/HIKARI_VfxLayerHistory.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackSchedule.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include <limits>
namespace HIKARI::VFX {
struct VfxPlaybackService::HandleSlot {
    size_t instanceIndex = (std::numeric_limits<size_t>::max)();
    uint32_t generation = 1u;
};

struct VfxPlaybackService::EmitterState : PLAYBACK::EmitterScheduleState {
    PLAYBACK::LayerHistory history{};
    EmitterState() = default;
};

struct VfxPlaybackService::Instance {
    VfxPlaybackHandle handle{};
    AssetGuid assetGuid{};
    std::shared_ptr<const VfxProgram> program{};
    RuntimeObjectHandle ownerObject{};
    VfxWorldTransform transform{};
    bool loop = false;
    uint32_t randomSeed = 0u;
    float elapsedSeconds = 0.0f;
    float timeScale = 1.0f;
    std::optional<float> queuedSeek;
    std::vector<VfxValue> inputValues{};
    std::vector<EmitterState> emitterStates{};
    std::vector<VfxWorldTransform> emitterTransforms{};
    std::vector<VfxWorldTransform> animationFrames{};
    std::vector<RENDER3D::PARTICLES::LayerAnimationSample> layerSamples{};
    std::vector<MATH::Vec4> groupTints{};
    std::vector<RENDER3D::PARTICLES::ParticleEmitterRuntimeOverrides> layerOverrides{};
    std::vector<uint8_t> layerVisible{};
    std::vector<uint8_t> transformResolved{};
    std::vector<uint32_t> transformStack{};
};

} // namespace HIKARI::VFX
