#include "Assets/Vfx/HIKARI_VfxPresetRegistry.h"

#include <utility>

namespace HIKARI::VFX::PRESETS {
    namespace {

        const std::vector<VfxPresetDescriptor>& Catalog() {
            static const std::vector<VfxPresetDescriptor> catalog{
                { "basic_sprite", "Basic Sprite", "Foundations",
                    "Looping billboard sprites with soft alpha falloff" },
                { "burst_explosion", "Burst Explosion", "Combat",
                    "One-shot radial sphere burst with additive sparks" },
                { "muzzle_flash", "Muzzle Flash", "Combat",
                    "Short cone flash with edge-aligned streaks" },
                { "hit_impact", "Hit Impact", "Combat",
                    "Radial edge sparks plus a fading impact ring" },
                { "projectile_trail", "Projectile Trail", "Combat",
                    "Dense world-space ribbon strips for moving emitters" },
                { "beam", "Beam", "Combat",
                    "Layered beam: bright core ribbon, wide glow and muzzle flash" },
                { "smoke_plume", "Smoke Plume", "Ambient",
                    "Rising alpha smoke with curl and soft fade" },
                { "fire", "Fire", "Ambient",
                    "Layered cone flame with gradient color over life" },
                { "rain", "Rain", "Ambient",
                    "Stretched box-volume falling streaks" },
                { "snow", "Snow", "Ambient",
                    "Slow drifting box-volume flakes" },
                { "magic_aura", "Magic Aura", "Magic",
                    "Layered sphere-shell particles with additive glow" },
                { "ground_ring", "Ground Ring", "Magic",
                    "Expanding fixed-orientation magic ring on the ground plane" },
                { "shockwave_ring", "Shockwave Ring", "Magic",
                    "Centered fast-expanding ground-plane ring burst" },
                { "fountain", "Fountain", "Ambient",
                    "Directional cone streaks with gravity arcs" },
                { "sparkler", "Sparkler", "Ambient",
                    "High-rate point emitter with random streaks" },
            };
            return catalog;
        }

        // Every emitter added to a preset asset must receive stable IDs
        // that are unique across the whole asset: module IDs, gradient stop
        // IDs, and any curve key IDs. Validation rejects assets whose IDs
        // collide, so preset factories must re-key the default emitter.
        void UniquifyEmitterIds(
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
            qualify(emitter.attachment.id);
            qualify(emitter.spawn.id);
            qualify(emitter.shape.id);
            qualify(emitter.position.id);
            qualify(emitter.rotation.id);
            qualify(emitter.scale.id);
            qualify(emitter.forces.id);
            qualify(emitter.color.id);
            qualify(emitter.renderCommon.id);
            qualify(emitter.renderShape.id);
            for (VfxBurstKeyData& key : emitter.spawn.burstTimeline) {
                qualify(key.id);
            }
            for (VfxSpecialOutputData& output : emitter.specialOutputs) {
                qualify(output.id);
            }
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

        VfxEmitterData MakeBaseEmitter(
            std::string idSuffix,
            std::string name) {

            VfxEmitterData emitter =
                MakeDefaultVfxAssetData().emitters.front();
            UniquifyEmitterIds(emitter, idSuffix);
            emitter.name = std::move(name);
            return emitter;
        }

        VfxAssetData MakePresetAsset(
            VfxAssetData asset,
            std::vector<VfxEmitterData> emitters) {

            if (!emitters.empty()) {
                asset.emitters = std::move(emitters);
                uint32_t order = 0u;
                for (VfxEmitterData& emitter : asset.emitters) {
                    emitter.order = order++;
                }
            }
            return asset;
        }

        void SetColorStops(
            VfxEmitterData& emitter,
            const MATH::Vec4& start,
            const MATH::Vec4& end) {

            emitter.color.value.mode = VfxAnimatedColorMode::Gradient;
            const std::string prefix = emitter.id + ".color";
            emitter.color.value.gradient.stops = {
                { prefix + ".start", 0.0f, start },
                { prefix + ".end", 1.0f, end },
            };
        }

        void ConfigureFlipbook(
            VfxEmitterData& emitter,
            uint32_t columns,
            uint32_t rows,
            uint32_t frameCount,
            float framesPerSecond,
            bool loop,
            bool randomStart) {

            emitter.renderCommon.flipbookColumns = columns;
            emitter.renderCommon.flipbookRows = rows;
            emitter.renderCommon.flipbookFrameCount = frameCount;
            emitter.renderCommon.flipbookFramesPerSecond = framesPerSecond;
            emitter.renderCommon.flipbookLoop = loop;
            emitter.renderCommon.flipbookRandomStart = randomStart;
        }

        void SetStandardTexture(
            VfxAssetData& asset,
            size_t emitterIndex,
            std::string_view textureAssetId) {

            if (emitterIndex >= asset.emitters.size()) {
                return;
            }
            VfxEmitterData& emitter = asset.emitters[emitterIndex];
            emitter.renderCommon.materialMode =
                VfxMaterialMode::StandardUnlit;
            emitter.renderCommon.colorTextureAssetGuid =
                AssetGuid{ std::string(textureAssetId) };
        }

        void ApplyPresetTextures(
            std::string_view id,
            VfxAssetData& asset) {

            if (id == "burst_explosion") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.explosion_flipbook");
                SetStandardTexture(asset, 1u,
                    "engine.vfx.texture.energy_streak");
                SetStandardTexture(asset, 2u,
                    "engine.vfx.texture.wispy_smoke_flipbook");
            } else if (id == "muzzle_flash") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.muzzle_flame");
                SetStandardTexture(asset, 1u,
                    "engine.vfx.texture.energy_streak");
            } else if (id == "hit_impact") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.star_glint");
                SetStandardTexture(asset, 1u,
                    "engine.vfx.texture.energy_ring");
            } else if (id == "beam") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.energy_streak");
                SetStandardTexture(asset, 1u,
                    "engine.vfx.texture.energy_streak");
                SetStandardTexture(asset, 2u,
                    "engine.vfx.texture.lens_flare");
            } else if (id == "projectile_trail" ||
                       id == "rain" || id == "fountain") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.energy_streak");
            } else if (id == "smoke_plume") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.wispy_smoke_flipbook");
            } else if (id == "fire") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.flame_flipbook");
            } else if (id == "snow") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.soft_disc");
            } else if (id == "magic_aura") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.lens_flare");
            } else if (id == "ground_ring") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.magic_ring");
            } else if (id == "shockwave_ring") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.energy_ring");
            } else if (id == "sparkler") {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.star_glint");
            } else {
                SetStandardTexture(asset, 0u,
                    "engine.vfx.texture.soft_disc");
            }
        }

        VfxAssetData BuildBasicSprite() {
            VfxAssetData asset;
            asset.name = "Basic Sprite";
            asset.durationSeconds = 2.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 24.0f;
            emitter.basic.lifetime = MakeVfxFloatConstant(1.2f);
            emitter.shape.type = VfxShapeType::Sphere;
            emitter.shape.radius = MakeVfxFloatConstant(0.4f);
            emitter.scale.value.fixed =
                MakeVfxVec3Constant({ 0.25f, 0.25f, 0.25f });
            SetColorStops(emitter,
                { 1.0f, 1.0f, 1.0f, 1.0f },
                { 1.0f, 1.0f, 1.0f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildBurstExplosion() {
            VfxAssetData asset;
            asset.name = "Burst Explosion";
            asset.durationSeconds = 1.0f;
            asset.loop = false;
            VfxEmitterData core =
                MakeBaseEmitter("core", "Explosion Core");

            // A flipbook already represents the complete expanding fireball.
            // Stacking dozens of identical 1024px sheets only multiplies
            // additive overdraw without adding shape detail.
            core.spawn.ratePerSecond = 0.0f;
            core.spawn.burstCount = 2u;
            // Automatic pools are allocated at the compute thread-group
            // granularity. Keep the authored ceiling above that granularity so
            // the compiler can derive the actual 64-slot pool instead of
            // silently clamping it to an undersized authoring value.
            core.spawn.maximumParticles = 128u;
            core.basic.lifetime = MakeVfxFloatConstant(0.7f);
            core.shape.type = VfxShapeType::Point;
            core.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            core.renderCommon.blendMode = VfxBlendMode::Additive;
            ConfigureFlipbook(core, 5u, 5u, 25u, 30.0f, false, false);
            core.scale.value.mode = VfxAnimatedValueMode::Easing;
            core.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.55f, 0.55f, 0.55f });
            core.scale.value.easing.end =
                MakeVfxVec3Constant({ 2.2f, 2.2f, 2.2f });
            SetColorStops(core,
                { 1.0f, 0.75f, 0.3f, 1.0f },
                { 0.6f, 0.05f, 0.0f, 0.0f });

            VfxEmitterData sparks =
                MakeBaseEmitter("sparks", "Radial Sparks");
            sparks.spawn.ratePerSecond = 0.0f;
            sparks.spawn.burstCount = 16u;
            sparks.spawn.maximumParticles = 128u;
            sparks.basic.lifetime = {
                VfxDistributionMode::RandomRange, 0.22f, 0.5f, 0u };
            sparks.shape.type = VfxShapeType::Sphere;
            sparks.shape.radius = MakeVfxFloatConstant(0.08f);
            sparks.position.radialSpeed = {
                VfxDistributionMode::RandomRange, 2.5f, 5.0f, 0u };
            sparks.forces.gravity = { 0.0f, -4.0f, 0.0f };
            sparks.renderCommon.blendMode = VfxBlendMode::Additive;
            sparks.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            sparks.scale.value.fixed =
                MakeVfxVec3Constant({ 0.06f, 0.32f, 1.0f });
            SetColorStops(sparks,
                { 1.0f, 0.85f, 0.35f, 1.0f },
                { 1.0f, 0.18f, 0.0f, 0.0f });

            VfxEmitterData smoke =
                MakeBaseEmitter("smoke", "After Smoke");
            smoke.spawn.ratePerSecond = 0.0f;
            smoke.spawn.burstCount = 3u;
            smoke.spawn.maximumParticles = 128u;
            smoke.basic.lifetime = MakeVfxFloatConstant(1.1f);
            smoke.shape.type = VfxShapeType::Sphere;
            smoke.shape.radius = MakeVfxFloatConstant(0.12f);
            smoke.position.radialSpeed = {
                VfxDistributionMode::RandomRange, 0.2f, 0.65f, 0u };
            smoke.renderCommon.blendMode = VfxBlendMode::Alpha;
            ConfigureFlipbook(smoke, 8u, 8u, 64u, 36.0f, false, true);
            smoke.scale.value.mode = VfxAnimatedValueMode::Easing;
            smoke.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.35f, 0.35f, 0.35f });
            smoke.scale.value.easing.end =
                MakeVfxVec3Constant({ 1.45f, 1.45f, 1.45f });
            SetColorStops(smoke,
                { 0.32f, 0.26f, 0.22f, 0.55f },
                { 0.08f, 0.07f, 0.07f, 0.0f });

            return MakePresetAsset(
                std::move(asset),
                { std::move(core), std::move(sparks), std::move(smoke) });
        }

        VfxAssetData BuildMuzzleFlash() {
            VfxAssetData asset;
            asset.name = "Muzzle Flash";
            asset.durationSeconds = 0.15f;
            asset.loop = false;
            VfxEmitterData flash =
                MakeBaseEmitter("flash", "Main");

            flash.name = "Flash Cone";
            flash.spawn.ratePerSecond = 0.0f;
            flash.spawn.burstCount = 24u;
            flash.basic.lifetime = MakeVfxFloatConstant(0.12f);
            flash.shape.type = VfxShapeType::Cone;
            flash.shape.radius = MakeVfxFloatConstant(0.1f);
            flash.shape.coneAngleDegrees = 24.0f;
            flash.position.radialSpeed = MakeVfxFloatConstant(3.0f);
            flash.renderCommon.blendMode = VfxBlendMode::Additive;
            SetColorStops(flash,
                { 1.0f, 0.9f, 0.5f, 1.0f },
                { 1.0f, 0.4f, 0.0f, 0.0f });

            VfxEmitterData streaks = MakeBaseEmitter("streaks", "Streaks");
            streaks.spawn.ratePerSecond = 0.0f;
            streaks.spawn.burstCount = 10u;
            streaks.basic.lifetime = MakeVfxFloatConstant(0.1f);
            streaks.shape.type = VfxShapeType::Edge;
            streaks.shape.edgeLength = 0.6f;
            streaks.renderCommon.blendMode = VfxBlendMode::Additive;
            streaks.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            streaks.scale.value.fixed =
                MakeVfxVec3Constant({ 0.08f, 0.7f, 1.0f });
            SetColorStops(streaks,
                { 1.0f, 0.85f, 0.4f, 1.0f },
                { 1.0f, 0.5f, 0.0f, 0.0f });
            return MakePresetAsset(
                std::move(asset),
                { std::move(flash), std::move(streaks) });
        }

        VfxAssetData BuildHitImpact() {
            VfxAssetData asset;
            asset.name = "Hit Impact";
            asset.durationSeconds = 0.5f;
            asset.loop = false;
            VfxEmitterData sparks =
                MakeBaseEmitter("sparks", "Main");

            sparks.name = "Sparks";
            sparks.spawn.ratePerSecond = 0.0f;
            sparks.spawn.burstCount = 40u;
            sparks.basic.lifetime = MakeVfxFloatConstant(0.35f);
            sparks.shape.type = VfxShapeType::Sphere;
            sparks.shape.radius = MakeVfxFloatConstant(0.05f);
            sparks.position.radialSpeed = MakeVfxFloatConstant(4.0f);
            sparks.forces.gravity = { 0.0f, -9.8f, 0.0f };
            sparks.renderCommon.blendMode = VfxBlendMode::Additive;
            sparks.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            SetColorStops(sparks,
                { 1.0f, 0.8f, 0.4f, 1.0f },
                { 1.0f, 0.3f, 0.0f, 0.0f });

            VfxEmitterData ring = MakeBaseEmitter("ring", "Impact Ring");
            ring.spawn.ratePerSecond = 0.0f;
            ring.spawn.burstCount = 1u;
            ring.basic.lifetime = MakeVfxFloatConstant(0.4f);
            // The built-in impact texture is a complete radial image, so it
            // belongs on a Sprite quad rather than a strip-mapped Ring mesh.
            ring.renderShape.type = VfxRenderShapeType::Sprite;
            ring.renderCommon.blendMode = VfxBlendMode::Additive;
            ring.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            ring.scale.value.mode = VfxAnimatedValueMode::Easing;
            ring.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.2f, 0.2f, 0.2f });
            ring.scale.value.easing.end =
                MakeVfxVec3Constant({ 2.0f, 2.0f, 2.0f });
            SetColorStops(ring,
                { 1.0f, 0.9f, 0.6f, 0.9f },
                { 1.0f, 0.5f, 0.1f, 0.0f });
            return MakePresetAsset(
                std::move(asset),
                { std::move(sparks), std::move(ring) });
        }

        VfxAssetData BuildProjectileTrail() {
            VfxAssetData asset;
            asset.name = "Projectile Trail";
            asset.durationSeconds = 1.5f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 60.0f;
            emitter.basic.lifetime = MakeVfxFloatConstant(0.6f);
            emitter.renderShape.type = VfxRenderShapeType::Ribbon;
            emitter.renderShape.ribbonSegmentCount = 24u;
            emitter.renderShape.ribbonWidth = 0.08f;
            emitter.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            emitter.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            emitter.scale.value.fixed =
                MakeVfxVec3Constant({ 0.6f, 1.0f, 1.0f });
            emitter.renderCommon.blendMode = VfxBlendMode::Additive;
            SetColorStops(emitter,
                { 0.4f, 0.7f, 1.0f, 0.9f },
                { 0.1f, 0.2f, 1.0f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildBeam() {
            VfxAssetData asset;
            asset.name = "Beam";
            asset.durationSeconds = 1.0f;
            asset.loop = true;

            // 実際のビームは「明るい芯＋広がるグロー＋根本のフラッシュ」の
            // 3 層で構成する。すべて同一の安定した単一パーティクルで、
            // エミッター回転とスケールだけでビームの方向と長さを決める。
            VfxEmitterData core = MakeBaseEmitter("core", "Beam Core");
            core.spawn.ratePerSecond = 0.0f;
            core.spawn.burstCount = 1u;
            core.basic.lifetime = MakeVfxFloatConstant(1.05f);
            core.shape.type = VfxShapeType::Point;
            core.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            core.renderShape.type = VfxRenderShapeType::Ribbon;
            core.renderShape.ribbonSegmentCount = 8u;
            core.renderShape.ribbonWidth = 0.12f;
            core.renderShape.billboardMode =
                VfxBillboardMode::RotatedBillboard;
            core.renderCommon.blendMode = VfxBlendMode::Additive;
            core.scale.value.fixed =
                MakeVfxVec3Constant({ 4.0f, 1.0f, 1.0f });
            SetColorStops(core,
                { 1.0f, 1.0f, 1.0f, 1.0f },
                { 0.6f, 0.9f, 1.0f, 0.0f });

            VfxEmitterData glow = MakeBaseEmitter("glow", "Beam Glow");
            glow.spawn.ratePerSecond = 0.0f;
            glow.spawn.burstCount = 1u;
            glow.basic.lifetime = MakeVfxFloatConstant(1.05f);
            glow.shape.type = VfxShapeType::Point;
            glow.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            glow.renderShape.type = VfxRenderShapeType::Ribbon;
            glow.renderShape.ribbonSegmentCount = 8u;
            glow.renderShape.ribbonWidth = 0.45f;
            glow.renderShape.billboardMode =
                VfxBillboardMode::RotatedBillboard;
            glow.renderCommon.blendMode = VfxBlendMode::Additive;
            glow.scale.value.fixed =
                MakeVfxVec3Constant({ 4.0f, 1.0f, 1.0f });
            SetColorStops(glow,
                { 0.2f, 0.5f, 1.0f, 0.4f },
                { 0.05f, 0.2f, 0.9f, 0.0f });

            VfxEmitterData flash = MakeBaseEmitter("flash", "Beam Flash");
            flash.spawn.ratePerSecond = 0.0f;
            flash.spawn.burstCount = 1u;
            flash.basic.lifetime = MakeVfxFloatConstant(1.05f);
            flash.shape.type = VfxShapeType::Point;
            flash.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            flash.renderShape.type = VfxRenderShapeType::Sprite;
            flash.renderCommon.blendMode = VfxBlendMode::Additive;
            // ビームの根本（ローカル X-）にフレアを置き、発射口を明るくする。
            flash.emitterTransform.transform.position =
                { -2.0f, 0.0f, 0.0f };
            flash.scale.value.fixed =
                MakeVfxVec3Constant({ 0.9f, 0.9f, 0.9f });
            SetColorStops(flash,
                { 0.8f, 0.95f, 1.0f, 1.0f },
                { 0.4f, 0.7f, 1.0f, 0.0f });
            return MakePresetAsset(
                std::move(asset),
                { std::move(core), std::move(glow), std::move(flash) });
        }

        VfxAssetData BuildSmokePlume() {
            VfxAssetData asset;
            asset.name = "Smoke Plume";
            asset.durationSeconds = 3.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 12.0f;
            emitter.basic.lifetime = MakeVfxFloatConstant(2.4f);
            emitter.shape.type = VfxShapeType::Sphere;
            emitter.shape.radius = MakeVfxFloatConstant(0.3f);
            emitter.position.value.pva.velocity =
                MakeVfxVec3Constant({ 0.0f, 1.2f, 0.0f });
            emitter.position.value.mode = VfxAnimatedValueMode::Pva;
            emitter.renderCommon.blendMode = VfxBlendMode::Alpha;
            ConfigureFlipbook(emitter, 8u, 8u, 64u, 16.0f, true, true);
            SetColorStops(emitter,
                { 0.5f, 0.5f, 0.55f, 0.5f },
                { 0.4f, 0.4f, 0.45f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildFire() {
            VfxAssetData asset;
            asset.name = "Fire";
            asset.durationSeconds = 2.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 40.0f;
            emitter.basic.lifetime = MakeVfxFloatConstant(0.9f);
            emitter.shape.type = VfxShapeType::Cone;
            emitter.shape.radius = MakeVfxFloatConstant(0.25f);
            emitter.shape.coneAngleDegrees = 18.0f;
            emitter.position.radialSpeed = MakeVfxFloatConstant(1.4f);
            emitter.renderCommon.blendMode = VfxBlendMode::Additive;
            ConfigureFlipbook(emitter, 16u, 4u, 64u, 32.0f, true, true);
            emitter.scale.value.mode = VfxAnimatedValueMode::Easing;
            emitter.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.6f, 0.6f, 0.6f });
            emitter.scale.value.easing.end =
                MakeVfxVec3Constant({ 0.1f, 0.4f, 0.1f });
            SetColorStops(emitter,
                { 1.0f, 0.7f, 0.2f, 1.0f },
                { 0.8f, 0.1f, 0.0f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildRain() {
            VfxAssetData asset;
            asset.name = "Rain";
            asset.durationSeconds = 2.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 180.0f;
            emitter.spawn.maximumParticles = 2048u;
            emitter.basic.lifetime = MakeVfxFloatConstant(1.2f);
            emitter.shape.type = VfxShapeType::Box;
            emitter.shape.boxHalfExtents = { 8.0f, 0.1f, 8.0f };
            emitter.emitterTransform.transform.position =
                { 0.0f, 6.0f, 0.0f };
            emitter.position.radialSpeed = MakeVfxFloatConstant(-6.0f);
            emitter.forces.gravity = { 0.0f, -2.5f, 0.0f };
            emitter.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            emitter.scale.value.fixed =
                MakeVfxVec3Constant({ 0.06f, 0.8f, 1.0f });
            SetColorStops(emitter,
                { 0.6f, 0.7f, 0.9f, 0.6f },
                { 0.6f, 0.7f, 0.9f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildSnow() {
            VfxAssetData asset;
            asset.name = "Snow";
            asset.durationSeconds = 4.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 60.0f;
            emitter.basic.lifetime = MakeVfxFloatConstant(3.5f);
            emitter.shape.type = VfxShapeType::Box;
            emitter.shape.boxHalfExtents = { 8.0f, 0.1f, 8.0f };
            emitter.emitterTransform.transform.position =
                { 0.0f, 6.0f, 0.0f };
            emitter.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            emitter.forces.gravity = { 0.0f, -0.6f, 0.0f };
            emitter.forces.drag = 0.2f;
            emitter.scale.value.fixed =
                MakeVfxVec3Constant({ 0.12f, 0.12f, 0.12f });
            SetColorStops(emitter,
                { 1.0f, 1.0f, 1.0f, 0.9f },
                { 1.0f, 1.0f, 1.0f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildMagicAura() {
            VfxAssetData asset;
            asset.name = "Magic Aura";
            asset.durationSeconds = 3.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 24.0f;
            emitter.basic.lifetime = MakeVfxFloatConstant(1.6f);
            emitter.shape.type = VfxShapeType::Sphere;
            emitter.shape.radius = MakeVfxFloatConstant(1.0f);
            emitter.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            emitter.renderCommon.blendMode = VfxBlendMode::Additive;
            emitter.scale.value.fixed =
                MakeVfxVec3Constant({ 0.2f, 0.2f, 0.2f });
            emitter.rotation.eulerDegrees.mode = VfxAnimatedValueMode::Pva;
            emitter.rotation.eulerDegrees.pva.velocity =
                MakeVfxVec3Constant({ 0.0f, 0.0f, 180.0f });
            SetColorStops(emitter,
                { 0.5f, 0.3f, 1.0f, 0.8f },
                { 0.2f, 0.8f, 1.0f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildGroundRing() {
            VfxAssetData asset;
            asset.name = "Ground Ring";
            asset.durationSeconds = 2.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 2.0f;
            emitter.spawn.burstCount = 1u;
            emitter.basic.lifetime = MakeVfxFloatConstant(1.8f);
            // Magic Ring is a complete square texture, not a ring-strip atlas.
            emitter.renderShape.type = VfxRenderShapeType::Sprite;
            emitter.renderShape.billboardMode = VfxBillboardMode::Fixed;
            emitter.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            emitter.rotation.eulerDegrees.fixed =
                MakeVfxVec3Constant({ 90.0f, 0.0f, 0.0f });
            emitter.renderCommon.blendMode = VfxBlendMode::Additive;
            emitter.scale.value.mode = VfxAnimatedValueMode::Easing;
            emitter.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.3f, 0.3f, 0.3f });
            emitter.scale.value.easing.end =
                MakeVfxVec3Constant({ 1.5f, 1.5f, 1.5f });
            SetColorStops(emitter,
                { 0.4f, 1.0f, 0.7f, 0.8f },
                { 0.2f, 0.8f, 0.5f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildShockwaveRing() {
            VfxAssetData asset;
            asset.name = "Shockwave Ring";
            asset.durationSeconds = 0.6f;
            asset.loop = false;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.burstCount = 1u;
            emitter.spawn.ratePerSecond = 0.0f;
            emitter.basic.lifetime = MakeVfxFloatConstant(0.5f);
            emitter.shape.type = VfxShapeType::Point;
            emitter.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            emitter.renderShape.type = VfxRenderShapeType::Sprite;
            emitter.renderShape.billboardMode = VfxBillboardMode::Fixed;
            emitter.rotation.eulerDegrees.fixed =
                MakeVfxVec3Constant({ 90.0f, 0.0f, 0.0f });
            emitter.renderCommon.blendMode = VfxBlendMode::Additive;
            emitter.scale.value.mode = VfxAnimatedValueMode::Easing;
            emitter.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.2f, 0.2f, 0.2f });
            emitter.scale.value.easing.end =
                MakeVfxVec3Constant({ 4.0f, 4.0f, 4.0f });
            SetColorStops(emitter,
                { 0.7f, 0.9f, 1.0f, 1.0f },
                { 0.3f, 0.6f, 1.0f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildFountain() {
            VfxAssetData asset;
            asset.name = "Fountain";
            asset.durationSeconds = 3.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 60.0f;
            emitter.basic.lifetime = MakeVfxFloatConstant(1.6f);
            emitter.shape.type = VfxShapeType::Cone;
            emitter.shape.radius = MakeVfxFloatConstant(0.15f);
            emitter.shape.coneAngleDegrees = 14.0f;
            emitter.position.radialSpeed = MakeVfxFloatConstant(5.0f);
            emitter.forces.gravity = { 0.0f, -9.8f, 0.0f };
            emitter.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            emitter.scale.value.fixed =
                MakeVfxVec3Constant({ 0.05f, 0.35f, 1.0f });
            SetColorStops(emitter,
                { 0.7f, 0.85f, 1.0f, 0.9f },
                { 0.5f, 0.7f, 1.0f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

        VfxAssetData BuildSparkler() {
            VfxAssetData asset;
            asset.name = "Sparkler";
            asset.durationSeconds = 2.0f;
            asset.loop = true;
            VfxEmitterData emitter =
                MakeBaseEmitter("main", "Main");

            emitter.spawn.ratePerSecond = 120.0f;
            emitter.basic.lifetime =
                { VfxDistributionMode::RandomRange, 0.2f, 0.7f, 0u };
            emitter.position.radialSpeed =
                { VfxDistributionMode::RandomRange, 1.0f, 3.5f, 0u };
            // A Point shape has the canonical +Y emission direction. A tiny
            // sphere preserves a point-like origin while providing the
            // all-direction spark velocity promised by this preset.
            emitter.shape.type = VfxShapeType::Sphere;
            emitter.shape.radius = MakeVfxFloatConstant(0.02f);
            emitter.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            emitter.renderCommon.blendMode = VfxBlendMode::Additive;
            emitter.scale.value.fixed =
                MakeVfxVec3Constant({ 0.15f, 0.15f, 1.0f });
            SetColorStops(emitter,
                { 1.0f, 0.9f, 0.5f, 1.0f },
                { 1.0f, 0.4f, 0.0f, 0.0f });
            return MakePresetAsset(std::move(asset), { std::move(emitter) });
        }

    } // namespace

    const std::vector<VfxPresetDescriptor>& GetVfxPresetCatalog() {
        return Catalog();
    }

    const VfxPresetDescriptor* FindVfxPreset(
        std::string_view id) noexcept {

        for (const VfxPresetDescriptor& descriptor : Catalog()) {
            if (descriptor.id == id) {
                return &descriptor;
            }
        }
        return nullptr;
    }

    VfxAssetData MakeVfxPresetAsset(std::string_view id) {
        VfxAssetData asset = [&]() {
            if (id == "burst_explosion") return BuildBurstExplosion();
            if (id == "muzzle_flash") return BuildMuzzleFlash();
            if (id == "hit_impact") return BuildHitImpact();
            if (id == "projectile_trail") return BuildProjectileTrail();
            if (id == "beam") return BuildBeam();
            if (id == "smoke_plume") return BuildSmokePlume();
            if (id == "fire") return BuildFire();
            if (id == "rain") return BuildRain();
            if (id == "snow") return BuildSnow();
            if (id == "magic_aura") return BuildMagicAura();
            if (id == "ground_ring") return BuildGroundRing();
            if (id == "shockwave_ring") return BuildShockwaveRing();
            if (id == "fountain") return BuildFountain();
            if (id == "sparkler") return BuildSparkler();
            return BuildBasicSprite();
        }();
        ApplyPresetTextures(id, asset);
        return asset;
    }

    VfxPresetPackage MakeVfxPresetPackage(std::string_view id) {
        VfxPresetPackage package{};
        package.asset = MakeVfxPresetAsset(id);
        return package;
    }

} // namespace HIKARI::VFX::PRESETS
