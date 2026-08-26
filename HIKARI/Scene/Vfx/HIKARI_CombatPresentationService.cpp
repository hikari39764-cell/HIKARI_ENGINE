#include "Scene/Vfx/HIKARI_CombatPresentationService.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_VfxSpecialOutputBridge.h"

namespace HIKARI::VFX {
    namespace {
        constexpr size_t kMaximumTrailCount = 16u;
        constexpr size_t kMaximumSamplesPerTrail = 32u;
        constexpr float kMinimumBladeLength = 0.08f;
        constexpr float kMaximumBladeLength = 4.0f;
        constexpr float kMaximumTrailDiscontinuity = 2.5f;
        constexpr float kTrailInterpolationSpacing = 0.18f;
        constexpr uint64_t kMaximumContinuousFrameGap = 2u;
        constexpr float kDefaultTrailLifetimeSeconds = 0.14f;
        constexpr float kDefaultTrailOpacity = 0.72f;
        constexpr float kDefaultTrailEmissive = 1.35f;
        constexpr float kDefaultTrailFlow = 0.65f;
        constexpr MATH::Vec3 kDefaultTrailBodyColor{
            0.08f,
            0.52f,
            0.92f };
        constexpr MATH::Vec3 kDefaultTrailEdgeColor{
            0.72f,
            0.96f,
            1.0f };

        bool IsFinite(float value) noexcept {
            return std::isfinite(value);
        }

        bool IsFinite(const MATH::Vec3& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y) &&
                IsFinite(value.z);
        }

        bool IsFinite(const MATH::Quat& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y) &&
                IsFinite(value.z) && IsFinite(value.w);
        }

        float LengthSquared(const MATH::Vec3& value) noexcept {
            return value.x * value.x + value.y * value.y +
                value.z * value.z;
        }

        float Distance(const MATH::Vec3& lhs, const MATH::Vec3& rhs) noexcept {
            const MATH::Vec3 delta = lhs - rhs;
            return std::sqrt((std::max)(0.0f, LengthSquared(delta)));
        }

        MATH::Vec3 Lerp(
            const MATH::Vec3& from,
            const MATH::Vec3& to,
            float weight) noexcept {
            return from + (to - from) * weight;
        }

        float SmoothStep01(float value) noexcept {
            const float t = std::clamp(value, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float ActionScale(
            float requestValue,
            float defaultValue) noexcept {
            if (!std::isfinite(requestValue) ||
                !std::isfinite(defaultValue) ||
                defaultValue <= 0.0f) {
                return 1.0f;
            }
            return std::clamp(
                requestValue / defaultValue,
                0.0f,
                8.0f);
        }

        MATH::Vec3 ApplyColorActionScale(
            const MATH::Vec4& profileColor,
            const MATH::Vec3& requestColor,
            const MATH::Vec3& defaultColor) noexcept {
            const auto channel = [](float profileValue,
                                    float requestValue,
                                    float defaultValue) noexcept {
                const float profile = std::isfinite(profileValue)
                    ? (std::max)(0.0f, profileValue)
                    : 0.0f;
                return profile * ActionScale(requestValue, defaultValue);
            };
            return {
                channel(profileColor.x, requestColor.x, defaultColor.x),
                channel(profileColor.y, requestColor.y, defaultColor.y),
                channel(profileColor.z, requestColor.z, defaultColor.z) };
        }

        MATH::Vec3 NormalizeSafe(
            const MATH::Vec3& value,
            const MATH::Vec3& fallback) noexcept {
            if (LengthSquared(value) <= 1.0e-8f) {
                return fallback;
            }
            return MATH::Normalize(value);
        }

        bool ReservedEffectKind(CombatWorldEffectKind kind) noexcept {
            return static_cast<size_t>(kind) >=
                static_cast<size_t>(CombatWorldEffectKind::Count);
        }

        void RekeyCombatEmitterIds(
            VfxEmitterData& emitter,
            std::string_view idSuffix) {
            const std::string suffix(idSuffix);
            const auto qualify = [&suffix](std::string& id) {
                if (!id.empty()) {
                    id += "." + suffix;
                }
            };
            qualify(emitter.id);
            qualify(emitter.basic.id);
            qualify(emitter.emitterTransform.id);
            qualify(emitter.spawn.id);
            qualify(emitter.shape.id);
            qualify(emitter.position.id);
            qualify(emitter.rotation.id);
            qualify(emitter.scale.id);
            qualify(emitter.forces.id);
            qualify(emitter.color.id);
            qualify(emitter.renderCommon.id);
            qualify(emitter.renderShape.id);
            const auto qualifyCurve = [&qualify](VfxCurveData& curve) {
                for (VfxCurveKeyData& key : curve.keys) {
                    qualify(key.id);
                }
            };
            const auto qualifyVec3Curve =
                [&qualifyCurve](VfxVec3CurveData& curve) {
                qualifyCurve(curve.x);
                qualifyCurve(curve.y);
                qualifyCurve(curve.z);
            };
            const auto qualifyColorCurve =
                [&qualifyCurve](VfxColorCurveData& curve) {
                qualifyCurve(curve.r);
                qualifyCurve(curve.g);
                qualifyCurve(curve.b);
                qualifyCurve(curve.a);
            };
            qualifyVec3Curve(emitter.position.value.curve);
            qualifyVec3Curve(emitter.rotation.eulerDegrees.curve);
            qualifyVec3Curve(emitter.scale.value.curve);
            qualifyColorCurve(emitter.color.value.curve);
            for (VfxColorStopData& stop :
                emitter.color.value.gradient.stops) {
                qualify(stop.id);
            }
        }

        std::string_view PresetFor(CombatWorldEffectKind kind) noexcept {
            switch (kind) {
            case CombatWorldEffectKind::PerfectGuardBurst:
                return "shockwave_ring";
            case CombatWorldEffectKind::DodgeAfterimage:
                return "projectile_trail";
            case CombatWorldEffectKind::DangerTelegraph:
                return "shockwave_ring";
            case CombatWorldEffectKind::SkillImpact:
                return "burst_explosion";
            case CombatWorldEffectKind::LightImpact:
            case CombatWorldEffectKind::HeavyImpact:
            default:
                return "hit_impact";
            }
        }

        void SetWaterGradient(
            VfxEmitterData& emitter,
            const MATH::Vec4& start,
            const MATH::Vec4& end) {
            emitter.color.value.mode = VfxAnimatedColorMode::Gradient;
            if (emitter.color.value.gradient.stops.size() < 2u) {
                const std::string prefix = emitter.id + ".combat.color";
                emitter.color.value.gradient.stops = {
                    { prefix + ".start", 0.0f, start },
                    { prefix + ".end", 1.0f, end },
                };
                return;
            }
            emitter.color.value.gradient.stops.front().time = 0.0f;
            emitter.color.value.gradient.stops.front().color = start;
            emitter.color.value.gradient.stops.back().time = 1.0f;
            emitter.color.value.gradient.stops.back().color = end;
        }

        void AuthorCombatProgram(
            CombatWorldEffectKind kind,
            VfxAssetData& asset) {
            if (kind == CombatWorldEffectKind::DangerTelegraph) {
                asset.name = "Combat Danger Telegraph";
                asset.loop = false;
                asset.durationSeconds = 0.22f;
                if (asset.emitters.empty()) {
                    return;
                }
                asset.emitters.resize(1u);
                VfxEmitterData& ring = asset.emitters.front();
                ring.order = 0u;
                ring.name = "Danger Ground Ring";
                ring.spawn.ratePerSecond = 0.0f;
                ring.spawn.burstCount = 1u;
                ring.spawn.maximumParticles = 4u;
                ring.basic.lifetime = MakeVfxFloatConstant(0.20f);
                ring.position.radialSpeed = MakeVfxFloatConstant(0.0f);
                ring.forces.gravity = { 0.0f, 0.0f, 0.0f };
                ring.renderCommon.blendMode = VfxBlendMode::Additive;
                ring.renderShape.type = VfxRenderShapeType::Sprite;
                ring.renderShape.billboardMode = VfxBillboardMode::Fixed;
                ring.rotation.eulerDegrees.mode =
                    VfxAnimatedValueMode::Fixed;
                ring.rotation.eulerDegrees.fixed = MakeVfxVec3Constant(
                    { 90.0f, 0.0f, 0.0f });
                ring.scale.value.mode = VfxAnimatedValueMode::Easing;
                ring.scale.value.easing.start = MakeVfxVec3Constant(
                    { 1.94f, 1.94f, 1.94f });
                ring.scale.value.easing.end = MakeVfxVec3Constant(
                    { 2.02f, 2.02f, 2.02f });
                SetWaterGradient(
                    ring,
                    { 1.0f, 0.025f, 0.008f, 0.82f },
                    { 1.0f, 0.18f, 0.018f, 0.0f });
                return;
            }

            if (kind == CombatWorldEffectKind::DodgeAfterimage) {
                asset.name = "Combat Dodge Water Mist and Drops";
                asset.loop = false;
                asset.durationSeconds = 0.36f;
                if (asset.emitters.empty()) {
                    return;
                }

                VfxEmitterData& mist = asset.emitters.front();
                mist.order = 0u;
                mist.name = "Water Mist";
                mist.spawn.ratePerSecond = 0.0f;
                mist.spawn.burstCount = 10u;
                mist.spawn.maximumParticles = 128u;
                mist.basic.lifetime = {
                    VfxDistributionMode::RandomRange,
                    0.14f,
                    0.30f,
                    0u };
                mist.shape.type = VfxShapeType::Box;
                mist.shape.boxHalfExtents = { 0.24f, 0.52f, 0.18f };
                mist.position.radialSpeed = {
                    VfxDistributionMode::RandomRange,
                    0.10f,
                    0.60f,
                    0u };
                mist.forces.gravity = { 0.0f, -0.55f, 0.0f };
                mist.forces.drag = 0.85f;
                mist.renderCommon.colorTextureAssetGuid = AssetGuid{
                    "engine.vfx.texture.wispy_smoke_flipbook" };
                mist.renderCommon.blendMode = VfxBlendMode::Alpha;
                mist.renderCommon.flipbookColumns = 8u;
                mist.renderCommon.flipbookRows = 8u;
                mist.renderCommon.flipbookFrameCount = 64u;
                mist.renderCommon.flipbookFramesPerSecond = 36.0f;
                mist.renderCommon.flipbookLoop = false;
                mist.renderCommon.flipbookRandomStart = true;
                mist.renderShape.type = VfxRenderShapeType::Sprite;
                mist.renderShape.billboardMode =
                    VfxBillboardMode::RotatedBillboard;
                mist.scale.value.mode = VfxAnimatedValueMode::Easing;
                mist.scale.value.easing.start = MakeVfxVec3Constant(
                    { 0.20f, 0.30f, 0.20f });
                mist.scale.value.easing.end = MakeVfxVec3Constant(
                    { 0.82f, 0.62f, 0.82f });
                SetWaterGradient(
                    mist,
                    { 0.42f, 0.85f, 1.0f, 0.44f },
                    { 0.10f, 0.38f, 0.82f, 0.0f });

                VfxEmitterData drops = mist;
                RekeyCombatEmitterIds(drops, "water_drops");
                drops.order = 1u;
                drops.name = "Water Drops";
                drops.spawn.burstCount = 8u;
                drops.spawn.maximumParticles = 64u;
                drops.basic.lifetime = {
                    VfxDistributionMode::RandomRange,
                    0.18f,
                    0.36f,
                    0u };
                drops.shape.type = VfxShapeType::Sphere;
                drops.shape.radius = MakeVfxFloatConstant(0.035f);
                drops.position.radialSpeed = {
                    VfxDistributionMode::RandomRange,
                    0.65f,
                    1.35f,
                    0u };
                drops.forces.gravity = { 0.0f, -4.8f, 0.0f };
                drops.forces.drag = 0.15f;
                drops.renderCommon.colorTextureAssetGuid = AssetGuid{
                    "engine.vfx.texture.soft_disc" };
                drops.renderCommon.blendMode = VfxBlendMode::Alpha;
                drops.renderCommon.flipbookColumns = 1u;
                drops.renderCommon.flipbookRows = 1u;
                drops.renderCommon.flipbookFrameCount = 1u;
                drops.renderCommon.flipbookFramesPerSecond = 0.0f;
                drops.renderCommon.flipbookLoop = true;
                drops.renderCommon.flipbookRandomStart = false;
                drops.renderShape.type = VfxRenderShapeType::Sprite;
                drops.renderShape.billboardMode =
                    VfxBillboardMode::RotatedBillboard;
                drops.scale.value.mode = VfxAnimatedValueMode::Fixed;
                drops.scale.value.fixed = MakeVfxVec3Constant(
                    { 0.06f, 0.18f, 0.06f });
                SetWaterGradient(
                    drops,
                    { 0.68f, 0.94f, 1.0f, 0.82f },
                    { 0.14f, 0.44f, 0.92f, 0.0f });
                asset.emitters.push_back(std::move(drops));
                return;
            }

            if (kind == CombatWorldEffectKind::PerfectGuardBurst) {
                asset.name = "Combat Perfect Guard Burst";
                asset.loop = false;
                asset.durationSeconds = 0.64f;
                if (asset.emitters.empty()) {
                    return;
                }

                VfxEmitterData& ring = asset.emitters.front();
                ring.order = 0u;
                ring.name = "Guard Shockwave Ring";
                ring.spawn.ratePerSecond = 0.0f;
                ring.spawn.burstCount = 1u;
                ring.basic.lifetime = MakeVfxFloatConstant(0.52f);
                ring.renderCommon.blendMode = VfxBlendMode::Additive;
                ring.renderShape.type = VfxRenderShapeType::Sprite;
                ring.renderShape.billboardMode = VfxBillboardMode::Fixed;
                ring.scale.value.mode = VfxAnimatedValueMode::Easing;
                ring.scale.value.easing.start = MakeVfxVec3Constant(
                    { 0.16f, 0.16f, 0.16f });
                ring.scale.value.easing.end = MakeVfxVec3Constant(
                    { 4.20f, 4.20f, 4.20f });
                SetWaterGradient(
                    ring,
                    { 0.70f, 0.94f, 1.0f, 1.0f },
                    { 0.12f, 0.42f, 1.0f, 0.0f });

                VfxEmitterData flash = ring;
                RekeyCombatEmitterIds(flash, "guard_flash");
                flash.order = 1u;
                flash.name = "Guard Flash";
                flash.basic.lifetime = MakeVfxFloatConstant(0.16f);
                flash.renderCommon.colorTextureAssetGuid = AssetGuid{
                    "engine.vfx.texture.star_glint" };
                flash.renderShape.billboardMode =
                    VfxBillboardMode::RotatedBillboard;
                flash.rotation.eulerDegrees.fixed = MakeVfxVec3Constant(
                    { 0.0f, 0.0f, 0.0f });
                flash.scale.value.easing.start = MakeVfxVec3Constant(
                    { 0.42f, 0.42f, 0.42f });
                flash.scale.value.easing.end = MakeVfxVec3Constant(
                    { 1.20f, 1.20f, 1.20f });
                SetWaterGradient(
                    flash,
                    { 1.0f, 1.0f, 1.0f, 0.96f },
                    { 0.38f, 0.84f, 1.0f, 0.0f });
                asset.emitters.push_back(std::move(flash));
                return;
            }

            if (kind == CombatWorldEffectKind::SkillImpact) {
                asset.name = "Combat Skill Impact";
                asset.loop = false;
                asset.durationSeconds = 1.10f;
                for (size_t index = 0u;
                    index < asset.emitters.size();
                    ++index) {
                    VfxEmitterData& emitter = asset.emitters[index];
                    if (index == 0u) {
                        emitter.name = "Skill Energy Core";
                        emitter.spawn.ratePerSecond = 0.0f;
                        emitter.spawn.burstCount = 1u;
                        emitter.basic.lifetime =
                            MakeVfxFloatConstant(0.64f);
                        emitter.renderCommon.blendMode =
                            VfxBlendMode::Additive;
                        emitter.scale.value.mode =
                            VfxAnimatedValueMode::Easing;
                        emitter.scale.value.easing.start =
                            MakeVfxVec3Constant(
                                { 0.34f, 0.34f, 0.34f });
                        emitter.scale.value.easing.end =
                            MakeVfxVec3Constant(
                                { 1.85f, 1.85f, 1.85f });
                        SetWaterGradient(
                            emitter,
                            { 0.72f, 0.96f, 1.0f, 0.98f },
                            { 0.08f, 0.32f, 1.0f, 0.0f });
                    }
                    else if (index == 1u) {
                        emitter.name = "Skill Radial Sparks";
                        emitter.spawn.ratePerSecond = 0.0f;
                        emitter.spawn.burstCount = 20u;
                        emitter.basic.lifetime = {
                            VfxDistributionMode::RandomRange,
                            0.24f,
                            0.48f,
                            0u };
                        emitter.position.radialSpeed = {
                            VfxDistributionMode::RandomRange,
                            2.8f,
                            5.2f,
                            0u };
                        emitter.forces.gravity = { 0.0f, -4.0f, 0.0f };
                        emitter.renderCommon.blendMode =
                            VfxBlendMode::Additive;
                        SetWaterGradient(
                            emitter,
                            { 0.46f, 0.86f, 1.0f, 0.96f },
                            { 0.05f, 0.22f, 0.94f, 0.0f });
                    }
                    else {
                        emitter.name = "Skill Residual Mist";
                        emitter.renderCommon.blendMode = VfxBlendMode::Alpha;
                        emitter.renderCommon.colorTextureAssetGuid = AssetGuid{
                            "engine.vfx.texture.wispy_smoke_flipbook" };
                        SetWaterGradient(
                            emitter,
                            { 0.18f, 0.36f, 0.78f, 0.34f },
                            { 0.04f, 0.12f, 0.42f, 0.0f });
                    }
                }
                return;
            }

            const bool heavy = kind == CombatWorldEffectKind::HeavyImpact;
            asset.name = heavy
                ? "Combat Heavy Impact"
                : "Combat Light Impact";
            asset.loop = false;
            asset.durationSeconds = heavy ? 0.58f : 0.46f;
            for (size_t index = 0u; index < asset.emitters.size(); ++index) {
                VfxEmitterData& emitter = asset.emitters[index];
                const bool sparks = index == 0u;
                if (sparks) {
                    emitter.name = heavy ? "Heavy Sparks" : "Light Sparks";
                    emitter.spawn.ratePerSecond = 0.0f;
                    emitter.spawn.burstCount = heavy ? 48u : 34u;
                    emitter.spawn.maximumParticles = 128u;
                    emitter.basic.lifetime = {
                        VfxDistributionMode::RandomRange,
                        heavy ? 0.24f : 0.20f,
                        heavy ? 0.42f : 0.34f,
                        0u };
                    emitter.shape.type = VfxShapeType::Sphere;
                    emitter.shape.radius = MakeVfxFloatConstant(
                        heavy ? 0.065f : 0.05f);
                    emitter.position.radialSpeed = {
                        VfxDistributionMode::RandomRange,
                        heavy ? 3.5f : 3.0f,
                        heavy ? 5.6f : 4.4f,
                        0u };
                    emitter.forces.gravity = { 0.0f, -9.8f, 0.0f };
                    emitter.renderCommon.blendMode = VfxBlendMode::Additive;
                    emitter.renderShape.billboardMode =
                        VfxBillboardMode::DirectionalBillboard;
                    emitter.scale.value.mode =
                        VfxAnimatedValueMode::Fixed;
                    emitter.scale.value.fixed = MakeVfxVec3Constant(
                        { 0.08f, heavy ? 0.46f : 0.34f, 1.0f });
                    SetWaterGradient(
                        emitter,
                        heavy
                            ? MATH::Vec4{ 0.96f, 0.99f, 1.0f, 1.0f }
                            : MATH::Vec4{ 0.86f, 0.96f, 1.0f, 1.0f },
                        heavy
                            ? MATH::Vec4{ 0.22f, 0.62f, 1.0f, 0.0f }
                            : MATH::Vec4{ 0.14f, 0.42f, 0.92f, 0.0f });
                }
                else {
                    emitter.name = heavy
                        ? "Heavy Impact Ring"
                        : "Light Impact Ring";
                    emitter.spawn.ratePerSecond = 0.0f;
                    emitter.spawn.burstCount = 1u;
                    emitter.basic.lifetime = MakeVfxFloatConstant(
                        heavy ? 0.46f : 0.34f);
                    emitter.renderShape.type = VfxRenderShapeType::Sprite;
                    emitter.renderCommon.blendMode = VfxBlendMode::Additive;
                    emitter.position.radialSpeed =
                        MakeVfxFloatConstant(0.0f);
                    emitter.scale.value.mode =
                        VfxAnimatedValueMode::Easing;
                    emitter.scale.value.easing.start =
                        MakeVfxVec3Constant({ 0.16f, 0.16f, 0.16f });
                    emitter.scale.value.easing.end = MakeVfxVec3Constant(
                        { heavy ? 2.45f : 1.75f,
                            heavy ? 2.45f : 1.75f,
                            heavy ? 2.45f : 1.75f });
                    SetWaterGradient(
                        emitter,
                        heavy
                            ? MATH::Vec4{ 0.50f, 0.90f, 1.0f, 0.98f }
                            : MATH::Vec4{ 0.38f, 0.82f, 1.0f, 0.86f },
                        { 0.04f, 0.22f, 0.78f, 0.0f });
                }
            }
        }

        float EffectScale(CombatWorldEffectKind kind) noexcept {
            switch (kind) {
            case CombatWorldEffectKind::HeavyImpact: return 1.26f;
            case CombatWorldEffectKind::SkillImpact: return 1.48f;
            case CombatWorldEffectKind::PerfectGuardBurst: return 1.36f;
            case CombatWorldEffectKind::DodgeAfterimage: return 1.0f;
            case CombatWorldEffectKind::DangerTelegraph: return 1.0f;
            case CombatWorldEffectKind::LightImpact:
            default: return 0.92f;
            }
        }
    }

    struct CombatPresentationService::TrailSample {
        MATH::Vec3 bladeBaseWorld{};
        MATH::Vec3 bladeTipWorld{};
        MATH::Vec3 bodyColor{};
        MATH::Vec3 edgeColor{};
        float bodyAlpha = 1.0f;
        float edgeAlpha = 1.0f;
        float lifetimeSeconds = 0.14f;
        float ageSeconds = 0.0f;
        float opacity = 0.72f;
        float emissiveIntensity = 1.35f;
        float flowStrength = 0.65f;
        float width = 1.0f;
        float distortionStrength = 1.0f;
        float blurScale = 1.0f;
    };

    struct CombatPresentationService::TrailState {
        RuntimeObjectHandle ownerObject{};
        uint64_t sourceId = 0u;
        uint64_t lastSubmissionFrame = 0u;
        std::vector<TrailSample> samples{};
    };

    CombatPresentationService::CombatPresentationService() = default;
    CombatPresentationService::~CombatPresentationService() = default;

    std::shared_ptr<const VfxProgram>
        CombatPresentationService::ResolveProgram(
            CombatWorldEffectKind kind) {
        if (ReservedEffectKind(kind)) {
            return {};
        }
        const size_t index = static_cast<size_t>(kind);
        if (programs_[index]) {
            return programs_[index];
        }
        VfxAssetData asset = PRESETS::MakeVfxPresetAsset(PresetFor(kind));
        AuthorCombatProgram(kind, asset);
        VfxCompileResult result = CompileVfxAsset(asset);
        if (!result.success) {
            return {};
        }
        programs_[index] = std::make_shared<VfxProgram>(
            std::move(result.program));
        return programs_[index];
    }

    bool CombatPresentationService::Prewarm() noexcept {
        try {
            for (size_t index = 0u;
                index < static_cast<size_t>(CombatWorldEffectKind::Count);
                ++index) {
                if (!ResolveProgram(
                        static_cast<CombatWorldEffectKind>(index))) {
                    return false;
                }
            }
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool CombatPresentationService::PlayWorldEffect(
        VfxPlaybackService& playback,
        const CombatWorldEffectRequest& request) {
        if (ReservedEffectKind(request.kind) || request.sourceId == 0u ||
            !IsFinite(request.position) || !IsFinite(request.rotation) ||
            !IsFinite(request.scale) || !IsFinite(request.direction) ||
            !IsFinite(request.uniformScale) ||
            !IsFinite(request.intensity) ||
            request.uniformScale <= 0.0f || request.intensity <= 0.0f) {
            return false;
        }
        const std::shared_ptr<const VfxProgram> program =
            ResolveProgram(request.kind);
        if (!program) {
            return false;
        }
        const float scale = std::clamp(
            request.uniformScale,
            0.05f,
            8.0f) * EffectScale(request.kind) * std::sqrt(std::clamp(
                request.intensity,
                0.05f,
                4.0f));
        VfxPlayRequest play{};
        play.program = program;
        play.ownerObject = request.ownerObject;
        play.transform.position = request.position;
        play.transform.rotation = request.rotation;
        play.transform.scale = {
            request.scale.x * scale,
            request.scale.y * scale,
            request.scale.z * scale };
        play.loopOverride = false;
        play.randomSeed = request.randomSeed ^
            static_cast<uint32_t>(request.sourceId) ^
            static_cast<uint32_t>(request.sourceId >> 32u);
        return playback.PlayDetailed(play).IsAccepted();
    }

    bool CombatPresentationService::SubmitWeaponTrail(
        const WeaponTrailSampleRequest& request,
        uint64_t frameIndex) noexcept {
        if (!request.ownerObject.IsValid() || request.sourceId == 0u ||
            !IsFinite(request.bladeBaseWorld) ||
            !IsFinite(request.bladeTipWorld) ||
            !IsFinite(request.bodyColor) || !IsFinite(request.edgeColor) ||
            !IsFinite(request.bodyAlpha) || !IsFinite(request.edgeAlpha) ||
            !IsFinite(request.lifetimeUnscaledSeconds) ||
            !IsFinite(request.opacity) ||
            !IsFinite(request.emissiveIntensity) ||
            !IsFinite(request.flowStrength) ||
            !IsFinite(request.width) ||
            !IsFinite(request.distortionStrength) ||
            !IsFinite(request.blurScale)) {
            return false;
        }
        const float bladeLength = Distance(
            request.bladeBaseWorld,
            request.bladeTipWorld);
        if (bladeLength < kMinimumBladeLength ||
            bladeLength > kMaximumBladeLength) {
            return false;
        }

        auto found = std::find_if(
            trails_.begin(),
            trails_.end(),
            [&request](const TrailState& trail) {
                return trail.ownerObject == request.ownerObject &&
                    trail.sourceId == request.sourceId;
            });
        if (found == trails_.end()) {
            if (trails_.size() >= kMaximumTrailCount) {
                return false;
            }
            TrailState state{};
            state.ownerObject = request.ownerObject;
            state.sourceId = request.sourceId;
            state.samples.reserve(kMaximumSamplesPerTrail);
            trails_.push_back(std::move(state));
            found = std::prev(trails_.end());
        }
        TrailState& trail = *found;
        const bool frameDiscontinuity = !trail.samples.empty() &&
            frameIndex > trail.lastSubmissionFrame +
                kMaximumContinuousFrameGap;
        const bool spatialDiscontinuity = !trail.samples.empty() &&
            (Distance(
                trail.samples.back().bladeBaseWorld,
                request.bladeBaseWorld) > kMaximumTrailDiscontinuity ||
             Distance(
                trail.samples.back().bladeTipWorld,
                request.bladeTipWorld) > kMaximumTrailDiscontinuity);
        if (frameDiscontinuity || spatialDiscontinuity) {
            trail.samples.clear();
        }

        TrailSample next{};
        next.bladeBaseWorld = request.bladeBaseWorld;
        next.bladeTipWorld = request.bladeTipWorld;
        next.bodyColor = {
            (std::max)(0.0f, request.bodyColor.x),
            (std::max)(0.0f, request.bodyColor.y),
            (std::max)(0.0f, request.bodyColor.z) };
        next.edgeColor = {
            (std::max)(0.0f, request.edgeColor.x),
            (std::max)(0.0f, request.edgeColor.y),
            (std::max)(0.0f, request.edgeColor.z) };
        next.bodyAlpha = std::clamp(request.bodyAlpha, 0.0f, 1.0f);
        next.edgeAlpha = std::clamp(request.edgeAlpha, 0.0f, 1.0f);
        next.lifetimeSeconds = std::clamp(
            request.lifetimeUnscaledSeconds,
            0.035f,
            0.45f);
        next.opacity = std::clamp(request.opacity, 0.0f, 1.0f);
        next.emissiveIntensity = std::clamp(
            request.emissiveIntensity,
            0.0f,
            8.0f);
        next.flowStrength = std::clamp(
            request.flowStrength,
            0.0f,
            2.0f);
        next.width = std::clamp(request.width, 0.05f, 8.0f);
        next.distortionStrength = std::clamp(
            request.distortionStrength,
            0.0f,
            8.0f);
        next.blurScale = std::clamp(request.blurScale, 0.0f, 8.0f);

        if (trail.lastSubmissionFrame == frameIndex &&
            !trail.samples.empty()) {
            trail.samples.back() = next;
            return true;
        }

        if (!trail.samples.empty()) {
            const TrailSample previous = trail.samples.back();
            const float travel = (std::max)(
                Distance(previous.bladeBaseWorld, next.bladeBaseWorld),
                Distance(previous.bladeTipWorld, next.bladeTipWorld));
            const uint32_t steps = std::clamp(
                static_cast<uint32_t>(std::ceil(
                    travel / kTrailInterpolationSpacing)),
                1u,
                4u);
            for (uint32_t step = 1u; step < steps; ++step) {
                const float weight = static_cast<float>(step) /
                    static_cast<float>(steps);
                TrailSample intermediate = next;
                intermediate.bladeBaseWorld = Lerp(
                    previous.bladeBaseWorld,
                    next.bladeBaseWorld,
                    weight);
                intermediate.bladeTipWorld = Lerp(
                    previous.bladeTipWorld,
                    next.bladeTipWorld,
                    weight);
                trail.samples.push_back(std::move(intermediate));
            }
        }
        trail.samples.push_back(std::move(next));
        if (trail.samples.size() > kMaximumSamplesPerTrail) {
            trail.samples.erase(
                trail.samples.begin(),
                trail.samples.begin() +
                    (trail.samples.size() - kMaximumSamplesPerTrail));
        }
        trail.lastSubmissionFrame = frameIndex;
        return true;
    }

    bool CombatPresentationService::SubmitWeaponTrail(
        const WeaponTrailSampleRequest& request,
        const VfxSpecialOutputData& output,
        uint64_t frameIndex) noexcept {

        if (!output.enabled ||
            output.type != VfxSpecialOutputType::WeaponPathDistortion) {
            return false;
        }

        // The compiled HVFX profile is the authored art source. The legacy
        // request contributes only frame-local action multipliers and the
        // world-space blade sample; it does not replace profile colors or
        // lifetime with old combat defaults.
        const VfxWeaponPathDistortionProfile& profile =
            output.weaponPathDistortion;
        WeaponTrailSampleRequest configured = request;
        const float lifetimeScale = ActionScale(
            request.lifetimeUnscaledSeconds,
            kDefaultTrailLifetimeSeconds);
        const float opacityScale = ActionScale(
            request.opacity,
            kDefaultTrailOpacity);
        const float emissiveScale = ActionScale(
            request.emissiveIntensity,
            kDefaultTrailEmissive);
        const float flowScale = ActionScale(
            request.flowStrength,
            kDefaultTrailFlow);
        const float widthScale = std::clamp(request.width, 0.05f, 8.0f);
        const float distortionScale = std::clamp(
            request.distortionStrength, 0.0f, 8.0f);
        const float blurScale = std::clamp(request.blurScale, 0.0f, 8.0f);
        // Profile RGB remains the authored base. Request colors are action
        // tints relative to the public GameProject defaults, so Summer's
        // per-action light/heavy/skill colors still affect the typed path
        // without replacing the HVFX profile itself.
        configured.bodyColor = ApplyColorActionScale(
            profile.bodyColor,
            request.bodyColor,
            kDefaultTrailBodyColor);
        configured.edgeColor = ApplyColorActionScale(
            profile.edgeColor,
            request.edgeColor,
            kDefaultTrailEdgeColor);
        configured.bodyAlpha = std::clamp(profile.bodyColor.w, 0.0f, 1.0f);
        configured.edgeAlpha = std::clamp(profile.edgeColor.w, 0.0f, 1.0f);
        configured.lifetimeUnscaledSeconds =
            profile.lifetimeSeconds * lifetimeScale;
        configured.opacity = profile.opacity * opacityScale;
        configured.emissiveIntensity = profile.emissive * emissiveScale;
        configured.flowStrength = profile.flow * flowScale;
        configured.width = (profile.width / 0.15f) * widthScale;
        configured.distortionStrength = profile.distortion * distortionScale;
        configured.blurScale = profile.blur * blurScale;
        return SubmitWeaponTrail(configured, frameIndex);
    }

    bool CombatPresentationService::SubmitWeaponTrail(
        const WeaponTrailSampleRequest& request,
        const VfxProgram& program,
        uint64_t frameIndex) noexcept {

        const VfxSpecialOutputData* output = FindVfxSpecialOutput(
            program,
            VfxSpecialOutputType::WeaponPathDistortion);
        if (output == nullptr) {
            // Assets authored before typed special outputs retain the old
            // numeric presentation request as a compatibility fallback.
            return SubmitWeaponTrail(request, frameIndex);
        }
        return SubmitWeaponTrail(request, *output, frameIndex);
    }

    bool CombatPresentationService::SubmitWeaponTrail(
        const WeaponTrailSampleRequest& request,
        const VfxProgram& program,
        std::string_view outputId,
        uint64_t frameIndex) noexcept {

        const VfxSpecialOutputData* output = FindVfxSpecialOutput(
            program,
            outputId,
            VfxSpecialOutputType::WeaponPathDistortion);
        return output != nullptr &&
            SubmitWeaponTrail(request, *output, frameIndex);
    }

    void CombatPresentationService::Tick(
        uint64_t frameIndex,
        float unscaledDeltaSeconds) noexcept {
        if (hasTickedFrame_ && lastTickFrame_ == frameIndex) {
            return;
        }
        hasTickedFrame_ = true;
        lastTickFrame_ = frameIndex;
        const float delta = IsFinite(unscaledDeltaSeconds)
            ? std::clamp(unscaledDeltaSeconds, 0.0f, 0.25f)
            : 0.0f;
        frameSource_.frameIndex = frameIndex;
        frameSource_.unscaledTimeSeconds = std::fmod(
            frameSource_.unscaledTimeSeconds + delta,
            4096.0f);
        for (TrailState& trail : trails_) {
            for (TrailSample& sample : trail.samples) {
                sample.ageSeconds += delta;
            }
            std::erase_if(
                trail.samples,
                [](const TrailSample& sample) {
                    return sample.ageSeconds >= sample.lifetimeSeconds;
                });
        }
        std::erase_if(
            trails_,
            [frameIndex](const TrailState& trail) {
                return trail.samples.empty() &&
                    frameIndex > trail.lastSubmissionFrame + 2u;
            });
        BuildWeaponTrailFrame();
        ++revision_;
        if (revision_ == 0u) {
            revision_ = 1u;
        }
        frameSource_.revision = revision_;
    }

    void CombatPresentationService::BuildWeaponTrailFrame() noexcept {
        weaponTrailVertices_.clear();
        frameSource_.activeWeaponTrailCount = 0u;
        // Distortion/blur are carried in each trail vertex. Keep the
        // frame-level compatibility values neutral so concurrent trails do
        // not inherit the strongest profile in the frame.
        frameSource_.distortionStrengthScale = 1.0f;
        frameSource_.blurScale = 1.0f;
        for (const TrailState& trail : trails_) {
            if (trail.samples.size() < 2u) {
                continue;
            }
            ++frameSource_.activeWeaponTrailCount;
            std::array<MATH::Vec3, kMaximumSamplesPerTrail> tangents{};
            std::array<MATH::Vec3, kMaximumSamplesPerTrail> normals{};
            const auto centerOf = [](const TrailSample& sample) noexcept {
                return (sample.bladeBaseWorld + sample.bladeTipWorld) * 0.5f;
            };
            const auto bladeSpanOf = [](const TrailSample& sample) noexcept {
                return NormalizeSafe(
                    sample.bladeTipWorld - sample.bladeBaseWorld,
                    { 0.0f, 1.0f, 0.0f });
            };
            for (size_t sampleIndex = 0u;
                sampleIndex < trail.samples.size();
                ++sampleIndex) {
                const TrailSample& sample = trail.samples[sampleIndex];
                const MATH::Vec3 center = centerOf(sample);
                MATH::Vec3 pathDelta{};
                if (sampleIndex == 0u) {
                    pathDelta = centerOf(trail.samples[1u]) - center;
                }
                else if (sampleIndex + 1u >= trail.samples.size()) {
                    pathDelta = center -
                        centerOf(trail.samples[sampleIndex - 1u]);
                }
                else {
                    pathDelta = centerOf(
                        trail.samples[sampleIndex + 1u]) -
                        centerOf(trail.samples[sampleIndex - 1u]);
                }
                MATH::Vec3 tangent = NormalizeSafe(
                    pathDelta,
                    bladeSpanOf(sample));
                if (sampleIndex > 0u &&
                    MATH::Dot(tangent, tangents[sampleIndex - 1u]) < 0.0f) {
                    tangent = tangent * -1.0f;
                }
                tangents[sampleIndex] = tangent;

                const MATH::Vec3 bladeSpan = bladeSpanOf(sample);
                MATH::Vec3 normal = MATH::Cross(tangent, bladeSpan);
                if (LengthSquared(normal) <= 1.0e-8f) {
                    normal = MATH::Cross(tangent, { 0.0f, 1.0f, 0.0f });
                }
                if (LengthSquared(normal) <= 1.0e-8f) {
                    normal = MATH::Cross(tangent, { 1.0f, 0.0f, 0.0f });
                }
                normal = NormalizeSafe(normal, { 0.0f, 0.0f, 1.0f });
                if (sampleIndex > 0u &&
                    MATH::Dot(normal, normals[sampleIndex - 1u]) < 0.0f) {
                    normal = normal * -1.0f;
                }
                normals[sampleIndex] = normal;
            }
            const float denominator = static_cast<float>(
                (std::max)(size_t{ 1u }, trail.samples.size() - 1u));
            const auto bladeBaseOf = [](const TrailSample& sample) noexcept {
                const MATH::Vec3 center =
                    (sample.bladeBaseWorld + sample.bladeTipWorld) * 0.5f;
                return center +
                    (sample.bladeBaseWorld - center) * sample.width;
            };
            const auto bladeTipOf = [](const TrailSample& sample) noexcept {
                const MATH::Vec3 center =
                    (sample.bladeBaseWorld + sample.bladeTipWorld) * 0.5f;
                return center +
                    (sample.bladeTipWorld - center) * sample.width;
            };
            for (size_t index = 1u; index < trail.samples.size(); ++index) {
                const TrailSample& previous = trail.samples[index - 1u];
                const TrailSample& current = trail.samples[index];
                const auto append = [this, denominator](
                    const TrailSample& sample,
                    const MATH::Vec3& position,
                    const MATH::Vec3& color,
                    float colorAlpha,
                    const MATH::Vec3& tangent,
                    const MATH::Vec3& normal,
                    float along,
                    float across) {
                    const float life = (std::max)(
                        0.0001f,
                        sample.lifetimeSeconds);
                    const float remaining = 1.0f - std::clamp(
                        sample.ageSeconds / life,
                        0.0f,
                        1.0f);
                    const float alpha = sample.opacity *
                        std::clamp(colorAlpha, 0.0f, 1.0f) *
                        SmoothStep01(remaining);
                    RENDER3D::COMBAT_VFX::WeaponTrailVertex vertex{};
                    vertex.position = position;
                    vertex.uv = { along / denominator, across };
                    vertex.color = {
                        color.x, color.y, color.z, alpha };
                    vertex.emissiveIntensity =
                        sample.emissiveIntensity;
                    vertex.flowStrength = sample.flowStrength;
                    const float edgeDistance =
                        (std::min)(across, 1.0f - across);
                    vertex.tangentWorld = tangent;
                    vertex.normalWorld = normal;
                    vertex.flowData = {
                        along / denominator + sample.ageSeconds * 2.0f,
                        alpha,
                        SmoothStep01(std::clamp(
                            edgeDistance * 4.0f * sample.width,
                            0.0f,
                            1.0f)) * std::clamp(
                                sample.blurScale,
                                0.0f,
                                8.0f),
                        std::clamp(
                            sample.distortionStrength,
                            0.0f,
                            8.0f) };
                    weaponTrailVertices_.push_back(vertex);
                };
                const float previousAlong = static_cast<float>(index - 1u);
                const float currentAlong = static_cast<float>(index);
                append(previous, bladeBaseOf(previous),
                    previous.bodyColor, previous.bodyAlpha,
                    tangents[index - 1u],
                    normals[index - 1u], previousAlong, 0.0f);
                append(previous, bladeTipOf(previous),
                    previous.edgeColor, previous.edgeAlpha,
                    tangents[index - 1u],
                    normals[index - 1u], previousAlong, 1.0f);
                append(current, bladeTipOf(current),
                    current.edgeColor, current.edgeAlpha,
                    tangents[index], normals[index],
                    currentAlong, 1.0f);
                append(previous, bladeBaseOf(previous),
                    previous.bodyColor, previous.bodyAlpha,
                    tangents[index - 1u],
                    normals[index - 1u], previousAlong, 0.0f);
                append(current, bladeTipOf(current),
                    current.edgeColor, current.edgeAlpha,
                    tangents[index], normals[index],
                    currentAlong, 1.0f);
                append(current, bladeBaseOf(current),
                    current.bodyColor, current.bodyAlpha,
                    tangents[index], normals[index],
                    currentAlong, 0.0f);
            }
        }
        frameSource_.weaponTrailVertices = weaponTrailVertices_.empty()
            ? nullptr
            : weaponTrailVertices_.data();
        frameSource_.weaponTrailVertexCount = static_cast<uint32_t>(
            (std::min)(
                weaponTrailVertices_.size(),
                static_cast<size_t>(
                    (std::numeric_limits<uint32_t>::max)())));
    }

    void CombatPresentationService::Reset() noexcept {
        trails_.clear();
        weaponTrailVertices_.clear();
        frameSource_ = {};
        revision_ = 0u;
        lastTickFrame_ = 0u;
        hasTickedFrame_ = false;
    }

    const RENDER3D::COMBAT_VFX::CombatPresentationFrameSource&
        CombatPresentationService::GetFrameSource() const noexcept {
        return frameSource_;
    }

} // namespace HIKARI::VFX
