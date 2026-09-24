#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {
namespace {
VfxEmitterData Smoke(std::string name, float start, float duration) {
    auto layer = SparkBurst(std::move(name), start, 0, {0.3f, 0.34f, 0.4f, 0.38f}, 0.35f, 1.5f);
    layer.activeDuration = duration;
    layer.spawn.ratePerSecond = 12;
    layer.spawn.maximumParticles = 48;
    layer.shape.type = VfxShapeType::Cone;
    layer.shape.radius = MakeVfxFloatConstant(0.16f);
    layer.shape.coneAngleDegrees = 24;
    layer.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
    layer.renderCommon.materialMode = VfxMaterialMode::StandardLit;
    layer.renderCommon.blendMode = VfxBlendMode::Alpha;
    layer.renderCommon.depthMode = VfxParticleDepthMode::SoftIntersection;
    Texture(layer, "wispy_smoke_flipbook");
    layer.renderCommon.flipbookColumns = 8;
    layer.renderCommon.flipbookRows = 8;
    layer.renderCommon.flipbookFrameCount = 64;
    layer.renderCommon.flipbookFramesPerSecond = 32;
    layer.renderCommon.flipbookLoop = false;
    layer.forces.gravity = {0, 0.35f, 0};
    layer.forces.drag = 0.5f;
    VectorKeys(layer.scale.value, layer.id + ".expansion",
               {{0, {0.2f, 0.2f, 0.2f}}, {0.35f, {0.7f, 0.7f, 0.7f}}, {1, {1.25f, 1.25f, 1.25f}}});
    AlphaKeys(layer, {0.4f, 0.44f, 0.5f}, {{0, 0}, {0.12f, 0.4f}, {0.55f, 0.25f}, {1, 0}});
    return layer;
}
VfxEmitterData GroundRing(std::string name, float start, float duration, float width) {
    auto layer = Shape(std::move(name), VfxRenderShapeType::Ring, start, duration);
    layer.emitterTransform.transform.position.y = 0.03f;
    layer.emitterTransform.transform.rotationEulerDegrees.x = -90;
    layer.renderShape.ringInnerRadius = 1 - width;
    layer.renderShape.ringOuterRadius = 1;
    layer.renderCommon.depthMode = VfxParticleDepthMode::SoftIntersection;
    return layer;
}
} // namespace
VfxAssetData ParticlePreset(std::string_view id) {
    VfxAssetData asset;
    asset.durationSeconds = id == "smoke" || id == "motes" ? 3.6f : 2.4f;
    asset.loop = true;
    std::vector<VfxEmitterData> layers;
    if (id == "smoke") {
        asset.name = "Drifting Smoke";
        layers.push_back(Smoke("Soft smoke", 0, 1.5f));
    } else if (id == "motes") {
        asset.name = "Suspended Motes";
        auto layer = SparkBurst("Suspended particles", 0, 16, {0.16f, 0.72f, 1.5f, 0.8f}, 0.1f, 1.8f);
        layer.activeDuration = 1.8f;
        layer.spawn.ratePerSecond = 15;
        layer.spawn.maximumParticles = 64;
        layer.shape.radius = MakeVfxFloatConstant(1.2f);
        layer.emitterTransform.transform.position.y = 1.0f;
        layer.forces.gravity = {0, 0.09f, 0};
        layer.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
        Texture(layer, "particle_soft");
        VectorKeys(layer.scale.value, layer.id + ".size",
                   {{0, {0.005f, 0.005f, 0.005f}}, {0.2f, {0.045f, 0.045f, 0.045f}}, {1, {0.008f, 0.008f, 0.008f}}});
        AlphaKeys(layer, {0.18f, 0.9f, 1.8f}, {{0, 0}, {0.3f, 0.7f}, {0.7f, 0.5f}, {1, 0}});
        layers.push_back(std::move(layer));
    } else if (id == "debris") {
        asset.name = "Stone Fragments";
        auto layer = SparkBurst("Angular fragments", 0.03f, 18, {0.28f, 0.22f, 0.17f, 1}, 3.6f, 1.6f);
        layer.emitterTransform.transform.position.y = 0.15f;
        layer.shape.type = VfxShapeType::Cone;
        layer.shape.coneAngleDegrees = 64;
        layer.renderShape.type = VfxRenderShapeType::MeshBox;
        layer.renderCommon.colorTextureAssetGuid = {};
        layer.renderCommon.materialMode = VfxMaterialMode::StandardLit;
        layer.renderCommon.blendMode = VfxBlendMode::Alpha;
        layer.forces.gravity = {0, -9.8f, 0};
        layer.forces.drag = 0.35f;
        layer.forces.collisionPlaneEnabled = true;
        layer.forces.restitution = 0.32f;
        layer.scale.value.mode = VfxAnimatedValueMode::Fixed;
        layer.scale.value.fixed = MakeVfxVec3Constant({0.08f, 0.05f, 0.12f});
        layer.rotation.eulerDegrees.mode = VfxAnimatedValueMode::Pva;
        layer.rotation.eulerDegrees.pva.velocity = MakeVfxVec3Constant({240, 170, 110});
        layers.push_back(std::move(layer));
        auto dust = Smoke("Fine dust", 0.05f, 0.20f);
        dust.spawn.burstTimeline = {{dust.spawn.id + ".burst", 0, 6}};
        dust.spawn.ratePerSecond = 0;
        layers.push_back(std::move(dust));
    } else {
        asset.name = "Hot Sparks";
        auto layer = SparkBurst("Ballistic sparks", 0.04f, 40, {3.0f, 0.85f, 0.12f, 1}, 4.5f, 0.9f);
        layer.shape.type = VfxShapeType::Cone;
        layer.shape.coneAngleDegrees = 65;
        layer.forces.gravity = {0, -6.5f, 0};
        layer.forces.collisionPlaneEnabled = true;
        layer.forces.restitution = 0.2f;
        layers.push_back(std::move(layer));
        // 瞬間発生の後にも少数の残火を残す。
        auto residual = SparkBurst("Residual embers", 0.05f, 8, {1.0f, 0.18f, 0.035f, 0.65f}, 1.6f, 1.65f);
        Texture(residual, "particle_soft");
        residual.scale.value.mode = VfxAnimatedValueMode::Fixed;
        residual.scale.value.fixed = MakeVfxVec3Constant({0.028f, 0.028f, 0.028f});
        residual.emitterTransform.transform.position.y = 0.4f;
        layers.push_back(std::move(residual));
    }
    return MakePresetAssetFromEmitters(std::move(asset), std::move(layers));
}
VfxAssetData SupportingComposition(std::string_view id) {
    VfxAssetData asset;
    asset.name = std::string(id);
    asset.durationSeconds = 1.8f;
    asset.loop = true;
    std::vector<VfxEmitterData> layers;
    if (id == "shockwave" || id == "impact") {
        const bool impact = id == "impact";
        asset.durationSeconds = 1.3f;
        for (int i = 0; i < 2; ++i) {
            auto wave =
                GroundRing(i ? "Broken wake" : "Pressure front", 0.025f * i, i ? 0.8f : 0.55f, i ? 0.24f : 0.07f);
            Texture(wave, i ? "noise_fractal_3" : "soft_line");
            if (i) {
                Flow(wave, 0.25f, 0.12f, 3, 1);
                Erode(wave, 0.1f, 0.85f);
            } else
                wave.renderShape.uvSwapAxes = true;
            const float end = impact ? 1.7f : 2.6f;
            VectorKeys(
                wave.scale.value, wave.id + ".spread",
                {{0, {0.12f, 0.12f, 0.12f}}, {0.3f, {end * 0.65f, end * 0.65f, end * 0.65f}}, {1, {end, end, end}}});
            AlphaKeys(wave, impact ? MATH::Vec3{2.5f, 0.65f, 0.12f} : MATH::Vec3{0.1f, 0.8f, 1.8f},
                      {{0, 0}, {0.04f, 1}, {0.5f, 0.45f}, {1, 0}});
            layers.push_back(std::move(wave));
        }
        auto burst = SparkBurst("Expelled fragments", 0.01f, impact ? 32 : 18,
                                impact ? MATH::Vec4{3, 0.8f, 0.15f, 1} : MATH::Vec4{0.2f, 0.9f, 2, 1},
                                impact ? 4.3f : 2.5f, 0.85f);
        burst.shape.type = VfxShapeType::Cone;
        burst.shape.coneAngleDegrees = 78;
        layers.push_back(std::move(burst));
        auto flash = Shape("Impact flash", VfxRenderShapeType::Sprite, 0, 0.2f);
        flash.emitterTransform.transform.position.y = 0.3f;
        flash.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
        Texture(flash, "star_glint");
        VectorKeys(flash.scale.value, flash.id + ".flash",
                   {{0, {0.1f, 0.1f, 0.1f}}, {0.15f, {1.3f, 1.3f, 1.3f}}, {1, {0.1f, 0.1f, 0.1f}}});
        AlphaKeys(flash, {3.5f, 1.6f, 0.55f}, {{0, 1}, {0.2f, 0.7f}, {1, 0}});
        Light(flash, {1, 0.35f, 0.1f, 1}, 1.2f, 3);
        layers.push_back(std::move(flash));
        auto dust = Smoke("Expanding dust", 0.06f, 0.18f);
        dust.spawn.burstTimeline = {{dust.spawn.id + ".burst", 0, 8}};
        dust.spawn.ratePerSecond = 0;
        layers.push_back(std::move(dust));
    } else if (id == "shield") {
        auto shell = Shape("Translucent shield", VfxRenderShapeType::MeshHemisphere, 0, 1.8f);
        shell.renderCommon.blendMode = VfxBlendMode::Alpha;
        shell.scale.value.fixed = MakeVfxVec3Constant({3, 3, 3});
        auto& rim = Effect(shell, MaterialEffectKind::Fresnel, "rim");
        rim.parameters = {3, 0.7f, 0.82f, 0};
        rim.colorA = {0.05f, 0.45f, 1, 1};
        AlphaKeys(shell, {0.12f, 0.55f, 1.1f}, {{0, 0}, {0.12f, 0.24f}, {0.76f, 0.24f}, {1, 0}});
        layers.push_back(std::move(shell));
        auto base = GroundRing("Shield contact", 0.02f, 1.7f, 0.025f);
        base.scale.value.fixed = MakeVfxVec3Constant({1.5f, 1.5f, 1.5f});
        Texture(base, "soft_line");
        base.renderShape.uvSwapAxes = true;
        AlphaKeys(base, {0.08f, 0.6f, 1.5f}, {{0, 0}, {0.1f, 0.8f}, {0.85f, 0.55f}, {1, 0}});
        layers.push_back(std::move(base));
        auto motes = ParticlePreset("motes").emitters.front();
        motes.emitterTransform.transform.position.y = 0.6f;
        motes.shape.radius = MakeVfxFloatConstant(1.35f);
        layers.push_back(std::move(motes));
    } else if (id == "charge") {
        for (int i = 0; i < 3; ++i) {
            auto ring = Shape("Converging orbit " + std::to_string(i), VfxRenderShapeType::Ring, 0.07f * i, 1.15f);
            ring.emitterTransform.transform.position.y = 1.1f;
            ring.emitterTransform.transform.rotationEulerDegrees = {float(35 + i * 40), float(20 + i * 55),
                                                                    float(i * 35)};
            ring.renderShape.ringInnerRadius = 0.94f;
            ring.renderShape.ringOuterRadius = 1;
            ring.renderShape.ringStartAngleDegrees = 0;
            ring.renderShape.ringEndAngleDegrees = 245;
            ring.renderShape.taperStart = 0.04f;
            ring.renderShape.uvSwapAxes = true;
            Texture(ring, "soft_line");
            VectorKeys(ring.scale.value, ring.id + ".collapse",
                       {{0, {1.4f, 1.4f, 1.4f}}, {0.7f, {0.7f, 0.7f, 0.7f}}, {1, {0.06f, 0.06f, 0.06f}}});
            VectorKeys(ring.rotation.eulerDegrees, ring.id + ".turn", {{0, {0, 0, 0}}, {1, {0, 0, 240}}});
            AlphaKeys(ring, {0.3f, 2.4f, 5.0f}, {{0, 0}, {0.18f, 0.8f}, {0.85f, 1}, {1, 0}});
            layers.push_back(std::move(ring));
        }
        auto core = Shape("Condensed core", VfxRenderShapeType::Sprite, 0.15f, 1.35f);
        core.emitterTransform.transform.position.y = 1.1f;
        core.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
        Texture(core, "star_glint");
        VectorKeys(core.scale.value, core.id + ".build",
                   {{0, {0.04f, 0.04f, 0.04f}},
                    {0.65f, {0.36f, 0.36f, 0.36f}},
                    {0.8f, {1.05f, 1.05f, 1.05f}},
                    {1, {0, 0, 0}}});
        AlphaKeys(core, {1.6f, 2.9f, 4.0f}, {{0, 0}, {0.6f, 0.5f}, {0.8f, 1}, {1, 0}});
        Light(core, {0.2f, 0.55f, 1, 1}, 1, 3);
        layers.push_back(std::move(core));
    } else if (id == "flame_jet") {
        asset.durationSeconds = 3.0f;
        auto fire = SparkBurst("Advected flame tongues", 0, 0, {1.3f, 0.55f, 0.12f, 1}, 4.0f, 0.65f);
        fire.activeDuration = 1.1f;
        fire.spawn.ratePerSecond = 38;
        fire.spawn.maximumParticles = 64;
        fire.shape.type = VfxShapeType::Cone;
        fire.shape.radius = MakeVfxFloatConstant(0.06f);
        fire.shape.coneAngleDegrees = 11;
        fire.emitterTransform.transform.position = {-1.2f, 0.7f, 0};
        fire.emitterTransform.transform.rotationEulerDegrees = {0, 0, -90};
        fire.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
        fire.forces.gravity = {0, 0.7f, 0};
        fire.forces.drag = 0.25f;
        Texture(fire, "flame_flipbook");
        fire.renderCommon.flipbookColumns = 16;
        fire.renderCommon.flipbookRows = 4;
        fire.renderCommon.flipbookFrameCount = 64;
        fire.renderCommon.flipbookFramesPerSecond = 72;
        fire.renderCommon.flipbookRandomStart = true;
        VectorKeys(fire.scale.value, fire.id + ".growth",
                   {{0, {0.15f, 0.27f, 0.15f}}, {0.3f, {0.48f, 0.74f, 0.48f}}, {1, {0.85f, 1.1f, 0.85f}}});
        AlphaKeys(fire, {1.8f, 0.50f, 0.075f}, {{0, 0}, {0.12f, 0.85f}, {0.55f, 0.65f}, {1, 0}});
        layers.push_back(std::move(fire));
        auto smoke = Smoke("Cooling smoke", 0.25f, 1.0f);
        smoke.emitterTransform.transform.position = {0.7f, 0.85f, 0};
        smoke.emitterTransform.transform.rotationEulerDegrees = {0, 0, -65};
        smoke.position.radialSpeed = MakeVfxFloatConstant(0.9f);
        layers.push_back(std::move(smoke));
        auto embers = SparkBurst("Carried embers", 0.12f, 5, {2.2f, 0.5f, 0.07f, 0.7f}, 3.5f, 0.9f);
        embers.activeDuration = 1.1f;
        embers.spawn.ratePerSecond = 12;
        embers.spawn.maximumParticles = 32;
        embers.emitterTransform.transform.position = {-1.2f, 0.7f, 0};
        embers.emitterTransform.transform.rotationEulerDegrees = {0, 0, -90};
        embers.shape.type = VfxShapeType::Cone;
        embers.shape.coneAngleDegrees = 15;
        embers.forces.gravity = {0, 0.5f, 0};
        layers.push_back(std::move(embers));
    } else {
        auto trail = Shape("Spatial ribbon", VfxRenderShapeType::Ribbon, 0, 1.1f);
        trail.history = {VfxHistoryMode::Trail, 0.38f, 16u, 5};
        trail.renderShape.ribbonWidth = 0.32f;
        Texture(trail, "noise_fractal_2");
        Flow(trail, -1.5f, 0.2f, 2, 0.65f);
        trail.renderCommon.maskTextureAssetGuid = AssetGuid{"engine.vfx.texture.gradient_falloff"};
        VectorKeys(trail.position.value, trail.id + ".path",
                   {{0, {-1.8f, 0.3f, -0.6f}},
                    {0.2f, {-1.2f, 1.4f, 0.6f}},
                    {0.45f, {0.0f, 0.65f, 1}},
                    {0.7f, {1, 1.7f, 0}},
                    {1, {1.6f, 0.9f, -1}}});
        VectorKeys(trail.rotation.eulerDegrees, trail.id + ".bank",
                   {{0, {0, 0, -30}}, {0.4f, {50, 20, 30}}, {1, {-30, 40, 110}}});
        AlphaKeys(trail, {0.08f, 0.85f, 1.8f}, {{0, 0.3f}, {0.25f, 0.85f}, {0.8f, 0.75f}, {1, 0}});
        layers.push_back(std::move(trail));
    }
    return MakePresetAssetFromEmitters(std::move(asset), std::move(layers));
}
} // namespace HIKARI::VFX::PRESETS::INTERNAL
