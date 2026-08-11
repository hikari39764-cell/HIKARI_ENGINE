#include "Assets/Vfx/HIKARI_VfxValidation.h"

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

        bool IsOrdered(float minimum, float maximum) noexcept {
            return minimum <= maximum;
        }

        bool IsOrdered(
            const MATH::Vec3& minimum,
            const MATH::Vec3& maximum) noexcept {

            return minimum.x <= maximum.x &&
                minimum.y <= maximum.y &&
                minimum.z <= maximum.z;
        }

        bool IsOrdered(
            const MATH::Vec4& minimum,
            const MATH::Vec4& maximum) noexcept {

            return minimum.x <= maximum.x &&
                minimum.y <= maximum.y &&
                minimum.z <= maximum.z &&
                minimum.w <= maximum.w;
        }

        void AddError(
            std::vector<VfxDiagnostic>& diagnostics,
            std::string code,
            std::string message,
            std::string subjectId = {}) {

            diagnostics.push_back({
                VfxDiagnosticSeverity::Error,
                std::move(code),
                std::move(message),
                std::move(subjectId)
            });
        }

        bool RegisterId(
            std::string_view id,
            std::string_view kind,
            std::unordered_set<std::string>& ids,
            std::vector<VfxDiagnostic>& diagnostics) {

            if (id.empty()) {
                AddError(
                    diagnostics,
                    "Vfx.MissingStableId",
                    std::string(kind) + " requires a stable ID");
                return false;
            }
            if (!ids.emplace(id).second) {
                AddError(
                    diagnostics,
                    "Vfx.DuplicateStableId",
                    "stable ID is already used: " + std::string(id),
                    std::string(id));
                return false;
            }
            return true;
        }

        template<class T>
        bool IsDistributionValid(
            const VfxDistributionData<T>& distribution) noexcept {

            if (!IsFinite(distribution.minimum) ||
                !IsFinite(distribution.maximum)) {
                return false;
            }
            if (distribution.mode == VfxDistributionMode::Constant) {
                return true;
            }
            return distribution.mode == VfxDistributionMode::RandomRange &&
                IsOrdered(distribution.minimum, distribution.maximum);
        }

        template<class T>
        void ValidateDistribution(
            const VfxDistributionData<T>& distribution,
            std::string_view subjectId,
            std::vector<VfxDiagnostic>& diagnostics) {

            if (!IsDistributionValid(distribution)) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidDistribution",
                    "distribution values must be finite and random minimum "
                    "must not exceed maximum",
                    std::string(subjectId));
            }
        }

        void ValidateCurve(
            const VfxCurveData& curve,
            std::string_view ownerId,
            std::unordered_set<std::string>& ids,
            std::vector<VfxDiagnostic>& diagnostics) {

            ValidateDistribution(
                curve.randomOffset,
                ownerId,
                diagnostics);
            if (curve.keys.empty()) {
                AddError(
                    diagnostics,
                    "Vfx.EmptyCurve",
                    "curve requires at least one key",
                    std::string(ownerId));
                return;
            }
            if (curve.keys.size() >
                RENDER3D::PARTICLES::kMaxParticleCurveKeys) {
                AddError(
                    diagnostics,
                    "Vfx.CurveKeyLimit",
                    "curve exceeds the native GPU limit of 8 keys",
                    std::string(ownerId));
            }
            std::unordered_set<float> times{};
            for (const VfxCurveKeyData& key : curve.keys) {
                (void)RegisterId(
                    key.id,
                    "curve key",
                    ids,
                    diagnostics);
                if (!IsFinite(key.time) || key.time < 0.0f ||
                    key.time > 1.0f || !IsFinite(key.value) ||
                    !IsFinite(key.inTangent) ||
                    !IsFinite(key.outTangent)) {
                    AddError(
                        diagnostics,
                        "Vfx.InvalidCurveKey",
                        "curve key values must be finite and time must be "
                        "within [0, 1]",
                        key.id);
                }
                if (!times.emplace(key.time).second) {
                    AddError(
                        diagnostics,
                        "Vfx.DuplicateCurveTime",
                        "curve keys may not share the same time",
                        key.id);
                }
            }
        }

        void ValidateVec3Curve(
            const VfxVec3CurveData& curve,
            std::string_view ownerId,
            std::unordered_set<std::string>& ids,
            std::vector<VfxDiagnostic>& diagnostics) {

            ValidateCurve(curve.x, ownerId, ids, diagnostics);
            ValidateCurve(curve.y, ownerId, ids, diagnostics);
            ValidateCurve(curve.z, ownerId, ids, diagnostics);
        }

        void ValidateColorCurve(
            const VfxColorCurveData& curve,
            std::string_view ownerId,
            std::unordered_set<std::string>& ids,
            std::vector<VfxDiagnostic>& diagnostics) {

            ValidateCurve(curve.r, ownerId, ids, diagnostics);
            ValidateCurve(curve.g, ownerId, ids, diagnostics);
            ValidateCurve(curve.b, ownerId, ids, diagnostics);
            ValidateCurve(curve.a, ownerId, ids, diagnostics);
        }

        void ValidateGradient(
            const VfxColorGradientData& gradient,
            std::string_view ownerId,
            std::unordered_set<std::string>& ids,
            std::vector<VfxDiagnostic>& diagnostics) {

            if (gradient.stops.empty()) {
                AddError(
                    diagnostics,
                    "Vfx.EmptyGradient",
                    "color gradient requires at least one stop",
                    std::string(ownerId));
                return;
            }
            if (gradient.stops.size() >
                RENDER3D::PARTICLES::kMaxParticleColorStops) {
                AddError(
                    diagnostics,
                    "Vfx.ColorStopLimit",
                    "color gradient exceeds the native GPU limit of 8 stops",
                    std::string(ownerId));
            }
            std::unordered_set<float> times{};
            for (const VfxColorStopData& stop : gradient.stops) {
                (void)RegisterId(
                    stop.id,
                    "color stop",
                    ids,
                    diagnostics);
                if (!IsFinite(stop.time) || stop.time < 0.0f ||
                    stop.time > 1.0f || !IsFinite(stop.color)) {
                    AddError(
                        diagnostics,
                        "Vfx.InvalidColorStop",
                        "gradient values must be finite and time must be "
                        "within [0, 1]",
                        stop.id);
                }
                if (!times.emplace(stop.time).second) {
                    AddError(
                        diagnostics,
                        "Vfx.DuplicateColorTime",
                        "color stops may not share the same time",
                        stop.id);
                }
            }
        }

        void ValidateAnimatedVec3(
            const VfxAnimatedVec3Data& value,
            std::string_view ownerId,
            std::unordered_set<std::string>& ids,
            std::vector<VfxDiagnostic>& diagnostics) {

            switch (value.mode) {
            case VfxAnimatedValueMode::Fixed:
                ValidateDistribution(value.fixed, ownerId, diagnostics);
                break;
            case VfxAnimatedValueMode::Pva:
                ValidateDistribution(value.pva.initial, ownerId, diagnostics);
                ValidateDistribution(value.pva.velocity, ownerId, diagnostics);
                ValidateDistribution(
                    value.pva.acceleration, ownerId, diagnostics);
                break;
            case VfxAnimatedValueMode::Easing:
                ValidateDistribution(value.easing.start, ownerId, diagnostics);
                if (value.easing.useMiddle) {
                    ValidateDistribution(
                        value.easing.middle, ownerId, diagnostics);
                }
                ValidateDistribution(value.easing.end, ownerId, diagnostics);
                break;
            case VfxAnimatedValueMode::Curve:
                ValidateVec3Curve(value.curve, ownerId, ids, diagnostics);
                break;
            }
        }

        void ValidateAnimatedColor(
            const VfxAnimatedColorData& value,
            std::string_view ownerId,
            std::unordered_set<std::string>& ids,
            std::vector<VfxDiagnostic>& diagnostics) {

            switch (value.mode) {
            case VfxAnimatedColorMode::Fixed:
            case VfxAnimatedColorMode::RandomRange:
                ValidateDistribution(value.fixed, ownerId, diagnostics);
                if (value.mode == VfxAnimatedColorMode::Fixed &&
                    value.fixed.mode != VfxDistributionMode::Constant) {
                    AddError(
                        diagnostics,
                        "Vfx.ColorModeMismatch",
                        "fixed color requires a constant distribution",
                        std::string(ownerId));
                }
                if (value.mode == VfxAnimatedColorMode::RandomRange &&
                    value.fixed.mode != VfxDistributionMode::RandomRange) {
                    AddError(
                        diagnostics,
                        "Vfx.ColorModeMismatch",
                        "random color requires a random-range distribution",
                        std::string(ownerId));
                }
                break;
            case VfxAnimatedColorMode::Easing:
                ValidateDistribution(value.easing.start, ownerId, diagnostics);
                if (value.easing.useMiddle) {
                    ValidateDistribution(
                        value.easing.middle, ownerId, diagnostics);
                }
                ValidateDistribution(value.easing.end, ownerId, diagnostics);
                break;
            case VfxAnimatedColorMode::Curve:
                ValidateColorCurve(value.curve, ownerId, ids, diagnostics);
                break;
            case VfxAnimatedColorMode::Gradient:
                ValidateGradient(value.gradient, ownerId, ids, diagnostics);
                break;
            }
        }

        bool IsValueFinite(const VfxValue& value) noexcept {
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

        void RegisterModuleIds(
            const VfxEmitterData& emitter,
            std::unordered_set<std::string>& ids,
            std::vector<VfxDiagnostic>& diagnostics) {

            const std::array<std::pair<std::string_view, std::string_view>,
                11u> modules{
                std::pair{ std::string_view(emitter.basic.id), "basic module" },
                std::pair{ std::string_view(emitter.emitterTransform.id),
                    "emitter transform module" },
                std::pair{ std::string_view(emitter.spawn.id), "spawn module" },
                std::pair{ std::string_view(emitter.shape.id), "shape module" },
                std::pair{ std::string_view(emitter.position.id),
                    "position module" },
                std::pair{ std::string_view(emitter.rotation.id),
                    "rotation module" },
                std::pair{ std::string_view(emitter.scale.id), "scale module" },
                std::pair{ std::string_view(emitter.forces.id), "forces module" },
                std::pair{ std::string_view(emitter.color.id), "color module" },
                std::pair{ std::string_view(emitter.renderCommon.id),
                    "render common module" },
                std::pair{ std::string_view(emitter.renderShape.id),
                    "render shape module" },
            };
            for (const auto& [id, kind] : modules) {
                (void)RegisterId(id, kind, ids, diagnostics);
            }
        }
    }

    std::vector<VfxDiagnostic> ValidateVfxAsset(
        const VfxAssetData& asset) {

        std::vector<VfxDiagnostic> diagnostics{};
        if (asset.version != kVfxSourceVersion) {
            AddError(
                diagnostics,
                "Vfx.UnsupportedVersion",
                "unsupported VFX source version " +
                    std::to_string(asset.version));
        }
        if (asset.name.empty()) {
            AddError(diagnostics, "Vfx.MissingName", "VFX asset name is empty");
        }
        if (!IsFinite(asset.durationSeconds) ||
            asset.durationSeconds <= 0.0f ||
            asset.durationSeconds > 3600.0f) {
            AddError(
                diagnostics,
                "Vfx.InvalidDuration",
                "durationSeconds must be within (0, 3600]");
        }
        if (asset.emitters.empty()) {
            AddError(
                diagnostics,
                "Vfx.MissingEmitter",
                "VFX asset requires at least one emitter");
        }
        if (asset.emitters.size() > kMaximumVfxProgramEmitters) {
            AddError(
                diagnostics,
                "Vfx.EmitterBudgetExceeded",
                "emitter count exceeds the native VFX program budget");
        }

        std::unordered_set<std::string> ids{};
        std::unordered_map<std::string, VfxValueType> inputTypes{};
        for (const VfxExposedInputData& input : asset.inputs) {
            (void)RegisterId(input.id, "exposed input", ids, diagnostics);
            if (input.name.empty()) {
                AddError(
                    diagnostics,
                    "Vfx.MissingInputName",
                    "exposed input name is empty",
                    input.id);
            }
            if (input.defaultValue.type != input.type ||
                !IsValueFinite(input.defaultValue)) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidInputDefault",
                    "exposed input default value does not match its type",
                    input.id);
            }
            inputTypes.emplace(input.id, input.type);
        }

        std::unordered_map<std::string, const VfxEmitterData*> emitters{};
        uint64_t declaredParticleCapacity = 0u;
        for (const VfxEmitterData& emitter : asset.emitters) {
            (void)RegisterId(emitter.id, "emitter", ids, diagnostics);
            emitters.emplace(emitter.id, &emitter);
            if (emitter.name.empty()) {
                AddError(
                    diagnostics,
                    "Vfx.MissingEmitterName",
                    "emitter name is empty",
                    emitter.id);
            }
            RegisterModuleIds(emitter, ids, diagnostics);

            const VfxTransformData& transform =
                emitter.emitterTransform.transform;
            if (!IsFinite(transform.position) ||
                !IsFinite(transform.rotationEulerDegrees) ||
                !IsFinite(transform.scale) ||
                transform.scale.x == 0.0f ||
                transform.scale.y == 0.0f ||
                transform.scale.z == 0.0f) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidEmitterTransform",
                    "emitter transform must be finite with non-zero scale",
                    emitter.emitterTransform.id);
            }

            ValidateDistribution(
                emitter.basic.lifetime,
                emitter.basic.id,
                diagnostics);
            if (emitter.basic.lifetime.minimum <= 0.0f ||
                emitter.basic.lifetime.maximum <= 0.0f ||
                emitter.basic.lifetime.maximum > 3600.0f) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidLifetime",
                    "particle lifetime must be within (0, 3600]",
                    emitter.basic.id);
            }

            if (!IsFinite(emitter.spawn.ratePerSecond) ||
                emitter.spawn.ratePerSecond < 0.0f ||
                emitter.spawn.ratePerSecond > 1000000.0f ||
                (emitter.spawn.capacityMode !=
                    VfxParticleCapacityMode::Automatic &&
                 emitter.spawn.capacityMode !=
                    VfxParticleCapacityMode::Fixed) ||
                emitter.spawn.maximumParticles == 0u ||
                emitter.spawn.maximumParticles >
                    RENDER3D::PARTICLES::kGpuParticlePoolCapacity ||
                emitter.spawn.burstCount >
                    emitter.spawn.maximumParticles) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidSpawnModule",
                    "spawn rate, burst count, or particle capacity is invalid",
                    emitter.spawn.id);
            }
            declaredParticleCapacity += emitter.spawn.maximumParticles;

            ValidateDistribution(
                emitter.shape.radius,
                emitter.shape.id,
                diagnostics);
            if (emitter.shape.radius.minimum < 0.0f ||
                emitter.shape.radius.maximum < 0.0f ||
                !IsFinite(emitter.shape.coneAngleDegrees) ||
                emitter.shape.coneAngleDegrees < 0.0f ||
                emitter.shape.coneAngleDegrees > 180.0f) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidShapeModule",
                    "shape radius or cone angle is invalid",
                    emitter.shape.id);
            }

            ValidateDistribution(
                emitter.position.radialSpeed,
                emitter.position.id,
                diagnostics);
            ValidateAnimatedVec3(
                emitter.position.value,
                emitter.position.id,
                ids,
                diagnostics);
            ValidateAnimatedVec3(
                emitter.rotation.eulerDegrees,
                emitter.rotation.id,
                ids,
                diagnostics);
            ValidateAnimatedVec3(
                emitter.scale.value,
                emitter.scale.id,
                ids,
                diagnostics);
            if (!IsFinite(emitter.forces.gravity) ||
                !IsFinite(emitter.forces.drag) ||
                emitter.forces.drag < 0.0f ||
                emitter.forces.drag > 1000.0f) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidForcesModule",
                    "particle gravity or drag is invalid",
                    emitter.forces.id);
            }
            ValidateAnimatedColor(
                emitter.color.value,
                emitter.color.id,
                ids,
                diagnostics);

            if (emitter.renderCommon.materialMode !=
                    VfxMaterialMode::StandardUnlit &&
                emitter.renderCommon.materialMode !=
                    VfxMaterialMode::CustomMaterial) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidMaterialMode",
                    "render material mode is invalid",
                    emitter.renderCommon.id);
            }
            if (emitter.renderCommon.colorTextureAssetGuid.IsValid() &&
                !IsVfxBuiltinTextureAssetId(
                    emitter.renderCommon.colorTextureAssetGuid.value) &&
                !IsValidAssetGuid(
                    emitter.renderCommon.colorTextureAssetGuid.value)) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidColorTextureGuid",
                    "render color texture GUID is invalid",
                    emitter.renderCommon.id);
            }
            if (emitter.renderCommon.materialAssetGuid.IsValid() &&
                !IsValidAssetGuid(
                    emitter.renderCommon.materialAssetGuid.value)) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidMaterialGuid",
                    "render material GUID is invalid",
                    emitter.renderCommon.id);
            }
            const uint64_t flipbookCellCount =
                static_cast<uint64_t>(
                    emitter.renderCommon.flipbookColumns) *
                static_cast<uint64_t>(
                    emitter.renderCommon.flipbookRows);
            if (emitter.renderCommon.flipbookColumns == 0u ||
                emitter.renderCommon.flipbookColumns > 64u ||
                emitter.renderCommon.flipbookRows == 0u ||
                emitter.renderCommon.flipbookRows > 64u ||
                emitter.renderCommon.flipbookFrameCount == 0u ||
                emitter.renderCommon.flipbookFrameCount >
                    flipbookCellCount ||
                !IsFinite(
                    emitter.renderCommon.flipbookFramesPerSecond) ||
                emitter.renderCommon.flipbookFramesPerSecond < 0.0f ||
                emitter.renderCommon.flipbookFramesPerSecond > 240.0f) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidFlipbook",
                    "flipbook layout or playback rate is invalid",
                    emitter.renderCommon.id);
            }
            if (emitter.renderShape.billboardMode !=
                    VfxBillboardMode::RotatedBillboard &&
                emitter.renderShape.billboardMode !=
                    VfxBillboardMode::FixedYAxis &&
                emitter.renderShape.billboardMode !=
                    VfxBillboardMode::DirectionalBillboard &&
                emitter.renderShape.billboardMode !=
                    VfxBillboardMode::Fixed) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidBillboardMode",
                    "sprite orientation mode is invalid",
                    emitter.renderShape.id);
            }
            if (emitter.renderShape.ribbonSegmentCount < 2u ||
                emitter.renderShape.ribbonSegmentCount > 64u ||
                emitter.renderShape.ringSegmentCount < 3u ||
                emitter.renderShape.ringSegmentCount > 64u) {
                AddError(
                    diagnostics,
                    "Vfx.InvalidRenderShapeSegments",
                    "ribbon/ring segment count exceeds the mesh shader "
                    "contract (ribbon 2..64, ring 3..64)",
                    emitter.renderShape.id);
            }

            std::unordered_set<std::string> inputLinkTargets{};
            for (const VfxInputLinkData& link : emitter.inputLinks) {
                (void)RegisterId(link.id, "input link", ids, diagnostics);
                const std::string targetKey =
                    link.target.emitterId + "\n" +
                    link.target.moduleId + "\n" +
                    link.target.propertyId;
                if (!inputLinkTargets.emplace(targetKey).second) {
                    AddError(
                        diagnostics,
                        "Vfx.DuplicateInputTarget",
                        "only one input link may bind a runtime property",
                        link.id);
                }
                const auto input = inputTypes.find(link.inputId);
                if (input == inputTypes.end()) {
                    AddError(
                        diagnostics,
                        "Vfx.MissingInput",
                        "input link references an unknown exposed input",
                        link.id);
                }
                const VfxPropertySchema* schema =
                    ResolveVfxPropertySchema(emitter, link.target);
                if (schema == nullptr) {
                    AddError(
                        diagnostics,
                        "Vfx.UnknownPropertyAddress",
                        "input link target does not resolve to a VFX property",
                        link.id);
                } else if (!schema->bindable || !schema->runtimeMutable) {
                    AddError(
                        diagnostics,
                        "Vfx.PropertyNotBindable",
                        "input link target is not runtime bindable",
                        link.id);
                } else if (input != inputTypes.end() &&
                    !IsVfxInputTypeCompatible(input->second, *schema)) {
                    AddError(
                        diagnostics,
                        "Vfx.InputTypeMismatch",
                        "input type is incompatible with the target property",
                        link.id);
                }
                if (!IsFinite(link.scale) || !IsFinite(link.bias)) {
                    AddError(
                        diagnostics,
                        "Vfx.InvalidInputLink",
                        "input link scale and bias must be finite",
                        link.id);
                }
            }
        }

        if (declaredParticleCapacity >
            RENDER3D::PARTICLES::kGpuParticlePoolCapacity) {
            AddError(
                diagnostics,
                "Vfx.ParticleBudgetExceeded",
                "combined emitter capacity exceeds the native GPU particle "
                "pool budget");
        }

        for (const VfxEmitterData& emitter : asset.emitters) {
            if (!emitter.parentEmitterId.empty() &&
                !emitters.contains(emitter.parentEmitterId)) {
                AddError(
                    diagnostics,
                    "Vfx.MissingParentEmitter",
                    "emitter parent does not exist",
                    emitter.id);
            }
        }

        enum class Visit : uint8_t { None, Active, Complete };
        std::unordered_map<std::string, Visit> visits{};
        std::function<void(const VfxEmitterData&)> visit =
            [&](const VfxEmitterData& emitter) {
                Visit& state = visits[emitter.id];
                if (state == Visit::Complete) {
                    return;
                }
                if (state == Visit::Active) {
                    AddError(
                        diagnostics,
                        "Vfx.EmitterCycle",
                        "emitter hierarchy contains a cycle",
                        emitter.id);
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
        return diagnostics;
    }

    bool HasVfxErrors(
        const std::vector<VfxDiagnostic>& diagnostics) noexcept {

        return std::any_of(
            diagnostics.begin(),
            diagnostics.end(),
            [](const VfxDiagnostic& diagnostic) {
                return diagnostic.severity == VfxDiagnosticSeverity::Error;
            });
    }

} // namespace HIKARI::VFX
