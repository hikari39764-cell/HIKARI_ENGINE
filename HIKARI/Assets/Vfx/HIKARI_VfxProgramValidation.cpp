#include "Assets/Vfx/HIKARI_VfxBudgets.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Render3D/Vfx/HIKARI_ParticleCapacity.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Assets/Vfx/HIKARI_VfxProgram.h"

#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <string>
#include <unordered_set>
#include <utility>

namespace HIKARI::VFX {
namespace {
using namespace RENDER3D::PARTICLES;

using MATH::IsFinite;

bool IsValidDistribution(const ParticleDistributionFloat& value) noexcept {
    return IsFinite(value.minimum) && IsFinite(value.maximum) &&
           (value.mode == ParticleDistributionMode::Constant ||
            (value.mode == ParticleDistributionMode::RandomRange && value.minimum <= value.maximum));
}

bool IsValidDistribution(const ParticleDistributionFloat3& value) noexcept {
    return IsFinite(value.minimum) && IsFinite(value.maximum) &&
           (value.mode == ParticleDistributionMode::Constant ||
            (value.mode == ParticleDistributionMode::RandomRange && value.minimum.x <= value.maximum.x &&
             value.minimum.y <= value.maximum.y && value.minimum.z <= value.maximum.z));
}

bool IsValidDistribution(const ParticleDistributionFloat4& value) noexcept {
    return IsFinite(value.minimum) && IsFinite(value.maximum) &&
           (value.mode == ParticleDistributionMode::Constant ||
            (value.mode == ParticleDistributionMode::RandomRange && value.minimum.x <= value.maximum.x &&
             value.minimum.y <= value.maximum.y && value.minimum.z <= value.maximum.z &&
             value.minimum.w <= value.maximum.w));
}

bool IsValidCurve(const ParticleCurve& curve) noexcept {
    if (curve.keyCount == 0u || curve.keyCount > kMaxParticleCurveKeys ||
        (curve.interpolation != ParticleCurveInterpolation::Linear &&
         curve.interpolation != ParticleCurveInterpolation::Smooth &&
         curve.interpolation != ParticleCurveInterpolation::Step) ||
        !IsValidDistribution(curve.randomOffset)) {
        return false;
    }
    float previousTime = -1.0f;
    for (uint32_t index = 0u; index < curve.keyCount; ++index) {
        const ParticleCurveKey& key = curve.keys[index];
        if (!IsFinite(key.time) || !IsFinite(key.value) || !IsFinite(key.inTangent) || !IsFinite(key.outTangent) ||
            key.time < 0.0f || key.time > 1.0f || key.time <= previousTime) {
            return false;
        }
        previousTime = key.time;
    }
    return true;
}

bool IsValidEasing(ParticleEasingFunction value) noexcept {
    return value == ParticleEasingFunction::Linear || value == ParticleEasingFunction::EaseIn ||
           value == ParticleEasingFunction::EaseOut || value == ParticleEasingFunction::EaseInOut;
}

bool IsValidAnimated(const ParticleAnimatedFloat3& value) noexcept {
    switch (value.mode) {
    case ParticleValueMode::Fixed: return IsValidDistribution(value.initial);
    case ParticleValueMode::Pva:
        return IsValidDistribution(value.initial) && IsValidDistribution(value.velocity) &&
               IsValidDistribution(value.acceleration);
    case ParticleValueMode::Easing:
        return IsValidDistribution(value.easingStart) &&
               (!value.hasEasingMiddle || IsValidDistribution(value.easingMiddle)) &&
               IsValidDistribution(value.easingEnd) && IsValidEasing(value.easingFunction) &&
               value.hasEasingMiddle <= 1u;
    case ParticleValueMode::Curve: return std::all_of(value.curves.begin(), value.curves.end(), IsValidCurve);
    default: return false;
    }
}

bool IsValidAnimated(const ParticleAnimatedColor& value) noexcept {
    switch (value.mode) {
    case ParticleColorMode::Fixed:
    case ParticleColorMode::RandomRange: return IsValidDistribution(value.fixed);
    case ParticleColorMode::Easing:
        return IsValidDistribution(value.easingStart) &&
               (!value.hasEasingMiddle || IsValidDistribution(value.easingMiddle)) &&
               IsValidDistribution(value.easingEnd) && IsValidEasing(value.easingFunction) &&
               value.hasEasingMiddle <= 1u;
    case ParticleColorMode::Curve: return std::all_of(value.curves.begin(), value.curves.end(), IsValidCurve);
    case ParticleColorMode::Gradient:
        if (value.gradient.stopCount == 0u || value.gradient.stopCount > kMaxParticleColorStops) {
            return false;
        }
        for (uint32_t index = 0u; index < value.gradient.stopCount; ++index) {
            const ParticleColorStop& stop = value.gradient.stops[index];
            if (!IsFinite(stop.time) || !IsFinite(stop.color) || stop.time < 0.0f || stop.time > 1.0f ||
                (index > 0u && stop.time <= value.gradient.stops[index - 1u].time)) {
                return false;
            }
        }
        return true;
    default: return false;
    }
}

VfxValueType ExpectedInputType(VfxRuntimeBindingSemantic semantic) noexcept {
    switch (semantic) {
    case VfxRuntimeBindingSemantic::EmitterEnabled: return VfxValueType::Bool;
    case VfxRuntimeBindingSemantic::ColorMultiplier: return VfxValueType::Color;
    case VfxRuntimeBindingSemantic::SpawnRateMultiplier:
    case VfxRuntimeBindingSemantic::RadialSpeedMultiplier:
    case VfxRuntimeBindingSemantic::ScaleMultiplier:
    case VfxRuntimeBindingSemantic::GravityMultiplier:
    default: return VfxValueType::Float;
    }
}

uint32_t StorageSize(ParticleRuntimePropertyType type) noexcept {
    switch (type) {
    case ParticleRuntimePropertyType::Bool:
    case ParticleRuntimePropertyType::Float: return sizeof(uint32_t);
    case ParticleRuntimePropertyType::Float4: return sizeof(MATH::Vec4);
    default: return 0u;
    }
}
bool Fail(std::string& outError, std::string message) {
    outError = std::move(message);
    return false;
}

template <typename Enum, std::size_t Count>
bool IsOneOf(Enum value, const Enum (&allowed)[Count]) noexcept {
    return std::find(std::begin(allowed), std::end(allowed), value) != std::end(allowed);
}

constexpr ParticleShape kEmitterShapes[] = {
    ParticleShape::Point,    ParticleShape::Sphere, ParticleShape::Cone, ParticleShape::Box,
    ParticleShape::Cylinder, ParticleShape::Circle, ParticleShape::Edge,
};
constexpr ParticleMaterialMode kMaterialModes[] = {
    ParticleMaterialMode::StandardUnlit,
    ParticleMaterialMode::CustomMaterial,
    ParticleMaterialMode::StandardLit,
    ParticleMaterialMode::CustomLit,
};
constexpr ParticleBlendMode kBlendModes[] = {ParticleBlendMode::Alpha, ParticleBlendMode::Additive,
                                             ParticleBlendMode::Distortion};
constexpr ParticleRenderShape kRenderShapes[] = {
    ParticleRenderShape::Sprite,    ParticleRenderShape::Ribbon,         ParticleRenderShape::Ring,
    ParticleRenderShape::MeshBox,   ParticleRenderShape::MeshSphere,     ParticleRenderShape::MeshCylinder,
    ParticleRenderShape::MeshCone,  ParticleRenderShape::MeshHemisphere, ParticleRenderShape::CrossPlane,
    ParticleRenderShape::MeshTorus, ParticleRenderShape::MeshCapsule,
};
constexpr ParticleBillboardMode kBillboardModes[] = {
    ParticleBillboardMode::RotatedBillboard,     ParticleBillboardMode::FixedYAxis,
    ParticleBillboardMode::DirectionalBillboard, ParticleBillboardMode::CylindricalBillboard,
    ParticleBillboardMode::FaceCameraPosition,   ParticleBillboardMode::Fixed,
};
constexpr ParticleDepthMode kDepthModes[] = {
    ParticleDepthMode::WorldOccluded,
    ParticleDepthMode::SoftIntersection,
    ParticleDepthMode::ContactSurface,
    ParticleDepthMode::ForegroundAccent,
};
constexpr VfxAttachmentMode kAttachmentModes[] = {
    VfxAttachmentMode::World,
    VfxAttachmentMode::Owner,
    VfxAttachmentMode::OwnerSocket,
};
constexpr VfxFollowMode kFollowModes[] = {
    VfxFollowMode::None,
    VfxFollowMode::Position,
    VfxFollowMode::Rotation,
    VfxFollowMode::Transform,
};
constexpr VfxSpecialOutputType kSpecialOutputTypes[] = {
    VfxSpecialOutputType::WeaponPathDistortion,
    VfxSpecialOutputType::ScreenImpulse,
    VfxSpecialOutputType::SkinnedAfterimage,
    VfxSpecialOutputType::Light,
};

bool IsNonZeroScale(const MATH::Vec3& scale) noexcept { return scale.x != 0.0f && scale.y != 0.0f && scale.z != 0.0f; }

bool IsValidProgramString(const std::string& value) noexcept {
    return !value.empty() && value.size() <= kMaximumVfxProgramStringBytes;
}

bool ValidateProgramHeader(const VfxProgram& program, std::string& outError) {
    if (program.version != kVfxProgramVersion || program.sourceVersion != kVfxSourceVersion ||
        program.sourceHash == 0u || !IsValidProgramString(program.programId) || !IsValidProgramString(program.name) ||
        program.emitters.empty() || !IsFinite(program.durationSeconds) || program.durationSeconds <= 0.0f ||
        !IsFinite(program.maximumParticleLifetimeSeconds) || program.maximumParticleLifetimeSeconds < 0.0f) {
        return Fail(outError, "VFX program header fields are invalid");
    }
    if (program.inputs.size() > kMaximumVfxProgramInputs || program.emitters.size() > kMaximumVfxProgramEmitters ||
        program.dependencyGuids.size() > kMaximumVfxProgramDependencies) {
        return Fail(outError, "VFX program table exceeds the runtime limit");
    }
    std::unordered_set<std::string> inputIds{};
    for (const VfxProgramInput& input : program.inputs) {
        if (!IsValidProgramString(input.id) || !inputIds.emplace(input.id).second ||
            input.defaultValue.type != input.type || !input.defaultValue.IsFinite()) {
            return Fail(outError, "VFX program input table is invalid");
        }
    }
    return true;
}

// 実行時エフェクトと、レイヤーカーブが参照するマテリアル式を検証する。
bool ValidateRuntimeEffects(const VfxProgramEmitter& emitter,
                            RENDER3D::MATERIAL::MaterialExpressionRuntimeProgram& expression, std::string& outError) {
    if (!emitter.animationTiming.IsValid() ||
        emitter.layerCurves.size() >
            RENDER3D::MATERIAL::kGpuMaterialProgramParameterSlotCount + 18 + kMaximumVfxRuntimeEffectCurves) {
        return Fail(outError, "Layer curve budget exceeded");
    }
    if (emitter.render.effectMaterialIncludesParticleColor && !emitter.render.effectMaterial) {
        return Fail(outError, "Local adjustment material is missing");
    }
    if (!emitter.render.animatedParameters.empty() || !emitter.render.spatialEffects.empty()) {
        return Fail(outError, "Compiled assets cannot contain per-frame parameter values");
    }
    const bool animatesMaterial =
        emitter.kind != VfxLayerKind::Adjustment &&
        std::any_of(emitter.layerCurves.begin(), emitter.layerCurves.end(), [](const auto& curve) {
            return curve.channel >= VfxLayerChannel::MaterialParameter &&
                   curve.channel <= VfxLayerChannel::MaterialEffectMix;
        });
    if (animatesMaterial) {
        if (!emitter.render.effectMaterial) {
            return Fail(outError, "Invalid layer animation channel");
        }
        if (!RENDER3D::MATERIAL::BuildMaterialExpressionRuntimeProgram(
                emitter.render.effectMaterial->compileResult.program, expression, outError)) {
            return false;
        }
    }
    if (emitter.runtimeEffects.size() > kMaximumVfxEffectsPerLayer) {
        return Fail(outError, "Runtime effect budget exceeded");
    }
    for (const auto& effect : emitter.runtimeEffects) {
        if (const auto error = ValidateMaterialEffect(effect); !error.empty()) {
            return Fail(outError, "Invalid runtime effect: " + std::string(error));
        }
        if (effect.space == MaterialEffectSpace::LayerUv &&
            (emitter.kind != VfxLayerKind::Adjustment || !IsCompositeAdjustmentEffect(effect) ||
             !IsAdjustmentMaterialEffect(effect.kind))) {
            return Fail(outError, "Invalid runtime effect domain");
        }
        float previous = -1;
        for (const auto& stop : effect.colorStops) {
            if (stop.time <= previous) {
                return Fail(outError, "Invalid runtime gradient");
            }
            previous = stop.time;
        }
    }
    return true;
}

// 空間変形の所有者は自身か、同じグループ内で先行する調整レイヤーに限る。
bool ValidateSpatialOwners(const VfxProgram& program, uint32_t index, const VfxProgramEmitter& emitter,
                           std::string& outError) {
    std::unordered_set<uint32_t> owners;
    size_t spatialCount = 0;
    for (const auto owner : emitter.spatialOwners) {
        if (owner >= program.emitters.size() || !owners.insert(owner).second ||
            (owner != index && program.emitters[owner].kind != VfxLayerKind::Adjustment)) {
            return Fail(outError, "Invalid spatial effect owner");
        }
        const size_t previousCount = spatialCount;
        for (const auto& effect : program.emitters[owner].runtimeEffects) {
            if (effect.enabled && effect.space == MaterialEffectSpace::Group3D) {
                ++spatialCount;
            }
        }
        if (previousCount == spatialCount) {
            return Fail(outError, "Spatial owner has no enabled deformation");
        }
        if (owner == index) {
            continue;
        }
        bool inScope = false;
        uint32_t scope = index;
        for (size_t depth = 0; depth < program.emitters.size() && scope < program.emitters.size(); ++depth) {
            const auto parent = program.emitters[scope].parentEmitterIndex;
            if (owner < scope && program.emitters[owner].parentEmitterIndex == parent) {
                inScope = true;
                break;
            }
            scope = parent;
        }
        if (!inScope) {
            return Fail(outError, "Spatial owner does not precede content in its group");
        }
    }
    if (spatialCount > RENDER3D::PARTICLES::kMaximumParticleSpatialEffects) {
        return Fail(outError, "A layer supports at most eight active spatial deformations");
    }
    return true;
}

bool ValidateLayerCurves(const VfxProgram& program, uint32_t index, const VfxProgramEmitter& emitter,
                         const RENDER3D::MATERIAL::MaterialExpressionRuntimeProgram& expression,
                         std::string& outError) {
    std::unordered_set<uint64_t> layerChannels;
    for (const auto& curve : emitter.layerCurves) {
        const auto channel = static_cast<uint32_t>(curve.channel);
        if (curve.ownerEmitterIndex != UINT32_MAX &&
            (curve.ownerEmitterIndex >= program.emitters.size() || curve.ownerEmitterIndex == index ||
             program.emitters[curve.ownerEmitterIndex].kind != VfxLayerKind::Adjustment ||
             curve.channel < VfxLayerChannel::MaterialParameter)) {
            return Fail(outError, "Invalid adjustment curve owner");
        }
        const bool runtime =
            curve.channel == VfxLayerChannel::EffectParameter || curve.channel == VfxLayerChannel::EffectPhase;
        const uint64_t identity = (uint64_t(runtime ? uint32_t(VfxLayerChannel::EffectParameter)
                                                    : (std::min)(channel, uint32_t(VfxLayerChannel::MaterialParameter)))
                                   << 32) |
                                  curve.materialSlot;
        if (runtime &&
            (curve.ownerEmitterIndex != UINT32_MAX || curve.materialSlot / 9u >= emitter.runtimeEffects.size())) {
            return Fail(outError, "Invalid runtime effect animation slot");
        }
        const bool drivesLayerMaterial = !runtime && emitter.kind != VfxLayerKind::Adjustment &&
                                         channel >= static_cast<uint32_t>(VfxLayerChannel::MaterialParameter);
        if (drivesLayerMaterial &&
            std::none_of(expression.parameterValueSlots.begin(), expression.parameterValueSlots.end(),
                         [&](const auto& slot) { return slot.second == curve.materialSlot; })) {
            return Fail(outError, "Animated parameter does not reference a material value slot");
        }
        if (!std::isfinite(curve.minimum) || !std::isfinite(curve.maximum) || curve.minimum > curve.maximum ||
            channel > static_cast<uint32_t>(VfxLayerChannel::EffectPhase) ||
            (emitter.kind == VfxLayerKind::Particles && channel < 13) ||
            (emitter.kind == VfxLayerKind::Adjustment && channel >= 18 && !runtime) ||
            (curve.channel == VfxLayerChannel::MaterialEffectMix && curve.ownerEmitterIndex == UINT32_MAX) ||
            (!runtime && curve.materialSlot >= RENDER3D::MATERIAL::kGpuMaterialProgramParameterSlotCount) ||
            (drivesLayerMaterial && !emitter.render.effectMaterial) ||
            static_cast<uint32_t>(curve.interpolation) > static_cast<uint32_t>(ParticleCurveInterpolation::Step) ||
            !layerChannels.emplace(identity).second || curve.keys.empty() ||
            curve.keys.size() > kMaxVfxLayerCurveKeys) {
            return Fail(outError, "Invalid layer animation channel");
        }
        float previous = -1;
        for (const auto& key : curve.keys) {
            if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.inTangent) ||
                !std::isfinite(key.outTangent) || key.time < 0 || key.time > 1 || key.time <= previous) {
                return Fail(outError, "Invalid layer animation key");
            }
            previous = key.time;
        }
    }
    return true;
}

// 流体層の設定は字段表の範囲の中にあり、流体源は流体域の層を指す。
bool IsValidFluidLayer(const VfxProgram& program, const VfxProgramEmitter& emitter) noexcept {
    namespace FLUID = RENDER3D::FLUID;
    if (emitter.kind == VfxLayerKind::Fluid)
        return emitter.fluidDomainIndex == kInvalidVfxProgramIndex &&
               JSONFIELDS::Sanitize(emitter.fluidDomain, FLUID::kFluidDomainSettingsFields) == emitter.fluidDomain;
    if (emitter.kind == VfxLayerKind::FluidSource)
        return emitter.fluidDomainIndex < program.emitters.size() &&
               program.emitters[emitter.fluidDomainIndex].kind == VfxLayerKind::Fluid &&
               JSONFIELDS::Sanitize(emitter.fluidSource, FLUID::kFluidSourceSettingsFields) == emitter.fluidSource;
    return emitter.fluidDomainIndex == kInvalidVfxProgramIndex;
}

bool IsValidEmitterIdentity(const VfxProgram& program, uint32_t index, const VfxProgramEmitter& emitter) noexcept {
    return emitter.emitterIndex == index && IsValidVfxHistory(emitter.history) &&
           (emitter.history.mode == VfxHistoryMode::None ||
            (emitter.kind == VfxLayerKind::Shape && emitter.simulation.capacity == emitter.history.samples)) &&
           static_cast<uint8_t>(emitter.kind) <= static_cast<uint8_t>(VfxLayerKind::FluidSource) &&
           IsValidFluidLayer(program, emitter) && IsValidProgramString(emitter.sourceEmitterId) &&
           (emitter.parentEmitterIndex == kInvalidVfxProgramIndex ||
            emitter.parentEmitterIndex < program.emitters.size()) &&
           emitter.parentEmitterIndex != index && IsFinite(emitter.transform.position) &&
           IsFinite(emitter.transform.rotationEulerDegrees) && IsFinite(emitter.transform.scale) &&
           IsNonZeroScale(emitter.transform.scale);
}

bool IsValidEmitterShape(const ParticleEmitterSimulationParameters& simulation) noexcept {
    return IsValidDistribution(simulation.shapeRadius) && IsOneOf(simulation.shape, kEmitterShapes) &&
           IsFinite(simulation.coneAngleRadians) && simulation.coneAngleRadians >= 0.0f &&
           simulation.coneAngleRadians <= 3.14159265358979323846f && IsFinite(simulation.shapeBoxHalfExtents) &&
           simulation.shapeBoxHalfExtents.x >= 0.0f && simulation.shapeBoxHalfExtents.y >= 0.0f &&
           simulation.shapeBoxHalfExtents.z >= 0.0f && IsFinite(simulation.shapeCylinderHeight) &&
           simulation.shapeCylinderHeight >= 0.0f && IsFinite(simulation.shapeCircleInnerRadius) &&
           simulation.shapeCircleInnerRadius >= 0.0f && simulation.shapeCircleEmitOnEdge <= 1u &&
           IsFinite(simulation.shapeEdgeLength) && simulation.shapeEdgeLength >= 0.0f &&
           (simulation.shape != ParticleShape::Circle ||
            simulation.shapeCircleInnerRadius <= simulation.shapeRadius.minimum);
}

bool IsValidEmitterSimulation(const VfxProgram& program, const VfxProgramEmitter& emitter) noexcept {
    const ParticleEmitterSimulationParameters& simulation = emitter.simulation;
    const float maximumLifetime = simulation.lifetime.mode == ParticleDistributionMode::RandomRange
                                      ? simulation.lifetime.maximum
                                      : simulation.lifetime.minimum;
    const ParticleEmitterRuntimeOverrides& overrides = emitter.authoredOverrides;
    return simulation.capacity != 0u && simulation.capacity <= kGpuParticlePoolCapacity &&
           IsValidDistribution(simulation.lifetime) && simulation.lifetime.minimum > 0.0f &&
           (emitter.kind != VfxLayerKind::Particles || maximumLifetime <= program.maximumParticleLifetimeSeconds) &&
           IsValidEmitterShape(simulation) && IsValidDistribution(simulation.radialSpeed) &&
           IsValidAnimated(simulation.position) && IsValidAnimated(simulation.rotationRadians) &&
           IsValidAnimated(simulation.scale) && RENDER3D::PARTICLES::IsValidParticleForceField(simulation.field) &&
           IsFinite(simulation.gravity) && IsFinite(simulation.drag) && simulation.drag >= 0.0f &&
           IsFinite(simulation.fluidFollow) && simulation.fluidFollow >= 0.0f &&
           simulation.fluidFollow <= RENDER3D::PARTICLES::kMaxParticleFluidFollow && simulation.localSimulation <= 1u &&
           simulation.collisionPlaneEnabled <= 1u && IsFinite(simulation.collisionPlaneHeight) &&
           IsFinite(simulation.restitution) && simulation.restitution >= 0.0f && simulation.restitution <= 1.0f &&
           IsFinite(simulation.friction) && simulation.friction >= 0.0f && simulation.friction <= 1.0f &&
           IsValidAnimated(simulation.color) && IsFinite(emitter.spawnRatePerSecond) &&
           emitter.spawnRatePerSecond >= 0.0f && overrides.enabled <= 1u && IsFinite(overrides.spawnRateMultiplier) &&
           IsFinite(overrides.radialSpeedMultiplier) && IsFinite(overrides.scaleMultiplier) &&
           IsFinite(overrides.gravityMultiplier) && IsFinite(overrides.colorMultiplier);
}

bool IsValidOptionalAssetReference(const std::string& guid, bool allowBuiltinTexture) {
    return guid.empty() || (guid.size() <= kMaximumVfxProgramStringBytes &&
                            ((allowBuiltinTexture && IsVfxBuiltinTextureAssetId(guid)) || IsValidAssetGuid(guid)));
}

bool IsValidEmitterRender(const ParticleEmitterRenderParameters& render) {
    const uint64_t flipbookCells =
        static_cast<uint64_t>(render.flipbookColumns) * static_cast<uint64_t>(render.flipbookRows);
    return TEXT::IsNamedEnum(kVfxPostProcessModeNames, render.postProcessMode) &&
           (render.meshAssetGuid.empty() || render.postProcessMode == VfxPostProcessMode::Scene) &&
           IsOneOf(render.materialMode, kMaterialModes) && IsOneOf(render.blendMode, kBlendModes) &&
           IsOneOf(render.shape, kRenderShapes) && IsOneOf(render.billboardMode, kBillboardModes) &&
           IsOneOf(render.depthMode, kDepthModes) && IsFinite(render.softIntersectionDistance) &&
           (render.depthMode != ParticleDepthMode::SoftIntersection ||
            (render.softIntersectionDistance >= 0.001f && render.softIntersectionDistance <= 10.0f)) &&
           IsFinite(render.depthBias) &&
           (render.depthMode != ParticleDepthMode::ContactSurface ||
            (render.depthBias >= 0.0f && render.depthBias <= 0.02f)) &&
           render.flipbookColumns != 0u && render.flipbookColumns <= 64u && render.flipbookRows != 0u &&
           render.flipbookRows <= 64u && render.flipbookFrameCount != 0u &&
           render.flipbookFrameCount <= flipbookCells && IsFinite(render.flipbookFramesPerSecond) &&
           render.flipbookFramesPerSecond >= 0.0f && render.flipbookFramesPerSecond <= 240.0f && render.uvMode <= 1u &&
           render.uvSwapAxes <= 1u && IsFinite(render.uvTileLength) && IsFinite(render.uvTilingU) &&
           IsFinite(render.uvTilingV) && render.uvTilingV > 0.0f && render.uvTilingU > 0.0f &&
           (render.uvMode != 1u || render.uvTileLength > 0.0f) && IsFinite(render.uvRotationRadians) &&
           IsFinite(render.anchor) && IsValidOptionalAssetReference(render.colorTextureAssetGuid, true) &&
           IsValidOptionalAssetReference(render.materialAssetGuid, false);
}

bool ValidateActivation(const VfxProgram& program, const VfxProgramEmitter& emitter, std::string& outError) {
    const VfxAttachmentData& attachment = emitter.attachment;
    if (!IsFinite(emitter.startDelay) || !IsFinite(emitter.activeDuration) || emitter.startDelay < 0.0f ||
        emitter.activeDuration < 0.0f || emitter.startDelay > 3600.0f || emitter.activeDuration > 3600.0f ||
        emitter.startDelay > program.durationSeconds ||
        (emitter.activeDuration > 0.0f && emitter.startDelay + emitter.activeDuration > program.durationSeconds) ||
        !IsOneOf(attachment.mode, kAttachmentModes) || !IsOneOf(attachment.follow, kFollowModes) ||
        (attachment.mode == VfxAttachmentMode::OwnerSocket && attachment.socketName.empty()) ||
        attachment.socketName.size() > kMaximumVfxProgramStringBytes || !IsFinite(attachment.offset.position) ||
        !IsFinite(attachment.offset.rotationEulerDegrees) || !IsFinite(attachment.offset.scale) ||
        !IsNonZeroScale(attachment.offset.scale)) {
        return Fail(outError, "VFX program emitter activation/attachment is invalid");
    }
    return true;
}

bool ValidateBurstTimeline(const VfxProgram& program, const VfxProgramEmitter& emitter, std::string& outError) {
    if (emitter.burstTimeline.size() > kMaximumVfxProgramBurstKeys) {
        return Fail(outError, "VFX program burst timeline is too large");
    }
    const double effectiveActiveDuration =
        emitter.activeDuration > 0.0f
            ? static_cast<double>(emitter.activeDuration)
            : (std::max)(0.0, static_cast<double>(program.durationSeconds) - static_cast<double>(emitter.startDelay));
    std::unordered_set<std::string> burstIds{};
    float previousBurstTime = 0.0f;
    for (const VfxBurstKeyData& key : emitter.burstTimeline) {
        if (!IsValidProgramString(key.id) || !burstIds.emplace(key.id).second || !IsFinite(key.time) ||
            key.time < 0.0f || key.time > 3600.0f || key.count == 0u || key.count > emitter.simulation.capacity) {
            return Fail(outError, "VFX program burst timeline is invalid");
        }
        if (key.time < previousBurstTime) {
            return Fail(outError, "VFX program burst timeline is not sorted");
        }
        previousBurstTime = key.time;
        if (static_cast<double>(key.time) > effectiveActiveDuration) {
            return Fail(outError, "VFX program burst key exceeds emitter activation");
        }
    }
    return true;
}

bool ValidateSpecialOutputs(const VfxProgramEmitter& emitter, std::unordered_set<std::string>& specialOutputIds,
                            std::string& outError) {
    if (emitter.specialOutputs.size() > kMaximumVfxProgramSpecialOutputs) {
        return Fail(outError, "VFX program special output table is too large");
    }
    for (const VfxSpecialOutputData& output : emitter.specialOutputs) {
        if (!IsValidProgramString(output.id) || !specialOutputIds.emplace(output.id).second ||
            !IsOneOf(output.type, kSpecialOutputTypes)) {
            return Fail(outError, "VFX program special output table is invalid");
        }
        if (!IsValidVfxSpecialOutputProfile(output)) {
            return Fail(outError, "VFX program special output profile is invalid");
        }
    }
    return true;
}

bool ValidateInputLinks(const VfxProgram& program, const VfxProgramEmitter& emitter, uint64_t& totalInputLinks,
                        std::string& outError) {
    if (emitter.inputLinks.size() > kMaximumVfxProgramLinksPerEmitter) {
        return Fail(outError, "VFX program input link table is too large");
    }
    totalInputLinks += emitter.inputLinks.size();
    if (totalInputLinks > kMaximumVfxProgramTableItems) {
        return Fail(outError, "VFX program total input link table is too large");
    }
    std::unordered_set<uint32_t> boundSemantics{};
    for (const VfxProgramInputLink& link : emitter.inputLinks) {
        if (!TEXT::IsNamedEnum(kVfxRuntimeBindingNames, link.semantic)) {
            return Fail(outError, "VFX program input semantic is invalid");
        }
        const VfxRuntimePropertyLayout expected = ResolveVfxRuntimePropertyLayout(link.semantic);
        const uint32_t storageSize = StorageSize(expected.storageType);
        const size_t ownerSize = expected.renderParameter ? sizeof(ParticleEmitterRenderParameters)
                                                          : sizeof(ParticleEmitterRuntimeOverrides);
        if (!boundSemantics.emplace(static_cast<uint32_t>(link.semantic)).second ||
            link.inputIndex >= program.inputs.size() || link.inputType != program.inputs[link.inputIndex].type ||
            !IsVfxInputTypeCompatible(link.inputType, ExpectedInputType(link.semantic)) ||
            link.byteOffset != expected.byteOffset || link.storageType != expected.storageType || storageSize == 0u ||
            expected.byteOffset > ownerSize - storageSize || !IsFinite(link.scale) || !IsFinite(link.bias)) {
            return Fail(outError, "VFX program input link is invalid");
        }
    }
    return true;
}

bool ValidateEmitter(const VfxProgram& program, uint32_t index, std::unordered_set<std::string>& emitterIds,
                     std::unordered_set<std::string>& specialOutputIds, uint64_t& totalInputLinks,
                     std::string& outError) {
    const VfxProgramEmitter& emitter = program.emitters[index];
    RENDER3D::MATERIAL::MaterialExpressionRuntimeProgram expression;
    if (!ValidateRuntimeEffects(emitter, expression, outError) ||
        !ValidateSpatialOwners(program, index, emitter, outError) ||
        !ValidateLayerCurves(program, index, emitter, expression, outError)) {
        return false;
    }
    if (!IsValidEmitterIdentity(program, index, emitter) || !emitterIds.emplace(emitter.sourceEmitterId).second ||
        !IsValidEmitterSimulation(program, emitter) || !IsValidEmitterRender(emitter.render)) {
        return Fail(outError, "VFX program emitter table is invalid");
    }
    return ValidateActivation(program, emitter, outError) && ValidateBurstTimeline(program, emitter, outError) &&
           ValidateSpecialOutputs(emitter, specialOutputIds, outError) &&
           ValidateInputLinks(program, emitter, totalInputLinks, outError);
}

// 経路追従とトラックマットは同じ親の下で循環しない参照に限る。
bool ValidateComposition(const VfxProgram& program, std::string& outError) {
    const bool requiresScreenComposition =
        std::any_of(program.emitters.begin(), program.emitters.end(), [](const auto& layer) {
            return layer.matteSourceIndex != UINT32_MAX || layer.matteOnly ||
                   std::any_of(layer.runtimeEffects.begin(), layer.runtimeEffects.end(), [](const auto& effect) {
                       return effect.enabled && effect.space == MaterialEffectSpace::LayerUv;
                   });
        });
    const VfxProgramEmitter* firstContent = nullptr;
    for (const auto& layer : program.emitters) {
        if (requiresScreenComposition && (layer.kind == VfxLayerKind::Particles || layer.kind == VfxLayerKind::Shape)) {
            if (firstContent && firstContent->render.postProcessMode != layer.render.postProcessMode)
                return Fail(outError, "Track matte and LayerUv compositions require one content post-process mode");
            firstContent = &layer;
        }
        if (layer.orientAlongPath && layer.kind != VfxLayerKind::Shape && layer.kind != VfxLayerKind::Group) {
            return Fail(outError, "Invalid path orientation layer");
        }
        if (layer.kind == VfxLayerKind::Adjustment && (layer.matteOnly || layer.matteSourceIndex != UINT32_MAX)) {
            return Fail(outError, "Invalid matte-only adjustment");
        }
        std::unordered_set<uint32_t> visited{layer.emitterIndex};
        auto source = layer.matteSourceIndex;
        while (source != UINT32_MAX) {
            if (source >= program.emitters.size() || !visited.insert(source).second ||
                program.emitters[source].parentEmitterIndex != layer.parentEmitterIndex ||
                program.emitters[source].kind == VfxLayerKind::Adjustment) {
                return Fail(outError, "Invalid or cyclic track matte reference");
            }
            source = program.emitters[source].matteSourceIndex;
        }
    }
    return true;
}

bool ValidateHierarchy(const VfxProgram& program, std::string& outError) {
    const bool hasComposition = std::any_of(program.emitters.begin(), program.emitters.end(), [](const auto& emitter) {
        return emitter.kind == VfxLayerKind::Adjustment || emitter.matteSourceIndex != UINT32_MAX || emitter.matteOnly;
    });
    std::unordered_set<uint32_t> visited{};
    for (uint32_t emitterIndex = 0u; emitterIndex < program.emitters.size(); ++emitterIndex) {
        visited.clear();
        if (hasComposition && !program.emitters[emitterIndex].render.meshAssetGuid.empty()) {
            return Fail(outError, "VFX composition does not support model output");
        }
        uint32_t current = emitterIndex;
        while (current != kInvalidVfxProgramIndex) {
            if (current >= program.emitters.size() || !visited.emplace(current).second) {
                return Fail(outError, "VFX program emitter hierarchy is invalid");
            }
            current = program.emitters[current].parentEmitterIndex;
            if ((hasComposition && visited.size() > 8u) ||
                (current < program.emitters.size() && program.emitters[current].kind == VfxLayerKind::Adjustment)) {
                return Fail(outError,
                            "VFX composition hierarchy exceeds its scope limit or parents under an adjustment");
            }
        }
    }
    return true;
}

bool ValidateDependencies(const VfxProgram& program, std::string& outError) {
    std::unordered_set<std::string> dependencies{};
    for (const AssetGuid& dependency : program.dependencyGuids) {
        if (!dependency.IsValid() || !dependencies.emplace(dependency.value).second) {
            return Fail(outError, "VFX program dependency table is invalid");
        }
    }
    return true;
}
} // namespace

bool ValidateVfxProgram(const VfxProgram& program, std::string& outError) {
    if (!ValidateProgramHeader(program, outError)) {
        return false;
    }
    uint64_t totalCapacity = 0u;
    uint64_t totalInputLinks = 0u;
    std::unordered_set<std::string> emitterIds{};
    std::unordered_set<std::string> specialOutputIds{};
    for (uint32_t index = 0u; index < program.emitters.size(); ++index) {
        if (!ValidateEmitter(program, index, emitterIds, specialOutputIds, totalInputLinks, outError)) {
            return false;
        }
        const VfxProgramEmitter& emitter = program.emitters[index];
        if (emitter.kind != VfxLayerKind::Group && emitter.kind != VfxLayerKind::Adjustment &&
            emitter.kind != VfxLayerKind::Fluid && emitter.kind != VfxLayerKind::FluidSource) {
            totalCapacity += AlignParticleCapacity(emitter.simulation.capacity);
        }
    }
    if (totalCapacity > kGpuParticlePoolCapacity) {
        return Fail(outError, "VFX program aligned particle reservations exceed the pool");
    }
    if (!ValidateComposition(program, outError) || !ValidateHierarchy(program, outError) ||
        !ValidateDependencies(program, outError)) {
        return false;
    }
    outError.clear();
    return true;
}

} // namespace HIKARI::VFX
