#include "Assets/Vfx/HIKARI_VfxBudgets.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Assets/Vfx/HIKARI_VfxLayerAnimation.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"
#include "Assets/Vfx/HIKARI_VfxEnumNames.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"
#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"
#include "Assets/Vfx/HIKARI_VfxProgram.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::VFX {
namespace {
using MATH::IsFinite;

bool IsOrdered(float minimum, float maximum) noexcept { return minimum <= maximum; }

bool IsOrdered(const MATH::Vec3& minimum, const MATH::Vec3& maximum) noexcept {
    return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z;
}

bool IsOrdered(const MATH::Vec4& minimum, const MATH::Vec4& maximum) noexcept {
    return minimum.x <= maximum.x && minimum.y <= maximum.y && minimum.z <= maximum.z && minimum.w <= maximum.w;
}

void AddError(std::vector<VfxDiagnostic>& diagnostics, std::string code, std::string message,
              std::string subjectId = {}) {
    diagnostics.push_back({VfxDiagnosticSeverity::Error, std::move(code), std::move(message), std::move(subjectId)});
}

bool RegisterId(std::string_view id, std::string_view kind, std::unordered_set<std::string>& ids,
                std::vector<VfxDiagnostic>& diagnostics) {
    if (id.empty()) {
        AddError(diagnostics, "Vfx.MissingStableId", std::string(kind) + " requires a stable ID");
        return false;
    }
    if (!ids.emplace(id).second) {
        AddError(diagnostics, "Vfx.DuplicateStableId", "stable ID is already used: " + std::string(id),
                 std::string(id));
        return false;
    }
    return true;
}

template <class T>
bool IsDistributionValid(const VfxDistributionData<T>& distribution) noexcept {
    if (!IsFinite(distribution.minimum) || !IsFinite(distribution.maximum)) {
        return false;
    }
    if (distribution.mode == VfxDistributionMode::Constant) {
        return true;
    }
    return distribution.mode == VfxDistributionMode::RandomRange &&
           IsOrdered(distribution.minimum, distribution.maximum);
}

template <class T>
void ValidateDistribution(const VfxDistributionData<T>& distribution, std::string_view subjectId,
                          std::vector<VfxDiagnostic>& diagnostics) {
    if (!IsDistributionValid(distribution)) {
        AddError(diagnostics, "Vfx.InvalidDistribution",
                 "distribution values must be finite and random minimum "
                 "must not exceed maximum",
                 std::string(subjectId));
    }
}

void ValidateCurve(const VfxCurveData& curve, std::string_view ownerId, std::unordered_set<std::string>& ids,
                   std::vector<VfxDiagnostic>& diagnostics,
                   size_t maxKeys = RENDER3D::PARTICLES::kMaxParticleCurveKeys) {
    ValidateDistribution(curve.randomOffset, ownerId, diagnostics);
    if (curve.keys.empty()) {
        AddError(diagnostics, "Vfx.EmptyCurve", "curve requires at least one key", std::string(ownerId));
        return;
    }
    if (curve.keys.size() > maxKeys) {
        AddError(diagnostics, "Vfx.CurveKeyLimit", "curve exceeds its animation key budget", std::string(ownerId));
    }
    std::unordered_set<float> times{};
    for (const VfxCurveKeyData& key : curve.keys) {
        (void)RegisterId(key.id, "curve key", ids, diagnostics);
        if (!IsFinite(key.time) || key.time < 0.0f || key.time > 1.0f || !IsFinite(key.value) ||
            !IsFinite(key.inTangent) || !IsFinite(key.outTangent)) {
            AddError(diagnostics, "Vfx.InvalidCurveKey",
                     "curve key values must be finite and time must be "
                     "within [0, 1]",
                     key.id);
        }
        if (!times.emplace(key.time).second) {
            AddError(diagnostics, "Vfx.DuplicateCurveTime", "curve keys may not share the same time", key.id);
        }
    }
}

void ValidateVec3Curve(const VfxVec3CurveData& curve, std::string_view ownerId, std::unordered_set<std::string>& ids,
                       std::vector<VfxDiagnostic>& diagnostics,
                       size_t maxKeys = RENDER3D::PARTICLES::kMaxParticleCurveKeys) {
    ValidateCurve(curve.x, ownerId, ids, diagnostics, maxKeys);
    ValidateCurve(curve.y, ownerId, ids, diagnostics, maxKeys);
    ValidateCurve(curve.z, ownerId, ids, diagnostics, maxKeys);
}

void ValidateColorCurve(const VfxColorCurveData& curve, std::string_view ownerId, std::unordered_set<std::string>& ids,
                        std::vector<VfxDiagnostic>& diagnostics,
                        size_t maxKeys = RENDER3D::PARTICLES::kMaxParticleCurveKeys) {
    ValidateCurve(curve.r, ownerId, ids, diagnostics, maxKeys);
    ValidateCurve(curve.g, ownerId, ids, diagnostics, maxKeys);
    ValidateCurve(curve.b, ownerId, ids, diagnostics, maxKeys);
    ValidateCurve(curve.a, ownerId, ids, diagnostics, maxKeys);
}

void ValidateGradient(const VfxColorGradientData& gradient, std::string_view ownerId,
                      std::unordered_set<std::string>& ids, std::vector<VfxDiagnostic>& diagnostics) {
    if (gradient.stops.empty()) {
        AddError(diagnostics, "Vfx.EmptyGradient", "color gradient requires at least one stop", std::string(ownerId));
        return;
    }
    if (gradient.stops.size() > RENDER3D::PARTICLES::kMaxParticleColorStops) {
        AddError(diagnostics, "Vfx.ColorStopLimit", "color gradient exceeds the native GPU limit of 8 stops",
                 std::string(ownerId));
    }
    std::unordered_set<float> times{};
    for (const VfxColorStopData& stop : gradient.stops) {
        (void)RegisterId(stop.id, "color stop", ids, diagnostics);
        if (!IsFinite(stop.time) || stop.time < 0.0f || stop.time > 1.0f || !IsFinite(stop.color)) {
            AddError(diagnostics, "Vfx.InvalidColorStop",
                     "gradient values must be finite and time must be "
                     "within [0, 1]",
                     stop.id);
        }
        if (!times.emplace(stop.time).second) {
            AddError(diagnostics, "Vfx.DuplicateColorTime", "color stops may not share the same time", stop.id);
        }
    }
}

void ValidateAnimatedVec3(const VfxAnimatedVec3Data& value, std::string_view ownerId,
                          std::unordered_set<std::string>& ids, std::vector<VfxDiagnostic>& diagnostics,
                          size_t maxKeys = RENDER3D::PARTICLES::kMaxParticleCurveKeys) {
    switch (value.mode) {
    case VfxAnimatedValueMode::Fixed: ValidateDistribution(value.fixed, ownerId, diagnostics); break;
    case VfxAnimatedValueMode::Pva:
        ValidateDistribution(value.pva.initial, ownerId, diagnostics);
        ValidateDistribution(value.pva.velocity, ownerId, diagnostics);
        ValidateDistribution(value.pva.acceleration, ownerId, diagnostics);
        break;
    case VfxAnimatedValueMode::Easing:
        ValidateDistribution(value.easing.start, ownerId, diagnostics);
        if (value.easing.useMiddle) {
            ValidateDistribution(value.easing.middle, ownerId, diagnostics);
        }
        ValidateDistribution(value.easing.end, ownerId, diagnostics);
        break;
    case VfxAnimatedValueMode::Curve: ValidateVec3Curve(value.curve, ownerId, ids, diagnostics, maxKeys); break;
    }
}

void ValidateAnimatedColor(const VfxAnimatedColorData& value, std::string_view ownerId,
                           std::unordered_set<std::string>& ids, std::vector<VfxDiagnostic>& diagnostics,
                           size_t maxKeys = RENDER3D::PARTICLES::kMaxParticleCurveKeys) {
    switch (value.mode) {
    case VfxAnimatedColorMode::Fixed:
    case VfxAnimatedColorMode::RandomRange:
        ValidateDistribution(value.fixed, ownerId, diagnostics);
        if (value.mode == VfxAnimatedColorMode::Fixed && value.fixed.mode != VfxDistributionMode::Constant) {
            AddError(diagnostics, "Vfx.ColorModeMismatch", "fixed color requires a constant distribution",
                     std::string(ownerId));
        }
        if (value.mode == VfxAnimatedColorMode::RandomRange && value.fixed.mode != VfxDistributionMode::RandomRange) {
            AddError(diagnostics, "Vfx.ColorModeMismatch", "random color requires a random-range distribution",
                     std::string(ownerId));
        }
        break;
    case VfxAnimatedColorMode::Easing:
        ValidateDistribution(value.easing.start, ownerId, diagnostics);
        if (value.easing.useMiddle) {
            ValidateDistribution(value.easing.middle, ownerId, diagnostics);
        }
        ValidateDistribution(value.easing.end, ownerId, diagnostics);
        break;
    case VfxAnimatedColorMode::Curve: ValidateColorCurve(value.curve, ownerId, ids, diagnostics, maxKeys); break;
    case VfxAnimatedColorMode::Gradient: ValidateGradient(value.gradient, ownerId, ids, diagnostics); break;
    }
}

void RegisterModuleIds(const VfxEmitterData& emitter, std::unordered_set<std::string>& ids,
                       std::vector<VfxDiagnostic>& diagnostics) {
    const std::array<std::pair<std::string_view, std::string_view>, 12u> modules{
        std::pair{std::string_view(emitter.basic.id), "basic module"},
        std::pair{std::string_view(emitter.emitterTransform.id), "emitter transform module"},
        std::pair{std::string_view(emitter.attachment.id), "attachment module"},
        std::pair{std::string_view(emitter.spawn.id), "spawn module"},
        std::pair{std::string_view(emitter.shape.id), "shape module"},
        std::pair{std::string_view(emitter.position.id), "position module"},
        std::pair{std::string_view(emitter.rotation.id), "rotation module"},
        std::pair{std::string_view(emitter.scale.id), "scale module"},
        std::pair{std::string_view(emitter.forces.id), "forces module"},
        std::pair{std::string_view(emitter.color.id), "color module"},
        std::pair{std::string_view(emitter.renderCommon.id), "render common module"},
        std::pair{std::string_view(emitter.renderShape.id), "render shape module"},
    };
    for (const auto& [id, kind] : modules) {
        (void)RegisterId(id, kind, ids, diagnostics);
    }
}
void ValidateLayerKindAndEffects(const VfxEmitterData& emitter, std::unordered_set<std::string>& ids,
                                 std::vector<VfxDiagnostic>& diagnostics) {
    if (emitter.position.orientAlongPath && emitter.kind != VfxLayerKind::Shape && emitter.kind != VfxLayerKind::Group)
        AddError(diagnostics, "Vfx.PathOrientation", "Path orientation is supported by Shape and Group", emitter.id);
    if (emitter.effects.size() > kMaximumVfxEffectsPerLayer)
        AddError(diagnostics, "Vfx.TooManyEffects", "Layer exceeds the effect safety limit", emitter.id);
    for (const auto& effect : emitter.effects) {
        RegisterId(effect.id, "effect", ids, diagnostics);
        if (const auto error = ValidateMaterialEffect(effect); !error.empty())
            AddError(diagnostics, "Vfx.InvalidEffect", std::string(error), effect.id);
        if (emitter.kind == VfxLayerKind::Adjustment && !IsAdjustmentMaterialEffect(effect.kind))
            AddError(diagnostics, "Vfx.EffectDomain", "Effect is not supported by this layer type", effect.id);
        if (effect.kind == MaterialEffectKind::FlowMap) {
            if (effect.enabled && !emitter.renderCommon.flowMapTextureAssetGuid.IsValid())
                AddError(diagnostics, "Vfx.MissingFlowMap", "Assign an RG flow map in the layer texture slots",
                         effect.id);
            const auto overlaps = std::count_if(emitter.effects.begin(), emitter.effects.end(), [&](const auto& other) {
                return other.enabled && other.kind == MaterialEffectKind::FlowMap &&
                       (effect.textureTarget == MaterialEffectTextureTarget::Shared ||
                        other.textureTarget == MaterialEffectTextureTarget::Shared ||
                        other.textureTarget == effect.textureTarget);
            });
            if (effect.enabled && overlaps > 1)
                AddError(diagnostics, "Vfx.DuplicateFlowMap", "Use one Flow Map per texture target", effect.id);
        }
    }
    if (!emitter.effects.empty() && IsCustomVfxMaterial(emitter.renderCommon.materialMode))
        AddError(diagnostics, "Vfx.EffectMaterialConflict",
                 "Effect stacks use the layer texture; custom materials own their shader graph", emitter.id);
    if (!IsValidVfxHistory(emitter.history) ||
        (emitter.history.mode != VfxHistoryMode::None && emitter.kind != VfxLayerKind::Shape))
        AddError(diagnostics, "Vfx.InvalidHistory", "History requires a shape layer and bounded duration/sample count",
                 emitter.id);
    if (!TEXT::IsNamedEnum(kVfxLayerKindNames, emitter.kind)) {
        AddError(diagnostics, "Vfx.InvalidLayerKind", "unknown layer kind", emitter.id);
    }
}

void ValidateActivation(const VfxAssetData& asset, const VfxEmitterData& emitter,
                        std::vector<VfxDiagnostic>& diagnostics) {
    const VfxTransformData& transform = emitter.emitterTransform.transform;
    if (!IsFinite(transform.position) || !IsFinite(transform.rotationEulerDegrees) || !IsFinite(transform.scale) ||
        transform.scale.x == 0.0f || transform.scale.y == 0.0f || transform.scale.z == 0.0f) {
        AddError(diagnostics, "Vfx.InvalidEmitterTransform", "emitter transform must be finite with non-zero scale",
                 emitter.emitterTransform.id);
    }

    if (!IsFinite(emitter.startDelay) || !IsFinite(emitter.activeDuration) || emitter.startDelay < 0.0f ||
        emitter.startDelay > 3600.0f || emitter.activeDuration < 0.0f || emitter.activeDuration > 3600.0f) {
        AddError(diagnostics, "Vfx.InvalidEmitterActivation",
                 "emitter startDelay and activeDuration must be within "
                 "[0, 3600] seconds",
                 emitter.id);
    }
    if (IsFinite(emitter.startDelay) && IsFinite(emitter.activeDuration) &&
        (static_cast<double>(emitter.startDelay) > static_cast<double>(asset.durationSeconds) ||
         (emitter.activeDuration > 0.0f &&
          static_cast<double>(emitter.startDelay) + static_cast<double>(emitter.activeDuration) >
              static_cast<double>(asset.durationSeconds)))) {
        AddError(diagnostics, "Vfx.EmitterActivationOutsideAsset",
                 "emitter activation window must fit within asset duration", emitter.id);
    }
}

void ValidateAttachment(const VfxEmitterData& emitter, std::vector<VfxDiagnostic>& diagnostics) {
    if (!TEXT::IsNamedEnum(kVfxAttachmentModeNames, emitter.attachment.mode)) {
        AddError(diagnostics, "Vfx.InvalidAttachmentMode", "emitter attachment mode is invalid", emitter.attachment.id);
    }
    if (!TEXT::IsNamedEnum(kVfxFollowModeNames, emitter.attachment.follow)) {
        AddError(diagnostics, "Vfx.InvalidFollowMode", "emitter follow mode is invalid", emitter.attachment.id);
    }
    if (emitter.attachment.mode == VfxAttachmentMode::OwnerSocket && emitter.attachment.socketName.empty()) {
        AddError(diagnostics, "Vfx.MissingAttachmentSocket", "OwnerSocket attachment requires a socket name",
                 emitter.attachment.id);
    }
    if (emitter.attachment.socketName.size() > 256u || !IsFinite(emitter.attachment.offset.position) ||
        !IsFinite(emitter.attachment.offset.rotationEulerDegrees) || !IsFinite(emitter.attachment.offset.scale) ||
        emitter.attachment.offset.scale.x == 0.0f || emitter.attachment.offset.scale.y == 0.0f ||
        emitter.attachment.offset.scale.z == 0.0f) {
        AddError(diagnostics, "Vfx.InvalidAttachmentOffset", "attachment socket and local offset are invalid",
                 emitter.attachment.id);
    }
}

void ValidateSpawn(const VfxAssetData& asset, const VfxEmitterData& emitter, std::unordered_set<std::string>& ids,
                   std::vector<VfxDiagnostic>& diagnostics) {
    ValidateDistribution(emitter.basic.lifetime, emitter.basic.id, diagnostics);
    if (emitter.basic.lifetime.minimum <= 0.0f || emitter.basic.lifetime.maximum <= 0.0f ||
        emitter.basic.lifetime.maximum > 3600.0f) {
        AddError(diagnostics, "Vfx.InvalidLifetime", "particle lifetime must be within (0, 3600]", emitter.basic.id);
    }

    if (!IsFinite(emitter.spawn.ratePerSecond) || emitter.spawn.ratePerSecond < 0.0f ||
        emitter.spawn.ratePerSecond > 1000000.0f ||
        !TEXT::IsNamedEnum(kVfxParticleCapacityModeNames, emitter.spawn.capacityMode) ||
        emitter.spawn.maximumParticles == 0u ||
        emitter.spawn.maximumParticles > RENDER3D::PARTICLES::kGpuParticlePoolCapacity) {
        AddError(diagnostics, "Vfx.InvalidSpawnModule", "spawn rate, burst count, or particle capacity is invalid",
                 emitter.spawn.id);
    }
    if (emitter.spawn.burstTimeline.size() > kMaximumVfxProgramBurstKeys) {
        AddError(diagnostics, "Vfx.BurstTimelineLimit", "burst timeline exceeds the native VFX key budget",
                 emitter.spawn.id);
    }
    std::unordered_set<std::string> burstIds{};
    std::unordered_set<float> burstTimes{};
    for (const VfxBurstKeyData& key : emitter.spawn.burstTimeline) {
        (void)RegisterId(key.id, "burst key", ids, diagnostics);
        if (!burstIds.emplace(key.id).second) {
            AddError(diagnostics, "Vfx.DuplicateBurstKeyId", "burst key stable ID is duplicated", key.id);
        }
        if (!IsFinite(key.time) || key.time < 0.0f || key.time > 3600.0f || key.count == 0u) {
            AddError(diagnostics, "Vfx.InvalidBurstKey", "burst key time/count is invalid", key.id);
        }
        const double effectiveActiveDuration =
            emitter.activeDuration > 0.0f
                ? static_cast<double>(emitter.activeDuration)
                : (std::max)(0.0, static_cast<double>(asset.durationSeconds) - static_cast<double>(emitter.startDelay));
        if (IsFinite(key.time) && key.time >= 0.0f && static_cast<double>(key.time) > effectiveActiveDuration) {
            AddError(diagnostics, "Vfx.BurstOutsideActivation", "burst key time exceeds the emitter active window",
                     key.id);
        }
        if (!burstTimes.emplace(key.time).second) {
            AddError(diagnostics, "Vfx.DuplicateBurstTime", "burst keys may not share the same time", key.id);
        }
        if (key.count > emitter.spawn.maximumParticles) {
            AddError(diagnostics, "Vfx.BurstCapacityExceeded", "burst count exceeds the emitter particle capacity",
                     key.id);
        }
    }
}

void ValidateMotionModules(const VfxEmitterData& emitter, std::unordered_set<std::string>& ids,
                           std::vector<VfxDiagnostic>& diagnostics) {
    ValidateDistribution(emitter.shape.radius, emitter.shape.id, diagnostics);
    if (emitter.shape.radius.minimum < 0.0f || emitter.shape.radius.maximum < 0.0f ||
        !IsFinite(emitter.shape.coneAngleDegrees) || emitter.shape.coneAngleDegrees < 0.0f ||
        emitter.shape.coneAngleDegrees > 180.0f || !IsFinite(emitter.shape.boxHalfExtents) ||
        emitter.shape.boxHalfExtents.x < 0.0f || emitter.shape.boxHalfExtents.y < 0.0f ||
        emitter.shape.boxHalfExtents.z < 0.0f || !IsFinite(emitter.shape.cylinderHeight) ||
        emitter.shape.cylinderHeight < 0.0f || !IsFinite(emitter.shape.circleInnerRadius) ||
        emitter.shape.circleInnerRadius < 0.0f || !IsFinite(emitter.shape.circleArcDegrees) ||
        emitter.shape.circleArcDegrees <= 0.0f || emitter.shape.circleArcDegrees > 360.0f ||
        !IsFinite(emitter.shape.edgeLength) || emitter.shape.edgeLength < 0.0f ||
        (emitter.shape.type == VfxShapeType::Circle &&
         emitter.shape.circleInnerRadius > emitter.shape.radius.minimum)) {
        AddError(diagnostics, "Vfx.InvalidShapeModule", "shape radius or cone angle is invalid", emitter.shape.id);
    }

    ValidateDistribution(emitter.position.radialSpeed, emitter.position.id, diagnostics);
    ValidateAnimatedVec3(emitter.position.value, emitter.position.id, ids, diagnostics,
                         emitter.kind == VfxLayerKind::Particles ? RENDER3D::PARTICLES::kMaxParticleCurveKeys
                                                                 : kMaxVfxLayerCurveKeys);
    ValidateAnimatedVec3(emitter.rotation.eulerDegrees, emitter.rotation.id, ids, diagnostics,
                         emitter.kind == VfxLayerKind::Particles ? RENDER3D::PARTICLES::kMaxParticleCurveKeys
                                                                 : kMaxVfxLayerCurveKeys);
    ValidateAnimatedVec3(emitter.scale.value, emitter.scale.id, ids, diagnostics,
                         emitter.kind == VfxLayerKind::Particles ? RENDER3D::PARTICLES::kMaxParticleCurveKeys
                                                                 : kMaxVfxLayerCurveKeys);
    if (!RENDER3D::PARTICLES::IsValidParticleForceField(emitter.forces.field) || !IsFinite(emitter.forces.gravity) ||
        !IsFinite(emitter.forces.drag) || emitter.forces.drag < 0.0f || emitter.forces.drag > 1000.0f ||
        !IsFinite(emitter.forces.collisionPlaneHeight) || !IsFinite(emitter.forces.restitution) ||
        emitter.forces.restitution < 0.0f || emitter.forces.restitution > 1.0f || !IsFinite(emitter.forces.friction) ||
        emitter.forces.friction < 0.0f || emitter.forces.friction > 1.0f || !IsFinite(emitter.forces.fluidFollow) ||
        emitter.forces.fluidFollow < 0.0f ||
        emitter.forces.fluidFollow > RENDER3D::PARTICLES::kMaxParticleFluidFollow) {
        AddError(diagnostics, "Vfx.InvalidForcesModule", "particle gravity or drag is invalid", emitter.forces.id);
    }
    ValidateAnimatedColor(emitter.color.value, emitter.color.id, ids, diagnostics,
                          emitter.kind == VfxLayerKind::Particles ? RENDER3D::PARTICLES::kMaxParticleCurveKeys
                                                                  : kMaxVfxLayerCurveKeys);
}

void ValidateRenderMaterial(const VfxEmitterData& emitter, std::vector<VfxDiagnostic>& diagnostics) {
    if (!TEXT::IsNamedEnum(kVfxPostProcessModeNames, emitter.renderCommon.postProcessMode))
        AddError(diagnostics, "Vfx.InvalidPostProcessMode", "Unknown post-process mode", emitter.renderCommon.id);
    if (emitter.meshOutput.enabled && emitter.renderCommon.postProcessMode != VfxPostProcessMode::Scene)
        AddError(diagnostics, "Vfx.PreserveColorMeshOutput",
                 "PreserveColor requires procedural shape or particle output", emitter.renderCommon.id);
    if (!TEXT::IsNamedEnum(kVfxMaterialModeNames, emitter.renderCommon.materialMode)) {
        AddError(diagnostics, "Vfx.InvalidMaterialMode", "render material mode is invalid", emitter.renderCommon.id);
    }
    const bool validDepthMode = TEXT::IsNamedEnum(kVfxParticleDepthModeNames, emitter.renderCommon.depthMode);
    const bool validSoftIntersection = IsFinite(emitter.renderCommon.softIntersectionDistance) &&
                                       (emitter.renderCommon.depthMode != VfxParticleDepthMode::SoftIntersection ||
                                        (emitter.renderCommon.softIntersectionDistance >= 0.001f &&
                                         emitter.renderCommon.softIntersectionDistance <= 10.0f));
    const bool validContactBias = IsFinite(emitter.renderCommon.depthBias) &&
                                  (emitter.renderCommon.depthMode != VfxParticleDepthMode::ContactSurface ||
                                   (emitter.renderCommon.depthBias >= 0.0f && emitter.renderCommon.depthBias <= 0.02f));
    if (!validDepthMode || !validSoftIntersection || !validContactBias) {
        AddError(diagnostics, "Vfx.InvalidParticlePresentation", "particle depth parameters are invalid",
                 emitter.renderCommon.id);
    }
    if (emitter.renderCommon.colorTextureAssetGuid.IsValid() &&
        !IsVfxBuiltinTextureAssetId(emitter.renderCommon.colorTextureAssetGuid.value) &&
        !IsValidAssetGuid(emitter.renderCommon.colorTextureAssetGuid.value)) {
        AddError(diagnostics, "Vfx.InvalidColorTextureGuid", "render color texture GUID is invalid",
                 emitter.renderCommon.id);
    }
    if (emitter.renderCommon.materialAssetGuid.IsValid() &&
        !IsValidAssetGuid(emitter.renderCommon.materialAssetGuid.value)) {
        AddError(diagnostics, "Vfx.InvalidMaterialGuid", "render material GUID is invalid", emitter.renderCommon.id);
    }
}

void ValidateParameterTracks(const VfxEmitterData& emitter, std::unordered_set<std::string>& ids,
                             std::vector<VfxDiagnostic>& diagnostics) {
    if (!emitter.animationTiming.IsValid())
        AddError(diagnostics, "Vfx.InvalidAnimationTiming", "Invalid animation time mapping", emitter.id);
    const auto animatable = GetVfxAnimatableParameters(emitter);
    std::unordered_set<std::string> targets;
    if (emitter.parameterTracks.size() > kMaxVfxParameterTracks)
        AddError(diagnostics, "Vfx.TrackLimit", "Too many layer parameter tracks", emitter.id);
    for (const auto& track : emitter.parameterTracks) {
        RegisterId(track.id, "parameter track", ids, diagnostics);
        ValidateCurve(track.curve, track.id, ids, diagnostics, kMaxVfxLayerCurveKeys);
        const auto parameter = std::find_if(animatable.begin(), animatable.end(), [&](const auto& p) {
            return p.targetId == track.targetId && p.propertyId == track.propertyId;
        });
        if (parameter == animatable.end() || !targets.emplace(track.targetId + "/" + track.propertyId).second ||
            track.curve.randomOffset.minimum != 0 || track.curve.randomOffset.maximum != 0) {
            AddError(diagnostics, "Vfx.InvalidParameterTrack", "Unknown, duplicate or randomized layer parameter",
                     track.id);
            continue;
        }
        for (const auto& key : track.curve.keys)
            if (key.value < parameter->minimum || key.value > parameter->maximum)
                AddError(diagnostics, "Vfx.ParameterRange", "Animated value is outside the parameter range", key.id);
    }
}

void ValidateRenderShape(const VfxEmitterData& emitter, std::vector<VfxDiagnostic>& diagnostics) {
    const uint64_t flipbookCellCount = static_cast<uint64_t>(emitter.renderCommon.flipbookColumns) *
                                       static_cast<uint64_t>(emitter.renderCommon.flipbookRows);
    if (emitter.renderCommon.flipbookColumns == 0u || emitter.renderCommon.flipbookColumns > 64u ||
        emitter.renderCommon.flipbookRows == 0u || emitter.renderCommon.flipbookRows > 64u ||
        emitter.renderCommon.flipbookFrameCount == 0u || emitter.renderCommon.flipbookFrameCount > flipbookCellCount ||
        !IsFinite(emitter.renderCommon.flipbookFramesPerSecond) ||
        emitter.renderCommon.flipbookFramesPerSecond < 0.0f || emitter.renderCommon.flipbookFramesPerSecond > 240.0f) {
        AddError(diagnostics, "Vfx.InvalidFlipbook", "flipbook layout or playback rate is invalid",
                 emitter.renderCommon.id);
    }
    if (!TEXT::IsNamedEnum(kVfxBillboardModeNames, emitter.renderShape.billboardMode)) {
        AddError(diagnostics, "Vfx.InvalidBillboardMode", "sprite orientation mode is invalid", emitter.renderShape.id);
    }
    if (!TEXT::IsNamedEnum(kVfxRenderShapeTypeNames, emitter.renderShape.type) ||
        !IsFinite(emitter.renderShape.ribbonWidth) || emitter.renderShape.ribbonWidth < 0.0f ||
        !IsFinite(emitter.renderShape.ringOuterRadius) || !IsFinite(emitter.renderShape.ringInnerRadius) ||
        emitter.renderShape.ringInnerRadius < 0.0f ||
        emitter.renderShape.ringOuterRadius < emitter.renderShape.ringInnerRadius ||
        !IsFinite(emitter.renderShape.ringStartAngleDegrees) || !IsFinite(emitter.renderShape.ringEndAngleDegrees)) {
        AddError(
            diagnostics, "Vfx.InvalidRenderGeometry",
            "Geometry requires a known shape, finite angles, non-negative width, and outer radius >= inner radius >= 0",
            emitter.renderShape.id);
    }
    if (emitter.renderShape.ribbonSegmentCount < 2u || emitter.renderShape.ribbonSegmentCount > 64u ||
        emitter.renderShape.ringSegmentCount < 3u || emitter.renderShape.ringSegmentCount > 64u) {
        AddError(diagnostics, "Vfx.InvalidRenderShapeSegments",
                 "ribbon/ring segment count exceeds the mesh shader "
                 "contract (ribbon 2..64, ring 3..64)",
                 emitter.renderShape.id);
    }
    if (!TEXT::IsNamedEnum(kVfxShapeUvModeNames, emitter.renderShape.uvMode) ||
        !IsFinite(emitter.renderShape.uvTileLength) ||
        (emitter.renderShape.uvMode == VfxShapeUvMode::WorldLength && emitter.renderShape.uvTileLength <= 0.0f) ||
        emitter.renderShape.uvTileLength < -64.0f || emitter.renderShape.uvTileLength > 64.0f ||
        !IsFinite(emitter.renderShape.uvTilingU) || !IsFinite(emitter.renderShape.uvTilingV) ||
        emitter.renderShape.uvTilingU <= 0.0f || emitter.renderShape.uvTilingV <= 0.0f ||
        emitter.renderShape.uvTilingU > 64.0f || emitter.renderShape.uvTilingV > 64.0f ||
        !IsFinite(emitter.renderShape.uvRotationDegrees)) {
        AddError(diagnostics, "Vfx.InvalidShapeUvContract",
                 "UV contract requires a known mode, a positive V tile "
                 "count, tile extent within 64, and finite rotation",
                 emitter.renderShape.id);
    }
    if (!IsFinite(emitter.anchor)) {
        AddError(diagnostics, "Vfx.InvalidAnchor", "layer anchor must be finite", emitter.id);
    }
}

void ValidateConnectAndMeshOutput(const VfxAssetData& asset, const VfxEmitterData& emitter,
                                  std::vector<VfxDiagnostic>& diagnostics) {
    // 接続ターゲット: 範囲と、参照先レイヤーの存在を検証する。
    const VfxConnectTargetData& connect = emitter.connectTarget;
    if (connect.mode != VfxConnectTargetMode::None) {
        if (!IsFinite(connect.localPosition.x) || !IsFinite(connect.localPosition.y) ||
            !IsFinite(connect.localPosition.z) || !IsFinite(connect.pivot) || connect.pivot < 0.0f ||
            connect.pivot > 1.0f || !IsFinite(connect.lengthScale) || connect.lengthScale < 0.0f ||
            !IsFinite(connect.startOffset) || connect.startOffset < 0.0f || !IsFinite(connect.endOffset) ||
            connect.endOffset < 0.0f) {
            AddError(diagnostics, "Vfx.InvalidConnectTarget", "connect target values are invalid", emitter.id);
        }
        if (connect.mode == VfxConnectTargetMode::LayerOrigin) {
            const bool targetExists =
                std::any_of(asset.emitters.begin(), asset.emitters.end(), [&connect](const VfxEmitterData& candidate) {
                    return candidate.id == connect.targetLayerId;
                });
            if (!targetExists) {
                AddError(diagnostics, "Vfx.MissingConnectTarget", "connect target layer does not exist", emitter.id);
            }
        }
    }

    // Mesh 出力は有効ならモデル素材が必須。参照が欠けたまま
    // 実行すると、そのレイヤーは何も描かないまま消える。
    if (emitter.meshOutput.enabled && !emitter.meshOutput.meshAssetGuid.IsValid()) {
        AddError(diagnostics, "Vfx.MissingMeshAsset", "mesh output requires a model asset", emitter.id);
    }
    if (emitter.meshOutput.enabled) {
        if (emitter.kind != VfxLayerKind::Shape || !emitter.effects.empty() ||
            emitter.history.mode != VfxHistoryMode::None) {
            AddError(
                diagnostics, "Vfx.UnsupportedMeshLayer",
                "Model output requires a shape layer and uses model/custom materials; particle effect stacks and history are not supported",
                emitter.id);
        }
        diagnostics.push_back(
            {VfxDiagnosticSeverity::Warning, "Vfx.MeshMaterialOwnership",
             "Model output supports layer transforms and model/custom materials. Layer tint, opacity and particle material curves are not applied to model surfaces.",
             emitter.id});
    }
}

void ValidateSpecialOutputs(const VfxEmitterData& emitter, std::unordered_set<std::string>& ids,
                            std::vector<VfxDiagnostic>& diagnostics) {
    if (emitter.specialOutputs.size() > kMaximumVfxProgramSpecialOutputs) {
        AddError(diagnostics, "Vfx.SpecialOutputLimit", "special output count exceeds the native VFX budget",
                 emitter.id);
    }
    std::unordered_set<std::string> specialOutputIds{};
    for (const VfxSpecialOutputData& output : emitter.specialOutputs) {
        (void)RegisterId(output.id, "special output", ids, diagnostics);
        if (!specialOutputIds.emplace(output.id).second) {
            AddError(diagnostics, "Vfx.DuplicateSpecialOutputId", "special output stable ID is duplicated", output.id);
        }
        if (!TEXT::IsNamedEnum(kVfxSpecialOutputTypeNames, output.type)) {
            AddError(diagnostics, "Vfx.InvalidSpecialOutputType", "special output type is invalid", output.id);
        }
        if (!IsValidVfxSpecialOutputProfile(output)) {
            const bool light = output.type == VfxSpecialOutputType::Light;
            AddError(diagnostics, light ? "Vfx.InvalidLightOutput" : "Vfx.InvalidSpecialOutputProfile",
                     light ? "light output profile is invalid" : "special output profile contains invalid values",
                     output.id);
        }
    }
}

void ValidateInputLinks(const VfxEmitterData& emitter, const std::unordered_map<std::string, VfxValueType>& inputTypes,
                        std::unordered_set<std::string>& ids, std::vector<VfxDiagnostic>& diagnostics) {
    std::unordered_set<std::string> inputLinkTargets{};
    for (const VfxInputLinkData& link : emitter.inputLinks) {
        (void)RegisterId(link.id, "input link", ids, diagnostics);
        const std::string targetKey =
            link.target.emitterId + "\n" + link.target.moduleId + "\n" + link.target.propertyId;
        if (!inputLinkTargets.emplace(targetKey).second) {
            AddError(diagnostics, "Vfx.DuplicateInputTarget", "only one input link may bind a runtime property",
                     link.id);
        }
        const auto input = inputTypes.find(link.inputId);
        if (input == inputTypes.end()) {
            AddError(diagnostics, "Vfx.MissingInput", "input link references an unknown exposed input", link.id);
        }
        const VfxPropertySchema* schema = ResolveVfxPropertySchema(emitter, link.target);
        if (schema == nullptr) {
            AddError(diagnostics, "Vfx.UnknownPropertyAddress", "input link target does not resolve to a VFX property",
                     link.id);
        } else if (!schema->bindable || !schema->runtimeMutable) {
            AddError(diagnostics, "Vfx.PropertyNotBindable", "input link target is not runtime bindable", link.id);
        } else if (input != inputTypes.end() && !IsVfxInputTypeCompatible(input->second, *schema)) {
            AddError(diagnostics, "Vfx.InputTypeMismatch", "input type is incompatible with the target property",
                     link.id);
        }
        if (!IsFinite(link.scale) || !IsFinite(link.bias)) {
            AddError(diagnostics, "Vfx.InvalidInputLink", "input link scale and bias must be finite", link.id);
        }
    }
}

void ValidateComposition(const VfxAssetData& asset,
                         const std::unordered_map<std::string, const VfxEmitterData*>& emitters,
                         std::vector<VfxDiagnostic>& diagnostics) {
    const bool hasComposition = std::any_of(asset.emitters.begin(), asset.emitters.end(), [](const auto& layer) {
        return layer.kind == VfxLayerKind::Adjustment || !layer.matte.sourceLayerId.empty() || layer.matte.sourceOnly;
    });
    const bool requiresScreenComposition =
        std::any_of(asset.emitters.begin(), asset.emitters.end(), [](const auto& layer) {
            return !layer.matte.sourceLayerId.empty() || layer.matte.sourceOnly ||
                   std::any_of(layer.effects.begin(), layer.effects.end(), [](const auto& effect) {
                       return effect.enabled && effect.space == MaterialEffectSpace::LayerUv;
                   });
        });
    const VfxEmitterData* firstContent = nullptr;
    for (const VfxEmitterData& emitter : asset.emitters) {
        if (requiresScreenComposition &&
            (emitter.kind == VfxLayerKind::Particles || emitter.kind == VfxLayerKind::Shape)) {
            if (firstContent && firstContent->renderCommon.postProcessMode != emitter.renderCommon.postProcessMode)
                AddError(
                    diagnostics, "Vfx.MixedCompositionPostProcessMode",
                    "Track matte and LayerUv compositions require the same post-process mode on all content layers",
                    emitter.renderCommon.id);
            firstContent = &emitter;
        }
        if (!emitter.matte.sourceLayerId.empty()) {
            const auto source = emitters.find(emitter.matte.sourceLayerId);
            if (source == emitters.end() || source->second == &emitter ||
                source->second->parentEmitterId != emitter.parentEmitterId ||
                source->second->kind == VfxLayerKind::Adjustment)
                AddError(diagnostics, "Vfx.InvalidMatte",
                         "Matte requires another content/group layer in the same group", emitter.id);
            std::unordered_set<std::string> visited{emitter.id};
            auto current = &emitter;
            while (!current->matte.sourceLayerId.empty()) {
                if (!visited.insert(current->matte.sourceLayerId).second) {
                    AddError(diagnostics, "Vfx.MatteCycle", "Track matte dependency contains a cycle", emitter.id);
                    break;
                }
                const auto next = emitters.find(current->matte.sourceLayerId);
                if (next == emitters.end())
                    break;
                current = next->second;
            }
        }
        if (emitter.kind == VfxLayerKind::Adjustment && !emitter.matte.sourceLayerId.empty())
            AddError(diagnostics, "Vfx.AdjustmentMatte",
                     "Use local effect masks on adjustment layers; track mattes belong on content or groups",
                     emitter.id);
        if (emitter.kind == VfxLayerKind::Adjustment && emitter.matte.sourceOnly)
            AddError(diagnostics, "Vfx.InvalidMatteSource", "Adjustment layers cannot be matte-only sources",
                     emitter.id);
        if (hasComposition && emitter.meshOutput.enabled)
            AddError(diagnostics, "Vfx.CompositionMeshOutput",
                     "Adjustment compositions currently require procedural shape or particle layers", emitter.id);
        if (hasComposition) {
            const VfxEmitterData* parent = &emitter;
            size_t depth = 0;
            while (!parent->parentEmitterId.empty() && depth < 8) {
                const auto found = emitters.find(parent->parentEmitterId);
                if (found == emitters.end())
                    break;
                parent = found->second;
                ++depth;
                if (parent->kind == VfxLayerKind::Adjustment)
                    AddError(diagnostics, "Vfx.AdjustmentParent", "Adjustment layers cannot contain child layers",
                             emitter.id);
            }
            if (depth >= 8)
                AddError(diagnostics, "Vfx.CompositionDepth",
                         "Adjustment compositions support at most eight nested scopes", emitter.id);
        }
        if (!emitter.parentEmitterId.empty() && !emitters.contains(emitter.parentEmitterId)) {
            AddError(diagnostics, "Vfx.MissingParentEmitter", "emitter parent does not exist", emitter.id);
        }
    }
}

void ValidateHierarchyCycles(const VfxAssetData& asset,
                             const std::unordered_map<std::string, const VfxEmitterData*>& emitters,
                             std::vector<VfxDiagnostic>& diagnostics) {
    enum class Visit : uint8_t { None, Active, Complete };
    std::unordered_map<std::string, Visit> visits{};
    std::function<void(const VfxEmitterData&)> visit = [&](const VfxEmitterData& emitter) {
        Visit& state = visits[emitter.id];
        if (state == Visit::Complete) {
            return;
        }
        if (state == Visit::Active) {
            AddError(diagnostics, "Vfx.EmitterCycle", "emitter hierarchy contains a cycle", emitter.id);
            return;
        }
        state = Visit::Active;
        const auto parent = emitters.find(emitter.parentEmitterId);
        if (parent != emitters.end()) {
            visit(*parent->second);
        }
        state = Visit::Complete;
    };
    for (const VfxEmitterData& emitter : asset.emitters) {
        visit(emitter);
    }
}

// 流体層：域は他の域の中に置かない、源は域の下（群は挟んでよい）、数は GPU の表の上限まで、値は字段表の範囲。
// 流体層は効果・メッシュ出力・特殊出力・履歴・マットを使わない（描画は体積の步进が受け持つ）。
void ValidateFluidLayers(const VfxAssetData& asset,
                         const std::unordered_map<std::string, const VfxEmitterData*>& emitters,
                         std::unordered_set<std::string>& ids, std::vector<VfxDiagnostic>& diagnostics) {
    namespace FLUID = RENDER3D::FLUID;
    const auto find = [&emitters](const std::string& id) -> const VfxEmitterData* {
        const auto found = emitters.find(id);
        return found == emitters.end() ? nullptr : found->second;
    };
    std::unordered_map<const VfxEmitterData*, uint32_t> sourcesPerDomain{};
    uint32_t domains = 0u;
    for (const VfxEmitterData& emitter : asset.emitters) {
        if (emitter.kind != VfxLayerKind::Fluid && emitter.kind != VfxLayerKind::FluidSource)
            continue;
        if (!emitter.effects.empty() || emitter.meshOutput.enabled || !emitter.specialOutputs.empty() ||
            emitter.history.mode != VfxHistoryMode::None || !emitter.matte.sourceLayerId.empty() ||
            emitter.matte.sourceOnly)
            AddError(diagnostics, "Vfx.FluidLayerFeature",
                     "Fluid layers take no effects, mesh output, special outputs, history or track mattes", emitter.id);
        const VfxEmitterData* domain = FindVfxFluidDomainLayer(emitter, find);
        if (emitter.kind == VfxLayerKind::Fluid) {
            ++domains;
            (void)RegisterId(emitter.fluidDomain.id, "fluid domain module", ids, diagnostics);
            const FLUID::FluidDomainSettings& settings = emitter.fluidDomain;
            if (JSONFIELDS::Sanitize(settings, FLUID::kFluidDomainSettingsFields) != settings)
                AddError(diagnostics, "Vfx.FluidDomainRange", "Fluid domain values are outside their ranges",
                         emitter.fluidDomain.id);
            if (domain != nullptr)
                AddError(diagnostics, "Vfx.NestedFluidDomain", "A fluid domain cannot be placed inside another",
                         emitter.id);
            continue;
        }
        (void)RegisterId(emitter.fluidSource.id, "fluid source module", ids, diagnostics);
        const FLUID::FluidSourceSettings& settings = emitter.fluidSource;
        if (JSONFIELDS::Sanitize(settings, FLUID::kFluidSourceSettingsFields) != settings)
            AddError(diagnostics, "Vfx.FluidSourceRange", "Fluid source values are outside their ranges",
                     emitter.fluidSource.id);
        // 域の外の流体源（流体注入层）は場面の域へ注入するので数えない。域自身の力場が表の一つを使う。
        if (domain != nullptr && ++sourcesPerDomain[domain] >= FLUID::kMaximumFluidSourcesPerDomain)
            AddError(diagnostics, "Vfx.FluidSourceBudget", "Fluid domain has too many sources", emitter.id);
    }
    if (domains > FLUID::kMaximumFluidDomainsPerAsset)
        AddError(diagnostics, "Vfx.FluidDomainBudget", "VFX asset has too many fluid domains");
}

} // namespace

std::vector<VfxDiagnostic> ValidateVfxAsset(const VfxAssetData& asset) {
    std::vector<VfxDiagnostic> diagnostics{};
    if (asset.version != kVfxSourceVersion) {
        AddError(diagnostics, "Vfx.UnsupportedVersion",
                 "unsupported VFX source version " + std::to_string(asset.version));
    }
    if (asset.name.empty()) {
        AddError(diagnostics, "Vfx.MissingName", "VFX asset name is empty");
    }
    if (!IsFinite(asset.durationSeconds) || asset.durationSeconds <= 0.0f || asset.durationSeconds > 3600.0f) {
        AddError(diagnostics, "Vfx.InvalidDuration", "durationSeconds must be within (0, 3600]");
    }
    if (asset.emitters.empty()) {
        AddError(diagnostics, "Vfx.MissingEmitter", "VFX asset requires at least one emitter");
    }
    if (asset.emitters.size() > kMaximumVfxProgramEmitters) {
        AddError(diagnostics, "Vfx.EmitterBudgetExceeded", "emitter count exceeds the native VFX program budget");
    }

    std::unordered_set<std::string> ids{};
    std::unordered_map<std::string, VfxValueType> inputTypes{};
    for (const VfxExposedInputData& input : asset.inputs) {
        (void)RegisterId(input.id, "exposed input", ids, diagnostics);
        if (input.name.empty()) {
            AddError(diagnostics, "Vfx.MissingInputName", "exposed input name is empty", input.id);
        }
        if (input.defaultValue.type != input.type || !input.defaultValue.IsFinite()) {
            AddError(diagnostics, "Vfx.InvalidInputDefault", "exposed input default value does not match its type",
                     input.id);
        }
        inputTypes.emplace(input.id, input.type);
    }

    std::unordered_map<std::string, const VfxEmitterData*> emitters{};
    for (const VfxEmitterData& emitter : asset.emitters) {
        ValidateLayerKindAndEffects(emitter, ids, diagnostics);
        (void)RegisterId(emitter.id, "emitter", ids, diagnostics);
        emitters.emplace(emitter.id, &emitter);
        if (emitter.name.empty()) {
            AddError(diagnostics, "Vfx.MissingEmitterName", "emitter name is empty", emitter.id);
        }
        RegisterModuleIds(emitter, ids, diagnostics);

        ValidateActivation(asset, emitter, diagnostics);
        ValidateAttachment(emitter, diagnostics);
        ValidateSpawn(asset, emitter, ids, diagnostics);
        ValidateMotionModules(emitter, ids, diagnostics);
        ValidateRenderMaterial(emitter, diagnostics);
        ValidateParameterTracks(emitter, ids, diagnostics);
        ValidateRenderShape(emitter, diagnostics);
        ValidateConnectAndMeshOutput(asset, emitter, diagnostics);
        ValidateSpecialOutputs(emitter, ids, diagnostics);
        ValidateInputLinks(emitter, inputTypes, ids, diagnostics);
    }

    // maximumParticles は自動割当ての上限であり予約量ではない。
    // Shape/Group の実容量も異なるため、合計はコンパイル後の
    // ValidateVfxProgram が実際の simulation.capacity で判定する。

    ValidateComposition(asset, emitters, diagnostics);
    ValidateHierarchyCycles(asset, emitters, diagnostics);
    ValidateFluidLayers(asset, emitters, ids, diagnostics);
    return diagnostics;
}

bool HasVfxErrors(const std::vector<VfxDiagnostic>& diagnostics) noexcept {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const VfxDiagnostic& diagnostic) {
        return diagnostic.severity == VfxDiagnosticSeverity::Error;
    });
}

} // namespace HIKARI::VFX
