#include "Assets/Vfx/HIKARI_VfxBudgets.h"
#include "Render3D/Vfx/HIKARI_ParticleCapacity.h"
#include "Render3D/Material/Program/HIKARI_MaterialExpressionProgram.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Assets/Vfx/HIKARI_VfxProgram.h"

#include "Assets/Vfx/HIKARI_VfxBuiltinTextureCatalog.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace HIKARI::VFX {
    namespace {
        using namespace RENDER3D::PARTICLES;

        bool IsFinite(float value) noexcept {
            return std::isfinite(value);
        }

        bool IsFinite(const MATH::Vec3& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y) &&
                IsFinite(value.z);
        }

        bool IsFinite(const MATH::Vec4& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y) &&
                IsFinite(value.z) && IsFinite(value.w);
        }

        bool IsNonNegative(const MATH::Vec4& value) noexcept {
            return value.x >= 0.0f && value.y >= 0.0f &&
                value.z >= 0.0f && value.w >= 0.0f;
        }

        bool IsValidDistribution(
            const ParticleDistributionFloat& value) noexcept {

            return IsFinite(value.minimum) && IsFinite(value.maximum) &&
                (value.mode == ParticleDistributionMode::Constant ||
                 (value.mode == ParticleDistributionMode::RandomRange &&
                  value.minimum <= value.maximum));
        }

        bool IsValidDistribution(
            const ParticleDistributionFloat3& value) noexcept {

            return IsFinite(value.minimum) && IsFinite(value.maximum) &&
                (value.mode == ParticleDistributionMode::Constant ||
                 (value.mode == ParticleDistributionMode::RandomRange &&
                  value.minimum.x <= value.maximum.x &&
                  value.minimum.y <= value.maximum.y &&
                  value.minimum.z <= value.maximum.z));
        }

        bool IsValidDistribution(
            const ParticleDistributionFloat4& value) noexcept {

            return IsFinite(value.minimum) && IsFinite(value.maximum) &&
                (value.mode == ParticleDistributionMode::Constant ||
                 (value.mode == ParticleDistributionMode::RandomRange &&
                  value.minimum.x <= value.maximum.x &&
                  value.minimum.y <= value.maximum.y &&
                  value.minimum.z <= value.maximum.z &&
                  value.minimum.w <= value.maximum.w));
        }

        bool IsValidCurve(const ParticleCurve& curve) noexcept {
            if (curve.keyCount == 0u ||
                curve.keyCount > kMaxParticleCurveKeys ||
                (curve.interpolation !=
                    ParticleCurveInterpolation::Linear &&
                 curve.interpolation !=
                    ParticleCurveInterpolation::Smooth &&
                 curve.interpolation !=
                    ParticleCurveInterpolation::Step) ||
                !IsValidDistribution(curve.randomOffset)) {
                return false;
            }
            float previousTime = -1.0f;
            for (uint32_t index = 0u; index < curve.keyCount; ++index) {
                const ParticleCurveKey& key = curve.keys[index];
                if (!IsFinite(key.time) || !IsFinite(key.value) ||
                    !IsFinite(key.inTangent) ||
                    !IsFinite(key.outTangent) || key.time < 0.0f ||
                    key.time > 1.0f || key.time <= previousTime) {
                    return false;
                }
                previousTime = key.time;
            }
            return true;
        }

        bool IsValidEasing(ParticleEasingFunction value) noexcept {
            return value == ParticleEasingFunction::Linear ||
                value == ParticleEasingFunction::EaseIn ||
                value == ParticleEasingFunction::EaseOut ||
                value == ParticleEasingFunction::EaseInOut;
        }

        bool IsValidAnimated(
            const ParticleAnimatedFloat3& value) noexcept {

            switch (value.mode) {
            case ParticleValueMode::Fixed:
                return IsValidDistribution(value.initial);
            case ParticleValueMode::Pva:
                return IsValidDistribution(value.initial) &&
                    IsValidDistribution(value.velocity) &&
                    IsValidDistribution(value.acceleration);
            case ParticleValueMode::Easing:
                return IsValidDistribution(value.easingStart) &&
                    (!value.hasEasingMiddle ||
                     IsValidDistribution(value.easingMiddle)) &&
                    IsValidDistribution(value.easingEnd) &&
                    IsValidEasing(value.easingFunction) &&
                    value.hasEasingMiddle <= 1u;
            case ParticleValueMode::Curve:
                return std::all_of(
                    value.curves.begin(),
                    value.curves.end(),
                    IsValidCurve);
            default:
                return false;
            }
        }

        bool IsValidAnimated(
            const ParticleAnimatedColor& value) noexcept {

            switch (value.mode) {
            case ParticleColorMode::Fixed:
            case ParticleColorMode::RandomRange:
                return IsValidDistribution(value.fixed);
            case ParticleColorMode::Easing:
                return IsValidDistribution(value.easingStart) &&
                    (!value.hasEasingMiddle ||
                     IsValidDistribution(value.easingMiddle)) &&
                    IsValidDistribution(value.easingEnd) &&
                    IsValidEasing(value.easingFunction) &&
                    value.hasEasingMiddle <= 1u;
            case ParticleColorMode::Curve:
                return std::all_of(
                    value.curves.begin(),
                    value.curves.end(),
                    IsValidCurve);
            case ParticleColorMode::Gradient:
                if (value.gradient.stopCount == 0u ||
                    value.gradient.stopCount > kMaxParticleColorStops) {
                    return false;
                }
                for (uint32_t index = 0u;
                     index < value.gradient.stopCount;
                     ++index) {
                    const ParticleColorStop& stop =
                        value.gradient.stops[index];
                    if (!IsFinite(stop.time) || !IsFinite(stop.color) ||
                        stop.time < 0.0f || stop.time > 1.0f ||
                        (index > 0u && stop.time <=
                            value.gradient.stops[index - 1u].time)) {
                        return false;
                    }
                }
                return true;
            default:
                return false;
            }
        }

        bool IsValidValue(const VfxValue& value) noexcept {
            switch (value.type) {
            case VfxValueType::Bool:
            case VfxValueType::Int:
                return true;
            case VfxValueType::Float:
            case VfxValueType::Float2:
            case VfxValueType::Float3:
            case VfxValueType::Float4:
            case VfxValueType::Color:
                return IsFinite(value.numeric);
            default:
                return false;
            }
        }

        VfxValueType ExpectedInputType(
            VfxRuntimeBindingSemantic semantic) noexcept {

            switch (semantic) {
            case VfxRuntimeBindingSemantic::EmitterEnabled:
                return VfxValueType::Bool;
            case VfxRuntimeBindingSemantic::ColorMultiplier:
                return VfxValueType::Color;
            case VfxRuntimeBindingSemantic::SpawnRateMultiplier:
            case VfxRuntimeBindingSemantic::RadialSpeedMultiplier:
            case VfxRuntimeBindingSemantic::ScaleMultiplier:
            case VfxRuntimeBindingSemantic::GravityMultiplier:
            default:
                return VfxValueType::Float;
            }
        }

        uint32_t StorageSize(ParticleRuntimePropertyType type) noexcept {
            switch (type) {
            case ParticleRuntimePropertyType::Bool:
            case ParticleRuntimePropertyType::Float:
                return sizeof(uint32_t);
            case ParticleRuntimePropertyType::Float4:
                return sizeof(MATH::Vec4);
            default:
                return 0u;
            }
        }
    }

    bool ValidateVfxProgram(
        const VfxProgram& program,
        std::string& outError) {

        if (program.version != kVfxProgramVersion ||
            program.sourceVersion != kVfxSourceVersion ||
            program.sourceHash == 0u || program.programId.empty() ||
            program.programId.size() > kMaximumVfxProgramStringBytes ||
            program.name.empty() ||
            program.name.size() > kMaximumVfxProgramStringBytes ||
            program.emitters.empty() ||
            !IsFinite(program.durationSeconds) ||
            program.durationSeconds <= 0.0f ||
            !IsFinite(program.maximumParticleLifetimeSeconds) ||
            program.maximumParticleLifetimeSeconds < 0.0f) {
            outError = "VFX program header fields are invalid";
            return false;
        }
        if (program.inputs.size() > kMaximumVfxProgramInputs ||
            program.emitters.size() > kMaximumVfxProgramEmitters ||
            program.dependencyGuids.size() >
                kMaximumVfxProgramDependencies) {
            outError = "VFX program table exceeds the runtime limit";
            return false;
        }

        std::unordered_set<std::string> inputIds{};
        for (const VfxProgramInput& input : program.inputs) {
            if (input.id.empty() ||
                input.id.size() > kMaximumVfxProgramStringBytes ||
                !inputIds.emplace(input.id).second ||
                input.defaultValue.type != input.type ||
                !IsValidValue(input.defaultValue)) {
                outError = "VFX program input table is invalid";
                return false;
            }
        }

        uint64_t totalCapacity = 0u;
        uint64_t totalInputLinks = 0u;
        std::unordered_set<std::string> emitterIds{};
        std::unordered_set<std::string> specialOutputIds{};
        for (uint32_t index = 0u;
             index < program.emitters.size();
             ++index) {
            const VfxProgramEmitter& emitter = program.emitters[index];
            if (!emitter.animationTiming.IsValid() || emitter.layerCurves.size()>RENDER3D::MATERIAL::kGpuMaterialProgramParameterSlotCount+18+kMaximumVfxRuntimeEffectCurves) { outError="Layer curve budget exceeded"; return false; }
            if (emitter.render.effectMaterialIncludesParticleColor && !emitter.render.effectMaterial) {outError="Local adjustment material is missing";return false;}
            if(!emitter.render.animatedParameters.empty() || !emitter.render.spatialEffects.empty()) {outError="Compiled assets cannot contain per-frame parameter values";return false;}
            RENDER3D::MATERIAL::MaterialExpressionRuntimeProgram expression;
            if(emitter.kind != VfxLayerKind::Adjustment && std::any_of(emitter.layerCurves.begin(),emitter.layerCurves.end(),[](const auto& c){return c.channel>=VfxLayerChannel::MaterialParameter && c.channel<=VfxLayerChannel::MaterialEffectMix;})) {
                if(!emitter.render.effectMaterial || !RENDER3D::MATERIAL::BuildMaterialExpressionRuntimeProgram(emitter.render.effectMaterial->compileResult.program,expression,outError)) return false;
            }
            if (emitter.runtimeEffects.size()>kMaximumVfxEffectsPerLayer) {outError="Runtime effect budget exceeded";return false;}
            for (const auto& effect:emitter.runtimeEffects) {
                if (MaterialEffectName(effect.kind)=="Invalid" || !IsValidMaterialEffectBlend(effect) ||
                    !IsFinite(effect.parameters) || !IsFinite(effect.parameters2) || !IsFinite(effect.colorA) || !IsFinite(effect.colorB) ||
                    (effect.space==MaterialEffectSpace::Group3D ? !SupportsSpatialMaterialEffect(effect.kind) || effect.mask>MaterialEffectMask::Vertical :
                        effect.space!=MaterialEffectSpace::LayerUv || emitter.kind!=VfxLayerKind::Adjustment || !IsCompositeAdjustmentEffect(effect) || !IsAdjustmentMaterialEffect(effect.kind)) ||
                    effect.colorStops.size()>kMaximumMaterialEffectColorStops) {outError="Invalid runtime effect";return false;}
                float previous=-1;
                for (const auto& stop:effect.colorStops) {
                    if (!IsFinite(stop.time) || stop.time<0 || stop.time>1 || stop.time<=previous || !IsFinite(stop.color)) {outError="Invalid runtime gradient";return false;}
                    previous=stop.time;
                }
            }
            std::unordered_set<uint32_t> owners;
            size_t spatialCount=0;
            for (const auto owner:emitter.spatialOwners) {
                if (owner>=program.emitters.size() || !owners.insert(owner).second ||
                    (owner!=index && program.emitters[owner].kind!=VfxLayerKind::Adjustment)) {outError="Invalid spatial effect owner";return false;}
                const size_t previousCount=spatialCount;
                for (const auto& effect:program.emitters[owner].runtimeEffects)
                    if(effect.enabled && effect.space==MaterialEffectSpace::Group3D) ++spatialCount;
                if (previousCount==spatialCount) {outError="Spatial owner has no enabled deformation";return false;}
                if (owner!=index) {
                    bool inScope=false;
                    uint32_t scope=uint32_t(index);
                    for(size_t depth=0;depth<program.emitters.size() && scope<program.emitters.size();++depth) {
                        const auto parent=program.emitters[scope].parentEmitterIndex;
                        if (owner<scope && program.emitters[owner].parentEmitterIndex==parent) {inScope=true;break;}
                        scope=parent;
                    }
                    if (!inScope) {outError="Spatial owner does not precede content in its group";return false;}
                }
            }
            if (spatialCount>RENDER3D::PARTICLES::kMaximumParticleSpatialEffects) {outError="A layer supports at most eight active spatial deformations";return false;}
            std::unordered_set<uint64_t> layerChannels;
            for (const auto& curve:emitter.layerCurves) {
                const auto channel=static_cast<uint32_t>(curve.channel);
                if (curve.ownerEmitterIndex != UINT32_MAX &&
                    (curve.ownerEmitterIndex >= program.emitters.size() || curve.ownerEmitterIndex == index ||
                     program.emitters[curve.ownerEmitterIndex].kind != VfxLayerKind::Adjustment ||
                     curve.channel < VfxLayerChannel::MaterialParameter)) {
                    outError = "Invalid adjustment curve owner"; return false;
                }
                const bool runtime = curve.channel==VfxLayerChannel::EffectParameter || curve.channel==VfxLayerChannel::EffectPhase;
                const uint64_t identity=(uint64_t(runtime ? uint32_t(VfxLayerChannel::EffectParameter) : (std::min)(channel,uint32_t(VfxLayerChannel::MaterialParameter)))<<32)|curve.materialSlot;
                if (runtime && (curve.ownerEmitterIndex!=UINT32_MAX || curve.materialSlot/9u>=emitter.runtimeEffects.size())) {
                    outError="Invalid runtime effect animation slot";return false;
                }
                if(!runtime && emitter.kind != VfxLayerKind::Adjustment && channel>=static_cast<uint32_t>(VfxLayerChannel::MaterialParameter) &&
                    std::none_of(expression.parameterValueSlots.begin(),expression.parameterValueSlots.end(),[&](const auto& slot){return slot.second==curve.materialSlot;})) {
                    outError="Animated parameter does not reference a material value slot";return false;
                }
                if (!std::isfinite(curve.minimum) || !std::isfinite(curve.maximum) || curve.minimum>curve.maximum || channel>static_cast<uint32_t>(VfxLayerChannel::EffectPhase) ||
                    (emitter.kind==VfxLayerKind::Particles && channel<13) ||
                    (emitter.kind==VfxLayerKind::Adjustment && channel>=18 && !runtime) ||
                    (curve.channel==VfxLayerChannel::MaterialEffectMix && curve.ownerEmitterIndex==UINT32_MAX) ||
                    (!runtime && curve.materialSlot>=RENDER3D::MATERIAL::kGpuMaterialProgramParameterSlotCount) ||
                    (!runtime && emitter.kind != VfxLayerKind::Adjustment && channel>=static_cast<uint32_t>(VfxLayerChannel::MaterialParameter) && !emitter.render.effectMaterial) ||
                    static_cast<uint32_t>(curve.interpolation)>static_cast<uint32_t>(ParticleCurveInterpolation::Step) ||
                    !layerChannels.emplace(identity).second || curve.keys.empty() || curve.keys.size()>kMaxVfxLayerCurveKeys) {
                    outError="Invalid layer animation channel"; return false;
                }
                float previous=-1;
                for (const auto& key:curve.keys) {
                    if (!std::isfinite(key.time) || !std::isfinite(key.value) || !std::isfinite(key.inTangent) || !std::isfinite(key.outTangent) ||
                        key.time<0 || key.time>1 || key.time<=previous) { outError="Invalid layer animation key"; return false; }
                    previous=key.time;
                }
            }
            const ParticleEmitterSimulationParameters& simulation =
                emitter.simulation;
            const float maximumLifetime =
                simulation.lifetime.mode ==
                    ParticleDistributionMode::RandomRange
                ? simulation.lifetime.maximum
                : simulation.lifetime.minimum;
            if (emitter.emitterIndex != index ||
                !IsValidVfxHistory(emitter.history) ||
                (emitter.history.mode != VfxHistoryMode::None && (emitter.kind != VfxLayerKind::Shape ||
                    emitter.simulation.capacity != emitter.history.samples)) ||
                static_cast<uint8_t>(emitter.kind) > static_cast<uint8_t>(VfxLayerKind::Adjustment) ||
                emitter.sourceEmitterId.empty() ||
                emitter.sourceEmitterId.size() >
                    kMaximumVfxProgramStringBytes ||
                !emitterIds.emplace(emitter.sourceEmitterId).second ||
                (emitter.parentEmitterIndex != kInvalidVfxProgramIndex &&
                 emitter.parentEmitterIndex >= program.emitters.size()) ||
                emitter.parentEmitterIndex == index ||
                !IsFinite(emitter.transform.position) ||
                !IsFinite(emitter.transform.rotationEulerDegrees) ||
                !IsFinite(emitter.transform.scale) ||
                emitter.transform.scale.x == 0.0f ||
                emitter.transform.scale.y == 0.0f ||
                emitter.transform.scale.z == 0.0f ||
                simulation.capacity == 0u ||
                simulation.capacity > kGpuParticlePoolCapacity ||
                !IsValidDistribution(simulation.lifetime) ||
                simulation.lifetime.minimum <= 0.0f ||
                (emitter.kind == VfxLayerKind::Particles && maximumLifetime >
                    program.maximumParticleLifetimeSeconds) ||
                !IsValidDistribution(simulation.shapeRadius) ||
                (simulation.shape != ParticleShape::Point &&
                 simulation.shape != ParticleShape::Sphere &&
                 simulation.shape != ParticleShape::Cone &&
                 simulation.shape != ParticleShape::Box &&
                 simulation.shape != ParticleShape::Cylinder &&
                 simulation.shape != ParticleShape::Circle &&
                 simulation.shape != ParticleShape::Edge) ||
                !IsFinite(simulation.coneAngleRadians) ||
                simulation.coneAngleRadians < 0.0f ||
                simulation.coneAngleRadians > 3.14159265358979323846f ||
                !IsFinite(simulation.shapeBoxHalfExtents) ||
                simulation.shapeBoxHalfExtents.x < 0.0f ||
                simulation.shapeBoxHalfExtents.y < 0.0f ||
                simulation.shapeBoxHalfExtents.z < 0.0f ||
                !IsFinite(simulation.shapeCylinderHeight) ||
                simulation.shapeCylinderHeight < 0.0f ||
                !IsFinite(simulation.shapeCircleInnerRadius) ||
                simulation.shapeCircleInnerRadius < 0.0f ||
                simulation.shapeCircleEmitOnEdge > 1u ||
                !IsFinite(simulation.shapeEdgeLength) ||
                simulation.shapeEdgeLength < 0.0f ||
                (simulation.shape == ParticleShape::Circle &&
                 simulation.shapeCircleInnerRadius >
                    simulation.shapeRadius.minimum) ||
                !IsValidDistribution(simulation.radialSpeed) ||
                !IsValidAnimated(simulation.position) ||
                !IsValidAnimated(simulation.rotationRadians) ||
                !IsValidAnimated(simulation.scale) ||
                !RENDER3D::PARTICLES::IsValidParticleForceField(simulation.field) || !IsFinite(simulation.gravity) ||
                !IsFinite(simulation.drag) || simulation.drag < 0.0f ||
                simulation.localSimulation > 1u || simulation.collisionPlaneEnabled > 1u ||
                !IsFinite(simulation.collisionPlaneHeight) ||
                !IsFinite(simulation.restitution) || simulation.restitution < 0.0f || simulation.restitution > 1.0f ||
                !IsFinite(simulation.friction) || simulation.friction < 0.0f || simulation.friction > 1.0f ||
                !IsValidAnimated(simulation.color) ||
                !IsFinite(emitter.spawnRatePerSecond) ||
                emitter.spawnRatePerSecond < 0.0f ||
                emitter.authoredOverrides.enabled > 1u ||
                !IsFinite(
                    emitter.authoredOverrides.spawnRateMultiplier) ||
                !IsFinite(
                    emitter.authoredOverrides.radialSpeedMultiplier) ||
                !IsFinite(
                    emitter.authoredOverrides.scaleMultiplier) ||
                !IsFinite(
                    emitter.authoredOverrides.gravityMultiplier) ||
                !IsFinite(
                    emitter.authoredOverrides.colorMultiplier) ||
                (emitter.render.materialMode !=
                    ParticleMaterialMode::StandardUnlit &&
                 emitter.render.materialMode !=
                    ParticleMaterialMode::CustomMaterial &&
                 emitter.render.materialMode != ParticleMaterialMode::StandardLit &&
                 emitter.render.materialMode != ParticleMaterialMode::CustomLit) ||
                (emitter.render.blendMode != ParticleBlendMode::Alpha &&
                 emitter.render.blendMode !=
                    ParticleBlendMode::Additive) ||
                (emitter.render.shape != ParticleRenderShape::Sprite &&
                 emitter.render.shape != ParticleRenderShape::Ribbon &&
                 emitter.render.shape != ParticleRenderShape::Ring &&
                 emitter.render.shape != ParticleRenderShape::MeshBox &&
                 emitter.render.shape != ParticleRenderShape::MeshSphere &&
                 emitter.render.shape != ParticleRenderShape::MeshCylinder &&
                 emitter.render.shape != ParticleRenderShape::MeshCone &&
                 emitter.render.shape != ParticleRenderShape::MeshHemisphere &&
                 emitter.render.shape != ParticleRenderShape::CrossPlane &&
                 emitter.render.shape != ParticleRenderShape::MeshTorus &&
                 emitter.render.shape != ParticleRenderShape::MeshCapsule) ||
                (emitter.render.billboardMode !=
                    ParticleBillboardMode::RotatedBillboard &&
                 emitter.render.billboardMode !=
                    ParticleBillboardMode::FixedYAxis &&
                 emitter.render.billboardMode !=
                     ParticleBillboardMode::DirectionalBillboard &&
                 emitter.render.billboardMode !=
                     ParticleBillboardMode::CylindricalBillboard &&
                 emitter.render.billboardMode !=
                     ParticleBillboardMode::FaceCameraPosition &&
                 emitter.render.billboardMode !=
                     ParticleBillboardMode::Fixed) ||
                (emitter.render.depthMode !=
                    ParticleDepthMode::WorldOccluded &&
                 emitter.render.depthMode !=
                    ParticleDepthMode::SoftIntersection &&
                 emitter.render.depthMode !=
                    ParticleDepthMode::ContactSurface &&
                 emitter.render.depthMode !=
                    ParticleDepthMode::ForegroundAccent) ||
                !IsFinite(emitter.render.softIntersectionDistance) ||
                (emitter.render.depthMode ==
                    ParticleDepthMode::SoftIntersection &&
                 (
                  emitter.render.softIntersectionDistance < 0.001f ||
                  emitter.render.softIntersectionDistance > 10.0f)) ||
                !IsFinite(emitter.render.depthBias) ||
                (emitter.render.depthMode ==
                    ParticleDepthMode::ContactSurface &&
                 (
                  emitter.render.depthBias < 0.0f ||
                  emitter.render.depthBias > 0.02f)) ||
                emitter.render.flipbookColumns == 0u ||
                emitter.render.flipbookColumns > 64u ||
                emitter.render.flipbookRows == 0u ||
                emitter.render.flipbookRows > 64u ||
                emitter.render.flipbookFrameCount == 0u ||
                emitter.render.flipbookFrameCount >
                    static_cast<uint64_t>(
                        emitter.render.flipbookColumns) *
                    static_cast<uint64_t>(
                        emitter.render.flipbookRows) ||
                !IsFinite(
                    emitter.render.flipbookFramesPerSecond) ||
                emitter.render.flipbookFramesPerSecond < 0.0f ||
                emitter.render.flipbookFramesPerSecond > 240.0f ||
                emitter.render.uvMode > 1u ||
                (emitter.render.uvSwapAxes != 0u &&
                 emitter.render.uvSwapAxes != 1u) ||
                !IsFinite(emitter.render.uvTileLength) ||
                !IsFinite(emitter.render.uvTilingU) ||
                !IsFinite(emitter.render.uvTilingV) ||
                emitter.render.uvTilingV <= 0.0f ||
                emitter.render.uvTilingU <= 0.0f ||
                (emitter.render.uvMode == 1u &&
                 emitter.render.uvTileLength <= 0.0f) ||
                !IsFinite(emitter.render.uvRotationRadians) ||
                !IsFinite(emitter.render.anchor) ||
                (!emitter.render.colorTextureAssetGuid.empty() &&
                 (emitter.render.colorTextureAssetGuid.size() >
                        kMaximumVfxProgramStringBytes ||
                  (!IsVfxBuiltinTextureAssetId(
                        emitter.render.colorTextureAssetGuid) &&
                   !IsValidAssetGuid(
                        emitter.render.colorTextureAssetGuid)))) ||
                (!emitter.render.materialAssetGuid.empty() &&
                 (emitter.render.materialAssetGuid.size() >
                        kMaximumVfxProgramStringBytes ||
                  !IsValidAssetGuid(
                    emitter.render.materialAssetGuid)))) {
                outError = "VFX program emitter table is invalid";
                return false;
            }
            if (emitter.kind != VfxLayerKind::Group && emitter.kind != VfxLayerKind::Adjustment)
                totalCapacity += AlignParticleCapacity(simulation.capacity);

            if (!IsFinite(emitter.startDelay) ||
                !IsFinite(emitter.activeDuration) ||
                emitter.startDelay < 0.0f || emitter.activeDuration < 0.0f ||
                emitter.startDelay > 3600.0f ||
                emitter.activeDuration > 3600.0f ||
                emitter.startDelay > program.durationSeconds ||
                (emitter.activeDuration > 0.0f &&
                 emitter.startDelay + emitter.activeDuration >
                    program.durationSeconds) ||
                emitter.attachment.mode != VfxAttachmentMode::World &&
                    emitter.attachment.mode != VfxAttachmentMode::Owner &&
                    emitter.attachment.mode !=
                        VfxAttachmentMode::OwnerSocket ||
                emitter.attachment.follow != VfxFollowMode::None &&
                    emitter.attachment.follow != VfxFollowMode::Position &&
                    emitter.attachment.follow != VfxFollowMode::Rotation &&
                    emitter.attachment.follow != VfxFollowMode::Transform ||
                emitter.attachment.mode == VfxAttachmentMode::OwnerSocket &&
                    emitter.attachment.socketName.empty() ||
                emitter.attachment.socketName.size() >
                    kMaximumVfxProgramStringBytes ||
                !IsFinite(emitter.attachment.offset.position) ||
                !IsFinite(emitter.attachment.offset.rotationEulerDegrees) ||
                !IsFinite(emitter.attachment.offset.scale) ||
                emitter.attachment.offset.scale.x == 0.0f ||
                emitter.attachment.offset.scale.y == 0.0f ||
                emitter.attachment.offset.scale.z == 0.0f) {
                outError = "VFX program emitter activation/attachment is invalid";
                return false;
            }

            if (emitter.burstTimeline.size() >
                kMaximumVfxProgramBurstKeys) {
                outError = "VFX program burst timeline is too large";
                return false;
            }
            std::unordered_set<std::string> burstIds{};
            float previousBurstTime = 0.0f;
            bool hasPreviousBurstTime = false;
            for (const VfxBurstKeyData& key : emitter.burstTimeline) {
                if (key.id.empty() ||
                    key.id.size() > kMaximumVfxProgramStringBytes ||
                    !burstIds.emplace(key.id).second ||
                    !IsFinite(key.time) || key.time < 0.0f ||
                    key.time > 3600.0f || key.count == 0u ||
                    key.count > simulation.capacity) {
                    outError = "VFX program burst timeline is invalid";
                    return false;
                }
                if (hasPreviousBurstTime && key.time < previousBurstTime) {
                    outError = "VFX program burst timeline is not sorted";
                    return false;
                }
                previousBurstTime = key.time;
                hasPreviousBurstTime = true;
                const double effectiveActiveDuration =
                    emitter.activeDuration > 0.0f
                    ? static_cast<double>(emitter.activeDuration)
                    : (std::max)(
                        0.0,
                        static_cast<double>(program.durationSeconds) -
                            static_cast<double>(emitter.startDelay));
                if (static_cast<double>(key.time) >
                    effectiveActiveDuration) {
                    outError =
                        "VFX program burst key exceeds emitter activation";
                    return false;
                }
            }

            if (emitter.specialOutputs.size() >
                kMaximumVfxProgramSpecialOutputs) {
                outError = "VFX program special output table is too large";
                return false;
            }
            for (const VfxSpecialOutputData& output :
                 emitter.specialOutputs) {
                if (output.id.empty() ||
                    output.id.size() > kMaximumVfxProgramStringBytes ||
                    !specialOutputIds.emplace(output.id).second ||
                    (output.type !=
                        VfxSpecialOutputType::WeaponPathDistortion &&
                     output.type != VfxSpecialOutputType::ScreenImpulse &&
                     output.type != VfxSpecialOutputType::SkinnedAfterimage &&
                     output.type != VfxSpecialOutputType::Light)) {
                    outError = "VFX program special output table is invalid";
                    return false;
                }
                const auto& weapon = output.weaponPathDistortion;
                const auto& screen = output.screenImpulse;
                const auto& afterimage = output.skinnedAfterimage;
                if (
                    (output.type ==
                        VfxSpecialOutputType::WeaponPathDistortion &&
                     (!IsFinite(weapon.distortion) ||
                      !IsFinite(weapon.blur) ||
                      !IsFinite(weapon.opacity) ||
                      !IsFinite(weapon.emissive) ||
                      !IsFinite(weapon.flow) ||
                      !IsFinite(weapon.width) ||
                      !IsFinite(weapon.lifetimeSeconds) ||
                      !IsFinite(weapon.bodyColor) ||
                      !IsFinite(weapon.edgeColor) ||
                      !IsNonNegative(weapon.bodyColor) ||
                      !IsNonNegative(weapon.edgeColor) ||
                      weapon.distortion < kVfxWeaponDistortionMin ||
                      weapon.distortion > kVfxWeaponDistortionMax ||
                      weapon.blur < kVfxWeaponBlurMin ||
                      weapon.blur > kVfxWeaponBlurMax ||
                      weapon.opacity < kVfxWeaponOpacityMin ||
                      weapon.opacity > kVfxWeaponOpacityMax ||
                      weapon.emissive < kVfxWeaponEmissiveMin ||
                      weapon.emissive > kVfxWeaponEmissiveMax ||
                      weapon.flow < kVfxWeaponFlowMin ||
                      weapon.flow > kVfxWeaponFlowMax ||
                      weapon.width < kVfxWeaponWidthMin ||
                      weapon.width > kVfxWeaponWidthMax ||
                      weapon.lifetimeSeconds < kVfxWeaponLifetimeMin ||
                      weapon.lifetimeSeconds > kVfxWeaponLifetimeMax)) ||
                    (output.type == VfxSpecialOutputType::ScreenImpulse &&
                     (!IsFinite(screen.durationSeconds) ||
                      !IsFinite(screen.maxRadius) ||
                      !IsFinite(screen.ringThickness) ||
                      !IsFinite(screen.distortion) ||
                      !IsFinite(screen.chromaticAberration) ||
                      !IsFinite(screen.flash) ||
                      !IsFinite(screen.intensity) ||
                      !IsFinite(screen.color) ||
                      !IsNonNegative(screen.color) ||
                      !IsFinite(screen.waveFrequency) ||
                      !IsFinite(screen.waveAmplitude) ||
                      !IsFinite(screen.waveDamping) ||
                      screen.durationSeconds < kVfxScreenDurationMin ||
                      screen.durationSeconds > kVfxScreenDurationMax ||
                      screen.maxRadius < kVfxScreenRadiusMin ||
                      screen.maxRadius > kVfxScreenRadiusMax ||
                      screen.ringThickness < kVfxScreenRingThicknessMin ||
                      screen.ringThickness > kVfxScreenRingThicknessMax ||
                      screen.distortion < kVfxScreenDistortionMin ||
                      screen.distortion > kVfxScreenDistortionMax ||
                      screen.chromaticAberration < kVfxScreenChromaticMin ||
                      screen.chromaticAberration > kVfxScreenChromaticMax ||
                      screen.flash < kVfxScreenFlashMin ||
                      screen.flash > kVfxScreenFlashMax ||
                      screen.intensity < kVfxScreenIntensityMin ||
                      screen.intensity > kVfxScreenIntensityMax ||
                      screen.waveFrequency < kVfxScreenWaveFrequencyMin ||
                      screen.waveFrequency > kVfxScreenWaveFrequencyMax ||
                      screen.waveAmplitude < kVfxScreenWaveAmplitudeMin ||
                      screen.waveAmplitude > kVfxScreenWaveAmplitudeMax ||
                      screen.waveDamping < kVfxScreenWaveDampingMin ||
                      screen.waveDamping > kVfxScreenWaveDampingMax ||
                      screen.waveCount < kVfxScreenImpulseWaveCountMin ||
                      screen.waveCount > kVfxScreenImpulseWaveCountMax)) ||
                    (output.type ==
                        VfxSpecialOutputType::SkinnedAfterimage &&
                     (!IsFinite(afterimage.lifetimeSeconds) ||
                      !IsFinite(afterimage.opacity) ||
                      !IsFinite(afterimage.fresnel) ||
                      !IsFinite(afterimage.dissolve) ||
                      !IsFinite(afterimage.shellOffset) ||
                      !IsFinite(afterimage.color) ||
                      !IsNonNegative(afterimage.color) ||
                      !IsFinite(afterimage.intensity) ||
                      afterimage.lifetimeSeconds < kVfxAfterimageLifetimeMin ||
                      afterimage.lifetimeSeconds > kVfxAfterimageLifetimeMax ||
                      afterimage.opacity < kVfxAfterimageOpacityMin ||
                      afterimage.opacity > kVfxAfterimageOpacityMax ||
                      afterimage.fresnel < kVfxAfterimageFresnelMin ||
                      afterimage.fresnel > kVfxAfterimageFresnelMax ||
                      afterimage.dissolve < kVfxAfterimageDissolveMin ||
                      afterimage.dissolve > kVfxAfterimageDissolveMax ||
                      afterimage.shellOffset < kVfxAfterimageShellOffsetMin ||
                      afterimage.shellOffset > kVfxAfterimageShellOffsetMax ||
                      afterimage.intensity < kVfxAfterimageIntensityMin ||
                      afterimage.intensity > kVfxAfterimageIntensityMax ||
                      afterimage.sampleCount < kVfxAfterimageSampleCountMin ||
                      afterimage.sampleCount > kVfxAfterimageSampleCountMax))) {
                    outError = "VFX program special output profile is invalid";
                    return false;
                }
            }

            if (emitter.inputLinks.size() >
                kMaximumVfxProgramLinksPerEmitter) {
                outError = "VFX program input link table is too large";
                return false;
            }
            totalInputLinks += emitter.inputLinks.size();
            if (totalInputLinks > kMaximumVfxProgramTableItems) {
                outError =
                    "VFX program total input link table is too large";
                return false;
            }
            std::unordered_set<uint32_t> boundSemantics{};
            for (const VfxProgramInputLink& link : emitter.inputLinks) {
                if (link.semantic <
                        VfxRuntimeBindingSemantic::EmitterEnabled ||
                    link.semantic >
                        VfxRuntimeBindingSemantic::ColorMultiplier) {
                    outError = "VFX program input semantic is invalid";
                    return false;
                }
                const uint32_t semantic =
                    static_cast<uint32_t>(link.semantic);
                const VfxRuntimePropertyLayout expected =
                    ResolveVfxRuntimePropertyLayout(link.semantic);
                const uint32_t storageSize = StorageSize(
                    expected.storageType);
                if (!boundSemantics.emplace(semantic).second ||
                    link.inputIndex >= program.inputs.size() ||
                    link.inputType !=
                        program.inputs[link.inputIndex].type ||
                    !IsVfxInputTypeCompatible(
                        link.inputType,
                        ExpectedInputType(link.semantic)) ||
                    link.byteOffset != expected.byteOffset ||
                    link.storageType != expected.storageType ||
                    storageSize == 0u ||
                    expected.byteOffset >
                        sizeof(ParticleEmitterRuntimeOverrides) -
                            storageSize ||
                    !IsFinite(link.scale) || !IsFinite(link.bias)) {
                    outError = "VFX program input link is invalid";
                    return false;
                }
            }
        }
        if (totalCapacity > kGpuParticlePoolCapacity) {
            outError = "VFX program aligned particle reservations exceed the pool";
            return false;
        }

        for (const auto& layer : program.emitters) {
            if (layer.orientAlongPath && layer.kind != VfxLayerKind::Shape && layer.kind != VfxLayerKind::Group) {outError="Invalid path orientation layer";return false;}
            if (layer.kind == VfxLayerKind::Adjustment && (layer.matteOnly || layer.matteSourceIndex != UINT32_MAX)) { outError="Invalid matte-only adjustment"; return false; }
            std::unordered_set<uint32_t> visited{layer.emitterIndex};
            auto source = layer.matteSourceIndex;
            while (source != UINT32_MAX) {
                if (source >= program.emitters.size() || !visited.insert(source).second ||
                    program.emitters[source].parentEmitterIndex != layer.parentEmitterIndex ||
                    program.emitters[source].kind == VfxLayerKind::Adjustment) {
                    outError="Invalid or cyclic track matte reference"; return false;
                }
                source = program.emitters[source].matteSourceIndex;
            }
        }
        const bool hasComposition = std::any_of(program.emitters.begin(), program.emitters.end(),
            [](const auto& emitter) { return emitter.kind == VfxLayerKind::Adjustment || emitter.matteSourceIndex != UINT32_MAX || emitter.matteOnly; });
        std::unordered_set<uint32_t> visited{};
        for (uint32_t emitterIndex = 0u;
             emitterIndex < program.emitters.size();
             ++emitterIndex) {
            visited.clear();
            if (hasComposition && !program.emitters[emitterIndex].render.meshAssetGuid.empty()) {
                outError = "VFX composition does not support model output";
                return false;
            }
            uint32_t current = emitterIndex;
            while (current != kInvalidVfxProgramIndex) {
                if (current >= program.emitters.size() ||
                    !visited.emplace(current).second) {
                    outError =
                        "VFX program emitter hierarchy is invalid";
                    return false;
                }
                current = program.emitters[current].parentEmitterIndex;
                if ((hasComposition && visited.size() > 8u) ||
                    (current < program.emitters.size() && program.emitters[current].kind == VfxLayerKind::Adjustment)) {
                    outError = "VFX composition hierarchy exceeds its scope limit or parents under an adjustment";
                    return false;
                }
            }
        }

        std::unordered_set<std::string> dependencies{};
        for (const AssetGuid& dependency : program.dependencyGuids) {
            if (!dependency.IsValid() ||
                !dependencies.emplace(dependency.value).second) {
                outError = "VFX program dependency table is invalid";
                return false;
            }
        }
        outError.clear();
        return true;
    }

} // namespace HIKARI::VFX
