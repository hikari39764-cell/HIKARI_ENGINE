#include "Scene/Vfx/HIKARI_VfxPlaybackState.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackSchedule.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

#include <algorithm>
#include <limits>

// 流体域の提交。域の層の世界変換と、下にある流体源の層（形・注入量・burst・力場・風）を 1 つにまとめる。
// 域自身の forces（力場・風・減衰）は域全体に掛かる源として先頭に入れる。解算は GPU 側が固定子步で行い、
// ここはゲーム時間（時間の倍率込み）とその間に起きた注入だけを渡す。
// 域の下に無い流体源の層（流体注入层）は場面の源として渡し、描画側が届く域すべてへ入れる（剑气の尾迹・烟弾が場地の域へ
// 注入する）。源は層ごとの鍵で見分け、描画側が子步の間の動きを掃いて注入する。
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

bool VfxPlaybackService::MakeFluidSource(Instance& instance, uint32_t layerIndex, float spawnDelta, bool wrapLoop,
                                         FLUID::FluidSourceSubmission& out) {
    const VfxProgram& program = *instance.program;
    const VfxProgramEmitter& layer = program.emitters[layerIndex];
    EmitterState& state = instance.emitterStates[layerIndex];
    const auto& overrides = instance.layerOverrides[layerIndex];
    // burst は 1 回で注入する密度（数をそのまま密度として使う）。連続の注入率は GPU が子步ごとに掛ける。
    const uint32_t burst =
        PLAYBACK::ComputeEmitterSpawnCount(state, program, layer, instance.elapsedSeconds, spawnDelta, 0.0f,
                                           instance.loop, wrapLoop, (std::numeric_limits<uint32_t>::max)());
    state.resetSimulation = false;
    if (instance.layerVisible[layerIndex] == 0u)
        return false;
    out = MakeSource(instance.emitterTransforms[layerIndex], layer.simulation);
    out.id = FLUID::FluidSourceId({playbackNamespace_, instance.handle.slot, instance.handle.generation, layerIndex});
    out.ratePerSecond = layer.spawnRatePerSecond * overrides.spawnRateMultiplier;
    out.burstDensity = static_cast<float>(burst);
    // 注入する烟の色は層の色（色の模块を層の時間で読んだ値 × 再生の色の倍率）。格ごとの色の域だけが使う。
    const auto& tint = instance.layerSamples[layerIndex].color;
    const auto& multiplier = overrides.colorMultiplier;
    out.color = {tint.x * multiplier.x, tint.y * multiplier.y, tint.z * multiplier.z};
    out.settings = layer.fluidSource;
    out.wind = out.wind * overrides.gravityMultiplier;
    return true;
}

void VfxPlaybackService::AppendFluidSceneSource(Instance& instance, uint32_t layerIndex, float spawnDelta,
                                                bool drawAfterSimulation, bool wrapLoop) {
    // 拖时间の途中の段は注入しない（場面の域は時間を飛ばさない）。
    if (!drawAfterSimulation)
        return;
    FLUID::FluidSourceSubmission source{};
    if (MakeFluidSource(instance, layerIndex, spawnDelta, wrapLoop, source)) {
        source.sceneInjection = true;
        renderInput_.fluidSources.push_back(source);
    }
}

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
    domain.reportGlow = std::any_of(
        domainLayer.specialOutputs.begin(), domainLayer.specialOutputs.end(), [](const VfxSpecialOutputData& output) {
            return output.enabled && output.type == VfxSpecialOutputType::Light && output.light.followFluidGlow;
        });

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
        FLUID::FluidSourceSubmission source{};
        if (MakeFluidSource(instance, index, spawnDelta, wrapLoop, source) &&
            domain.sources.size() < FLUID::kMaximumFluidSourcesPerDomain)
            domain.sources.push_back(source);
    }
    renderInput_.fluids.push_back(std::move(domain));
}

void VfxPlaybackService::ReceiveFluidGlow(std::span<const FLUID::FluidGlowReport> reports) {
    std::erase_if(fluidGlow_, [this](const FluidGlow& glow) { return !IsActive(glow.handle); });
    for (const FLUID::FluidGlowReport& report : reports) {
        if (report.key.playbackNamespace != playbackNamespace_)
            continue;
        const VfxPlaybackHandle handle{report.key.playbackSlot, report.key.playbackGeneration};
        if (!IsActive(handle))
            continue;
        const auto found = std::find_if(fluidGlow_.begin(), fluidGlow_.end(), [&](const FluidGlow& glow) {
            return glow.handle.slot == handle.slot && glow.handle.generation == handle.generation &&
                   glow.emitterIndex == report.key.emitterIndex;
        });
        if (found != fluidGlow_.end())
            found->report = report;
        else
            fluidGlow_.push_back({handle, report.key.emitterIndex, report});
    }
}

const FLUID::FluidGlowReport* VfxPlaybackService::FindFluidGlow(VfxPlaybackHandle handle,
                                                                uint32_t emitterIndex) const noexcept {
    for (const FluidGlow& glow : fluidGlow_)
        if (glow.handle.slot == handle.slot && glow.handle.generation == handle.generation &&
            glow.emitterIndex == emitterIndex)
            return &glow.report;
    return nullptr;
}

} // namespace HIKARI::VFX
