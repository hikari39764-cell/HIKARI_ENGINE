#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {
namespace {
void FireRamp(VfxEmitterData& layer, bool ground = false) {
    auto& ramp = Effect(layer, MaterialEffectKind::Gradient, "temperature");
    ramp.colorStops = {
        {ramp.id + ".cool", 0.0f, ground ? MATH::Vec4{0.35f, 0.018f, 0.002f, 1} : MATH::Vec4{0.2f, 0.004f, 0.001f, 1}},
        {ramp.id + ".orange", 0.4f, ground ? MATH::Vec4{2.0f, 0.16f, 0.008f, 1} : MATH::Vec4{1.1f, 0.035f, 0.001f, 1}},
        {ramp.id + ".hot", 0.75f, ground ? MATH::Vec4{3.4f, 0.95f, 0.075f, 1} : MATH::Vec4{1.8f, 0.35f, 0.009f, 1}},
        {ramp.id + ".core", 1.0f, ground ? MATH::Vec4{4.0f, 2.4f, 0.65f, 1} : MATH::Vec4{2.5f, 1.1f, 0.1f, 1}}};
}
void Swirl(VfxEmitterData& layer, float rise, float vortex, float turbulence) {
    layer.forces.gravity = {0, 0.7f, 0};
    layer.forces.drag = 0.65f;
    layer.forces.field.center = {0, 1.8f, 0};
    layer.forces.field.vortex = vortex;
    layer.forces.field.attraction = 1.2f;
    layer.forces.field.turbulence = turbulence;
    layer.forces.field.frequency = 1.4f;
    layer.position.value.mode = VfxAnimatedValueMode::Pva;
    layer.position.value.pva.velocity = MakeVfxVec3Constant({0, rise, 0});
}
} // namespace

VfxAssetData FireTornado() {
    VfxAssetData asset;
    asset.name = "Fire Tornado";
    asset.durationSeconds = 6.8f;
    asset.loop = true;
    std::vector<VfxEmitterData> layers;

    // 中空の三次元形状を異なる位相で重ね、前後の炎と隙間を保持する。
    for (int i = 0; i < 3; ++i) {
        auto shell = Shape(i == 0   ? "火焰核心"
                           : i == 1 ? "螺旋火幕"
                                    : "外沿碎焰",
                           VfxRenderShapeType::MeshCylinder, 0.10f + i * 0.07f, 5.25f);
        shell.anchor = {0, -0.5f, 0};
        shell.renderShape.ringSegmentCount = 24;
        shell.renderShape.cylinderBottomRadius = 0.22f + i * 0.04f;
        shell.renderShape.cylinderTopRadius = 0.92f + i * 0.16f;
        constexpr float twist[]{450, 360, 530};
        shell.renderShape.twistDegrees = twist[i];
        shell.renderShape.bendAmount = 0.09f + i * 0.025f;
        shell.renderCommon.depthMode = VfxParticleDepthMode::SoftIntersection;
        Texture(shell, i == 1 ? "noise_fractal_2" : "flame_mask");
        shell.renderCommon.noiseTextureAssetGuid = AssetGuid{"engine.vfx.texture.distortion_seamless"};
        constexpr float tiles[]{1, 1.2f, 2};
        Flow(shell, -0.19f - i * 0.04f, 0.22f + i * 0.02f, tiles[i], 1);
        Effect(shell, MaterialEffectKind::UvDistortion, "curl").parameters = {0.045f, 1.8f, 0.4f, 0};
        Effect(shell, MaterialEffectKind::LuminanceMask, "brokenFlames").parameters = {i == 1 ? 0.25f : 0.07f, 0.28f, 0,
                                                                                       0};
        FireRamp(shell);
        auto& crown = Effect(shell, MaterialEffectKind::Opacity, "crownFade");
        crown.parameters = {};
        crown.mask = MaterialEffectMask::Vertical;
        crown.maskParameters = {0.5f, 0.5f, 0, 0.28f};
        VectorKeys(shell.scale.value, shell.id + ".build",
                   {{0, {0.25f, 0.2f, 0.25f}},
                    {0.13f, {1, 4.6f - i * 0.25f, 1}},
                    {0.76f, {1.08f, 4.9f - i * 0.25f, 1.08f}},
                    {1, {1.3f, 5.25f, 1.3f}}});
        shell.rotation.eulerDegrees.mode = VfxAnimatedValueMode::Pva;
        shell.rotation.eulerDegrees.pva.initial = MakeVfxVec3Constant({0, i * 117.0f, 0});
        shell.rotation.eulerDegrees.pva.velocity = MakeVfxVec3Constant({0, -75.0f - i * 18.0f, 0});
        constexpr float density[]{0.68f, 0.325125f, 0.204f};
        AlphaKeys(
            shell, {1, 1, 1},
            {{0, 0}, {0.12f, (0.75f - i * 0.17f) * density[i]}, {0.76f, (0.65f - i * 0.14f) * density[i]}, {1, 0}});
        if (i == 0)
            Light(shell, {1, 0.18f, 0.025f, 1}, 1.2f, 5.5f);
        layers.push_back(std::move(shell));
    }

    auto tongues = SparkBurst("上升火舌", 0.15f, 0, {1, 1, 1, 1}, 0.2f, 0.95f);
    tongues.activeDuration = 4.5f;
    tongues.spawn.ratePerSecond = 52;
    tongues.spawn.maximumParticles = 96;
    tongues.shape.type = VfxShapeType::Cylinder;
    tongues.shape.radius = MakeVfxFloatConstant(0.7f);
    tongues.shape.cylinderHeight = 3.0f;
    tongues.emitterTransform.transform.position.y = 1.85f;
    tongues.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
    tongues.renderCommon.depthMode = VfxParticleDepthMode::SoftIntersection;
    Texture(tongues, "flame_mask");
    tongues.rotation.eulerDegrees.fixed.mode = VfxDistributionMode::RandomRange;
    tongues.rotation.eulerDegrees.fixed.minimum = {0, 0, -28};
    tongues.rotation.eulerDegrees.fixed.maximum = {0, 0, 28};
    Swirl(tongues, 1.4f, 3.5f, 1.3f);
    tongues.forces.drag = 1.2f;
    VectorKeys(tongues.scale.value, tongues.id + ".lick",
               {{0, {0.1248f, 0.325f, 0.1248f}}, {0.22f, {0.468f, 1.17f, 0.468f}}, {1, {0.1768f, 0.65f, 0.1768f}}});
    AlphaKeys(tongues, {1.5f, 0.47f, 0.065f}, {{0, 0}, {0.15f, 0.39f}, {0.6f, 0.3f}, {1, 0}});
    layers.push_back(std::move(tongues));

    auto foot = Shape("地面卷吸", VfxRenderShapeType::Ring, 0, 5.5f);
    foot.emitterTransform.transform.position.y = 0.055f;
    foot.emitterTransform.transform.rotationEulerDegrees.x = -90;
    foot.renderShape.ringInnerRadius = 0.12f;
    foot.renderShape.ringOuterRadius = 1.55f;
    foot.renderCommon.depthMode = VfxParticleDepthMode::SoftIntersection;
    Texture(foot, "noise_fractal_2");
    foot.renderCommon.maskTextureAssetGuid = AssetGuid{"engine.vfx.texture.soft_line_hd"};
    Flow(foot, -0.6f, -0.32f, 4, 1.2f);
    foot.effects.back().textureTarget = MaterialEffectTextureTarget::BaseColor;
    Effect(foot, MaterialEffectKind::LuminanceMask, "wisps").parameters = {0.1f, 0.4f, 0, 0};
    FireRamp(foot, true);
    AlphaKeys(foot, {1, 1, 1}, {{0, 0}, {0.08f, 0.112f}, {0.78f, 0.084f}, {1, 0}});
    VectorKeys(foot.scale.value, foot.id + ".intake",
               {{0, {0.2f, 0.2f, 0.2f}}, {0.14f, {1, 1, 1}}, {1, {1.15f, 1.15f, 1.15f}}});
    layers.push_back(std::move(foot));

    auto embers = SparkBurst("环流余烬", 0.12f, 8, {3.5f, 0.7f, 0.045f, 1}, 0.35f, 1.8f);
    embers.activeDuration = 4.6f;
    embers.spawn.ratePerSecond = 34;
    embers.spawn.maximumParticles = 96;
    embers.shape.type = VfxShapeType::Cylinder;
    embers.shape.radius = MakeVfxFloatConstant(0.9f);
    embers.shape.cylinderHeight = 2.6f;
    embers.emitterTransform.transform.position.y = 1.65f;
    Swirl(embers, 1.8f, 8.0f, 1.8f);
    VectorKeys(embers.scale.value, embers.id + ".spark",
               {{0, {0.018f, 0.12f, 0.018f}}, {0.35f, {0.022f, 0.2f, 0.022f}}, {1, {0, 0.03f, 0}}});
    layers.push_back(std::move(embers));

    auto smoke = SparkBurst("冷却烟羽", 0.5f, 0, {0.18f, 0.12f, 0.085f, 0.3f}, 0.18f, 1.8f);
    smoke.activeDuration = 4.25f;
    smoke.spawn.ratePerSecond = 9;
    smoke.spawn.maximumParticles = 32;
    smoke.shape.radius = MakeVfxFloatConstant(0.7f);
    smoke.emitterTransform.transform.position.y = 3.1f;
    smoke.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
    smoke.renderCommon.blendMode = VfxBlendMode::Alpha;
    smoke.renderCommon.materialMode = VfxMaterialMode::StandardLit;
    smoke.renderCommon.depthMode = VfxParticleDepthMode::SoftIntersection;
    Texture(smoke, "wispy_smoke_flipbook");
    smoke.renderCommon.flipbookColumns = 8;
    smoke.renderCommon.flipbookRows = 8;
    smoke.renderCommon.flipbookFrameCount = 64;
    smoke.renderCommon.flipbookFramesPerSecond = 30;
    smoke.renderCommon.flipbookRandomStart = true;
    Swirl(smoke, 0.6f, 2.8f, 0.8f);
    VectorKeys(smoke.scale.value, smoke.id + ".cooling",
               {{0, {0.3f, 0.4f, 0.3f}}, {0.3f, {0.85f, 1.0f, 0.85f}}, {1, {1.6f, 1.4f, 1.6f}}});
    AlphaKeys(smoke, {0.22f, 0.14f, 0.095f}, {{0, 0}, {0.25f, 0.26f}, {0.65f, 0.18f}, {1, 0}});
    layers.push_back(std::move(smoke));
    return MakePresetAssetFromEmitters(std::move(asset), std::move(layers));
}
} // namespace HIKARI::VFX::PRESETS::INTERNAL
