#include "Scene/Vfx/HIKARI_VfxPlaybackState.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackSchedule.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

#include <algorithm>
#include <limits>

// 流体域の提交。域の層の世界変換と、下にある流体源の層（形・注入量・burst・力場・風）を 1 つにまとめる。
// 域自身の forces（力場・風・減衰）は域全体に掛かる源として先頭に入れる。解算は GPU 側が固定子步で行い、
// ここはゲーム時間（時間の倍率込み）とその間に起きた注入だけを渡す。
namespace HIKARI::VFX {
namespace {
namespace FLUID = RENDER3D::FLUID;
using RENDER3D::PARTICLES::ParticleDistributionFloat;
using RENDER3D::PARTICLES::ParticleDistributionMode;
using RENDER3D::PARTICLES::ParticleEmitterSimulationParameters;

float DistributionExtent(const ParticleDistributionFloat& value) noexcept {
    return value.mode == ParticleDistributionMode::RandomRange ? value.maximum : value.minimum;
}

FLUID::FluidSourceSubmission MakeSource(const VfxWorldTransform& world,
                                        const ParticleEmitterSimulationParameters& simulation) {
    FLUID::FluidSourceSubmission source{};
    source.worldPosition = world.position;
    source.worldRotation = world.rotation;
    source.worldScale = world.scale;
    source.shape = static_cast<FLUID::FluidSourceShape>(simulation.shape);
    source.radius = DistributionExtent(simulation.shapeRadius);
    // 球は半径の範囲（最小〜最大、粒子の放出と同じ）が球殻。最小と最大が同じなら中まで詰まった球。円は円環の内径。
    const ParticleDistributionFloat& radius = simulation.shapeRadius;
    source.innerRadius = simulation.shape != RENDER3D::PARTICLES::ParticleShape::Sphere
                             ? simulation.shapeCircleInnerRadius
                         : radius.mode == ParticleDistributionMode::RandomRange && radius.minimum < radius.maximum
                             ? (std::max)(radius.minimum, 0.0f)
                             : 0.0f;
    source.coneAngleRadians = simulation.coneAngleRadians;
    source.boxHalfExtents = simulation.shapeBoxHalfExtents;
    // 線（Edge）の長さも height で渡す（円柱の高さと同じ軸の長さ）。
    source.height = simulation.shape == RENDER3D::PARTICLES::ParticleShape::Edge ? simulation.shapeEdgeLength
                                                                                 : simulation.shapeCylinderHeight;
    source.field = simulation.field;
    source.wind = simulation.gravity;
    source.drag = simulation.drag;
    return source;
}

bool HasDomainForces(const ParticleEmitterSimulationParameters& simulation) noexcept {
    return RENDER3D::PARTICLES::IsParticleForceFieldActive(simulation.field) || simulation.drag > 0.0f ||
           simulation.gravity.x != 0.0f || simulation.gravity.y != 0.0f || simulation.gravity.z != 0.0f;
}
} // namespace

void VfxPlaybackService::AppendFluidDomain(Instance& instance, uint32_t domainIndex, float deltaSeconds,
                                           float spawnDelta, bool drawAfterSimulation, bool wrapLoop) {
    const VfxProgram& program = *instance.program;
    const VfxProgramEmitter& domainLayer = program.emitters[domainIndex];
    EmitterState& domainState = instance.emitterStates[domainIndex];
    // 流体は時間を飛ばせない。拖时间の途中の段は解かず、次に描く段で作り直して預熱からやり直す。
    if (!drawAfterSimulation) {
        domainState.resetSimulation = true;
        return;
    }
    if (renderInput_.fluids.size() >= FLUID::kMaximumFluidDomainSubmissionsPerFrame)
        return;

    FLUID::FluidDomainSubmission domain{};
    domain.key = {playbackNamespace_, instance.handle.slot, instance.handle.generation, domainIndex};
    domain.visible = instance.layerVisible[domainIndex] != 0u;
    // 見えない域は解算側で飛ばされるので、作り直しの要求は見えるようになって最初に渡すまで持ち越す。
    domain.resetSimulation = domainState.resetSimulation;
    if (domain.visible)
        domainState.resetSimulation = false;
    domain.localSpace = domainLayer.simulation.localSimulation != 0u;
    domain.deltaSeconds = deltaSeconds;
    const VfxWorldTransform& world = instance.emitterTransforms[domainIndex];
    domain.worldPosition = world.position;
    domain.worldRotation = world.rotation;
    const auto& tint = instance.layerSamples[domainIndex].color;
    const auto& color = instance.layerOverrides[domainIndex].colorMultiplier;
    domain.colorMultiplier = {tint.x * color.x, tint.y * color.y, tint.z * color.z, tint.w * color.w};
    domain.settings = domainLayer.fluidDomain;

    if (HasDomainForces(domainLayer.simulation)) {
        FLUID::FluidSourceSubmission forces = MakeSource(world, domainLayer.simulation);
        forces.shape = FLUID::FluidSourceShape::Domain;
        forces.wind = forces.wind * instance.layerOverrides[domainIndex].gravityMultiplier;
        domain.sources.push_back(forces);
    }
    for (uint32_t index = 0u; index < program.emitters.size(); ++index) {
        const VfxProgramEmitter& layer = program.emitters[index];
        if (layer.kind != VfxLayerKind::FluidSource || layer.fluidDomainIndex != domainIndex)
            continue;
        EmitterState& state = instance.emitterStates[index];
        const auto& overrides = instance.layerOverrides[index];
        // burst は 1 回で注入する密度（数をそのまま密度として使う）。連続の注入率は GPU が子步ごとに掛ける。
        const uint32_t burst =
            PLAYBACK::ComputeEmitterSpawnCount(state, program, layer, instance.elapsedSeconds, spawnDelta, 0.0f,
                                               instance.loop, wrapLoop, (std::numeric_limits<uint32_t>::max)());
        state.resetSimulation = false;
        if (instance.layerVisible[index] == 0u || domain.sources.size() >= FLUID::kMaximumFluidSourcesPerDomain)
            continue;
        FLUID::FluidSourceSubmission source = MakeSource(instance.emitterTransforms[index], layer.simulation);
        source.ratePerSecond = layer.spawnRatePerSecond * overrides.spawnRateMultiplier;
        source.burstDensity = static_cast<float>(burst);
        source.settings = layer.fluidSource;
        source.wind = source.wind * overrides.gravityMultiplier;
        domain.sources.push_back(source);
    }
    renderInput_.fluids.push_back(std::move(domain));
}

} // namespace HIKARI::VFX
