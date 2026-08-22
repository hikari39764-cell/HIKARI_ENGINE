#include "Assets/Vfx/Internal/HIKARI_VfxComicPresetLibrary.h"

#include "Assets/Vfx/Internal/HIKARI_VfxPresetBuilder.h"

#include <utility>

namespace HIKARI::VFX::PRESETS::INTERNAL {
    namespace {
        VfxAssetData BuildComicHit() {
            VfxAssetData asset;
            asset.name = "Comic Hit";
            asset.durationSeconds = 0.34f;
            asset.loop = false;

            VfxEmitterData ink =
                MakePresetEmitterBase("ink", "Ink Backing");
            ink.spawn.ratePerSecond = 0.0f;
            ink.spawn.burstCount = 1u;
            ink.basic.lifetime = MakeVfxFloatConstant(0.16f);
            ink.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            ink.renderCommon.blendMode = VfxBlendMode::Alpha;
            ink.renderCommon.depthMode =
                VfxParticleDepthMode::WorldOccluded;
            ink.scale.value.mode = VfxAnimatedValueMode::Easing;
            ink.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.72f, 0.72f, 0.72f });
            ink.scale.value.easing.end =
                MakeVfxVec3Constant({ 1.52f, 1.52f, 1.52f });
            SetPresetColorStops(ink,
                { 0.015f, 0.020f, 0.040f, 0.96f },
                { 0.015f, 0.020f, 0.040f, 0.0f });

            VfxEmitterData color =
                MakePresetEmitterBase("color", "Graphic Color Burst");
            color.spawn.ratePerSecond = 0.0f;
            color.spawn.burstCount = 1u;
            color.startDelay = 0.012f;
            color.basic.lifetime = MakeVfxFloatConstant(0.13f);
            color.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            color.renderCommon.blendMode = VfxBlendMode::Alpha;
            color.renderCommon.depthMode =
                VfxParticleDepthMode::WorldOccluded;
            color.scale.value.mode = VfxAnimatedValueMode::Easing;
            color.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.54f, 0.54f, 0.54f });
            color.scale.value.easing.end =
                MakeVfxVec3Constant({ 1.28f, 1.28f, 1.28f });
            SetPresetColorStops(color,
                { 0.96f, 0.19f, 0.08f, 1.0f },
                { 1.0f, 0.62f, 0.10f, 0.0f });

            VfxEmitterData shards =
                MakePresetEmitterBase("shards", "Quadrilateral Shards");
            shards.spawn.ratePerSecond = 0.0f;
            shards.spawn.burstCount = 14u;
            shards.spawn.maximumParticles = 128u;
            shards.startDelay = 0.018f;
            shards.basic.lifetime = {
                VfxDistributionMode::RandomRange, 0.14f, 0.30f, 0u };
            shards.shape.type = VfxShapeType::Sphere;
            shards.shape.radius = MakeVfxFloatConstant(0.06f);
            shards.position.radialSpeed = {
                VfxDistributionMode::RandomRange, 2.4f, 5.4f, 0u };
            shards.renderCommon.blendMode = VfxBlendMode::Alpha;
            shards.renderCommon.depthMode =
                VfxParticleDepthMode::WorldOccluded;
            shards.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            shards.scale.value.fixed =
                MakeVfxVec3Constant({ 0.10f, 0.48f, 1.0f });
            SetPresetColorStops(shards,
                { 1.0f, 0.72f, 0.12f, 1.0f },
                { 0.48f, 0.03f, 0.02f, 0.0f });
            return MakePresetAssetFromEmitters(std::move(asset),
                { std::move(ink), std::move(color), std::move(shards) });
        }

        VfxAssetData BuildComicSlash() {
            VfxAssetData asset;
            asset.name = "Comic Slash";
            asset.durationSeconds = 0.42f;
            asset.loop = false;

            VfxEmitterData ink =
                MakePresetEmitterBase("ink", "Slash Ink Backing");
            ink.spawn.ratePerSecond = 0.0f;
            ink.spawn.burstCount = 1u;
            ink.basic.lifetime = MakeVfxFloatConstant(0.22f);
            ink.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            ink.renderCommon.blendMode = VfxBlendMode::Alpha;
            ink.renderCommon.depthMode =
                VfxParticleDepthMode::WorldOccluded;
            ink.scale.value.mode = VfxAnimatedValueMode::Easing;
            ink.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.80f, 0.80f, 0.80f });
            ink.scale.value.easing.end =
                MakeVfxVec3Constant({ 1.64f, 1.64f, 1.64f });
            SetPresetColorStops(ink,
                { 0.01f, 0.02f, 0.05f, 0.94f },
                { 0.01f, 0.02f, 0.05f, 0.0f });

            VfxEmitterData body =
                MakePresetEmitterBase("body", "Slash Color Body");
            body.spawn.ratePerSecond = 0.0f;
            body.spawn.burstCount = 1u;
            body.startDelay = 0.014f;
            body.basic.lifetime = MakeVfxFloatConstant(0.18f);
            body.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            body.renderCommon.blendMode = VfxBlendMode::Alpha;
            body.renderCommon.depthMode =
                VfxParticleDepthMode::WorldOccluded;
            body.scale.value.mode = VfxAnimatedValueMode::Easing;
            body.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.67f, 0.67f, 0.67f });
            body.scale.value.easing.end =
                MakeVfxVec3Constant({ 1.45f, 1.45f, 1.45f });
            SetPresetColorStops(body,
                { 0.08f, 0.82f, 1.0f, 1.0f },
                { 0.14f, 0.26f, 1.0f, 0.0f });

            VfxEmitterData shards =
                MakePresetEmitterBase("shards", "Slash Quad Shards");
            shards.spawn.ratePerSecond = 0.0f;
            shards.spawn.burstCount = 10u;
            shards.spawn.maximumParticles = 128u;
            shards.startDelay = 0.035f;
            shards.basic.lifetime = {
                VfxDistributionMode::RandomRange, 0.16f, 0.32f, 0u };
            shards.shape.type = VfxShapeType::Edge;
            shards.shape.edgeLength = 0.42f;
            shards.position.radialSpeed = {
                VfxDistributionMode::RandomRange, 1.8f, 4.2f, 0u };
            shards.renderCommon.blendMode = VfxBlendMode::Alpha;
            shards.renderShape.billboardMode =
                VfxBillboardMode::DirectionalBillboard;
            shards.scale.value.fixed =
                MakeVfxVec3Constant({ 0.09f, 0.42f, 1.0f });
            SetPresetColorStops(shards,
                { 0.82f, 0.96f, 1.0f, 0.96f },
                { 0.08f, 0.22f, 0.90f, 0.0f });
            return MakePresetAssetFromEmitters(std::move(asset),
                { std::move(ink), std::move(body), std::move(shards) });
        }

        VfxAssetData BuildComicGroundSlam() {
            VfxAssetData asset;
            asset.name = "Comic Ground Slam";
            asset.durationSeconds = 0.58f;
            asset.loop = false;

            VfxEmitterData cracks =
                MakePresetEmitterBase("cracks", "Graphic Ground Cracks");
            cracks.spawn.ratePerSecond = 0.0f;
            cracks.spawn.burstCount = 1u;
            cracks.basic.lifetime = MakeVfxFloatConstant(0.46f);
            cracks.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            cracks.renderCommon.blendMode = VfxBlendMode::Alpha;
            cracks.renderCommon.depthMode =
                VfxParticleDepthMode::ContactSurface;
            cracks.renderShape.billboardMode = VfxBillboardMode::Fixed;
            cracks.rotation.eulerDegrees.fixed =
                MakeVfxVec3Constant({ 90.0f, 0.0f, 0.0f });
            cracks.emitterTransform.transform.position.y = 0.018f;
            cracks.scale.value.mode = VfxAnimatedValueMode::Easing;
            cracks.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.42f, 0.42f, 0.42f });
            cracks.scale.value.easing.end =
                MakeVfxVec3Constant({ 3.60f, 3.60f, 3.60f });
            SetPresetColorStops(cracks,
                { 0.018f, 0.022f, 0.045f, 0.92f },
                { 0.018f, 0.022f, 0.045f, 0.0f });

            VfxEmitterData ring =
                MakePresetEmitterBase("ring", "Broken Color Ring");
            ring.spawn.ratePerSecond = 0.0f;
            ring.spawn.burstCount = 1u;
            ring.startDelay = 0.018f;
            ring.basic.lifetime = MakeVfxFloatConstant(0.28f);
            ring.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            ring.renderCommon.blendMode = VfxBlendMode::Alpha;
            ring.renderCommon.depthMode =
                VfxParticleDepthMode::ContactSurface;
            ring.renderShape.billboardMode = VfxBillboardMode::Fixed;
            ring.rotation.eulerDegrees.fixed =
                MakeVfxVec3Constant({ 90.0f, 0.0f, 0.0f });
            ring.emitterTransform.transform.position.y = 0.026f;
            ring.scale.value.mode = VfxAnimatedValueMode::Easing;
            ring.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.48f, 0.48f, 0.48f });
            ring.scale.value.easing.end =
                MakeVfxVec3Constant({ 4.55f, 4.55f, 4.55f });
            SetPresetColorStops(ring,
                { 1.0f, 0.28f, 0.06f, 0.98f },
                { 1.0f, 0.72f, 0.08f, 0.0f });

            VfxEmitterData lines =
                MakePresetEmitterBase("lines", "Ground Compression Lines");
            lines.spawn.ratePerSecond = 0.0f;
            lines.spawn.burstCount = 1u;
            lines.startDelay = 0.032f;
            lines.basic.lifetime = MakeVfxFloatConstant(0.18f);
            lines.position.radialSpeed = MakeVfxFloatConstant(0.0f);
            lines.renderCommon.blendMode = VfxBlendMode::Alpha;
            lines.renderCommon.depthMode =
                VfxParticleDepthMode::WorldOccluded;
            lines.renderShape.billboardMode = VfxBillboardMode::Fixed;
            lines.rotation.eulerDegrees.fixed =
                MakeVfxVec3Constant({ 90.0f, 0.0f, 0.0f });
            lines.emitterTransform.transform.position.y = 0.032f;
            lines.scale.value.mode = VfxAnimatedValueMode::Easing;
            lines.scale.value.easing.start =
                MakeVfxVec3Constant({ 0.76f, 0.76f, 0.76f });
            lines.scale.value.easing.end =
                MakeVfxVec3Constant({ 4.90f, 4.90f, 4.90f });
            SetPresetColorStops(lines,
                { 1.0f, 0.82f, 0.18f, 0.92f },
                { 0.84f, 0.08f, 0.02f, 0.0f });
            return MakePresetAssetFromEmitters(std::move(asset),
                { std::move(cracks), std::move(ring), std::move(lines) });
        }
    }

    bool IsComicPresetId(std::string_view id) noexcept {
        return id == "comic_hit" ||
            id == "comic_slash" ||
            id == "comic_ground_slam";
    }

    VfxAssetData BuildComicPreset(std::string_view id) {
        if (id == "comic_hit") return BuildComicHit();
        if (id == "comic_slash") return BuildComicSlash();
        return BuildComicGroundSlam();
    }

    bool ApplyComicPresetTextures(
        std::string_view id,
        VfxAssetData& asset) {

        if (!IsComicPresetId(id) || asset.emitters.size() < 3u) {
            return false;
        }
        const auto setTexture = [&asset](size_t index, const char* textureId) {
            asset.emitters[index].renderCommon.materialMode =
                VfxMaterialMode::StandardUnlit;
            asset.emitters[index].renderCommon.colorTextureAssetGuid =
                AssetGuid{ textureId };
        };
        if (id == "comic_hit") {
            setTexture(0u, "engine.vfx.texture.comic_burst");
            setTexture(1u, "engine.vfx.texture.comic_burst");
            setTexture(2u, "engine.vfx.texture.comic_quad_shard");
        } else if (id == "comic_slash") {
            setTexture(0u, "engine.vfx.texture.comic_slash_wedge");
            setTexture(1u, "engine.vfx.texture.comic_slash_wedge");
            setTexture(2u, "engine.vfx.texture.comic_quad_shard");
        } else {
            setTexture(0u, "engine.vfx.texture.comic_ground_crack");
            setTexture(1u, "engine.vfx.texture.comic_broken_ring");
            setTexture(2u, "engine.vfx.texture.comic_speed_lines");
        }
        return true;
    }

} // namespace HIKARI::VFX::PRESETS::INTERNAL
