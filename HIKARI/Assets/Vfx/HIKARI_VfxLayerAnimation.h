#pragma once
#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Render3D/Vfx/HIKARI_ParticleCpuSimulation.h"

namespace HIKARI::VFX {
struct VfxProgramEmitter;
// 0..12 は既存のTRS/RGBA。GPU粒子の寿命カーブとは独立したCPUレイヤー曲線。
enum class VfxLayerChannel : uint8_t {
    PositionX,
    PositionY,
    PositionZ,
    RotationX,
    RotationY,
    RotationZ,
    ScaleX,
    ScaleY,
    ScaleZ,
    ColorR,
    ColorG,
    ColorB,
    ColorA,
    RingInnerRadius,
    RingOuterRadius,
    RingStartAngle,
    RingEndAngle,
    RibbonWidth,
    MaterialParameter,
    MaterialUvOffset
};
struct VfxProgramLayerCurve {
    VfxLayerChannel channel{};
    uint32_t materialSlot = 0;
    float minimum = -1.0e30f, maximum = 1.0e30f;
    RENDER3D::PARTICLES::ParticleCurveInterpolation interpolation{};
    std::vector<RENDER3D::PARTICLES::ParticleCurveKey> keys;
};
struct VfxAnimatableParameter {
    std::string targetId, propertyId, label;
    float value = 0, minimum = 0, maximum = 1;
    VfxLayerChannel channel = VfxLayerChannel::MaterialParameter;
};
std::vector<VfxAnimatableParameter> GetVfxAnimatableParameters(const VfxEmitterData &layer);
std::vector<std::string> VfxAnimatedMaterialParameters(const VfxEmitterData &layer);
bool CompileVfxLayerCurves(const VfxEmitterData &source, VfxProgramEmitter &target, std::string &error);
RENDER3D::PARTICLES::LayerAnimationSample EvaluateVfxLayerAnimation(const VfxProgramEmitter &layer,
                                                                    float seconds, float duration,
                                                                    uint32_t seed) noexcept;
void EvaluateVfxLayerRenderParameters(const VfxProgramEmitter &layer, float normalizedTime,
                                      RENDER3D::PARTICLES::ParticleEmitterRenderParameters &render,
                                      float activeDuration = 1);
} // namespace HIKARI::VFX
