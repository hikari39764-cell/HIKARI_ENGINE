#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {
namespace {
constexpr float kMuzzle = -2.65f;
constexpr float kLength = 5.3f;
VfxEmitterData Tube(std::string name, float width, float start, float duration) {
    auto layer = Shape(std::move(name), VfxRenderShapeType::MeshCylinder, start, duration);
    layer.emitterTransform.transform.position = {kMuzzle, 1.1f, 0};
    layer.emitterTransform.transform.rotationEulerDegrees = {0, 0, -90};
    layer.anchor = {0, -0.5f, 0};
    layer.renderShape.ringSegmentCount = 24;
    layer.renderShape.taperStart = 0.85f;
    layer.renderShape.taperEnd = 0.95f;
    VectorKeys(layer.scale.value, layer.id + ".envelope",
               {{0, {width * 0.12f, 0.08f, width * 0.12f}},
                {0.045f, {width, kLength, width}},
                {0.13f, {width * 0.76f, kLength, width * 0.76f}},
                {0.72f, {width * 0.8f, kLength, width * 0.8f}},
                {0.86f, {width * 0.48f, kLength, width * 0.48f}},
                {1, {0.001f, kLength, 0.001f}}});
    return layer;
}
VfxEmitterData Disc(std::string name, float x, float start, float duration) {
    auto layer = Shape(std::move(name), VfxRenderShapeType::Sprite, start, duration);
    layer.emitterTransform.transform.position = {x, 1.1f, 0};
    layer.renderShape.billboardMode = VfxBillboardMode::RotatedBillboard;
    return layer;
}
} // namespace
VfxAssetData Beam() {
    VfxAssetData asset;
    asset.name = "Ion Lance";
    asset.durationSeconds = 2.2f;
    asset.loop = true;
    std::vector<VfxEmitterData> layers;

    auto charge = Disc("Muzzle condensation", kMuzzle, 0, 0.46f);
    Texture(charge, "glow_halo");
    VectorKeys(charge.scale.value, charge.id + ".collapse",
               {{0, {1.2f, 1.2f, 1.2f}}, {0.6f, {0.65f, 0.65f, 0.65f}}, {1, {0.08f, 0.08f, 0.08f}}});
    AlphaKeys(charge, {0.15f, 0.65f, 1.8f}, {{0, 0}, {0.25f, 0.32f}, {0.85f, 0.65f}, {1, 0}});
    layers.push_back(std::move(charge));
    for (int i = 0; i < 3; ++i) {
        auto orbit = Shape("Charge orbit " + std::to_string(i + 1), VfxRenderShapeType::Ring, 0.025f * i, 0.39f);
        orbit.emitterTransform.transform.position = {kMuzzle, 1.1f, 0};
        orbit.emitterTransform.transform.rotationEulerDegrees = {float(35 * i), float(65 + 25 * i), float(40 * i)};
        orbit.renderShape.ringInnerRadius = 0.95f;
        orbit.renderShape.ringOuterRadius = 1;
        orbit.renderShape.ringStartAngleDegrees = 10 + 70.0f * i;
        orbit.renderShape.ringEndAngleDegrees = 155 + 70.0f * i;
        orbit.renderShape.uvSwapAxes = true;
        Texture(orbit, "soft_line");
        VectorKeys(orbit.scale.value, orbit.id + ".collapse", {{0, {0.8f, 0.8f, 0.8f}}, {1, {0.09f, 0.09f, 0.09f}}});
        VectorKeys(orbit.rotation.eulerDegrees, orbit.id + ".turn", {{0, {0, 0, 0}}, {1, {0, 0, 100}}});
        AlphaKeys(orbit, {0.25f, 1.25f, 2.3f}, {{0, 0}, {0.2f, 0.8f}, {0.8f, 0.8f}, {1, 0}});
        layers.push_back(std::move(orbit));
    }

    // 極細の縦線テクスチャを円筒へ巻くと斜め視点で点列になる。
    // 核は連続した体積とし、移流は外殻とフィラメントで表現する。
    auto core = Tube("Coherent beam core", 0.12f, 0.39f, 1.23f);
    AlphaKeys(core, {1.3f, 3.2f, 4.4f}, {{0, 0}, {0.025f, 1}, {0.15f, 0.8f}, {0.75f, 0.85f}, {1, 0}});
    layers.push_back(std::move(core));

    auto body = Tube("Advected energy sheath", 0.64f, 0.40f, 1.30f);
    Texture(body, "noise_fractal_1");
    body.renderCommon.maskTextureAssetGuid = AssetGuid{"engine.vfx.texture.soft_line_hd"};
    Flow(body, -0.23f, 1.7f, 2.3f, 0.4f);
    Effect(body, MaterialEffectKind::UvDistortion, "shear").parameters = {0.10f, 2.6f, 1.1f, 0};
    body.renderCommon.noiseTextureAssetGuid = AssetGuid{"engine.vfx.texture.distortion_seamless"};
    body.renderCommon.flowMapTextureAssetGuid = AssetGuid{"engine.vfx.texture.flow_vortex"};
    Effect(body, MaterialEffectKind::FlowMap, "rolling energy").parameters = {0.09f, 1.4f, 1.2f, 1};
    Effect(body, MaterialEffectKind::LuminanceMask, "gaps").parameters = {0.30f, 0.18f, 0, 0};
    auto& rim = Effect(body, MaterialEffectKind::Fresnel, "edge density");
    rim.parameters = {1.5f, 0.12f, 0.35f, 0};
    rim.colorA = {0.04f, 0.3f, 0.8f, 1};
    AlphaKeys(body, {0.03f, 0.75f, 3.2f}, {{0, 0}, {0.04f, 0.7f}, {0.65f, 0.62f}, {1, 0}});
    layers.push_back(std::move(body));

    for (int i = 0; i < 2; ++i) {
        auto helix = Tube("Spiral filament " + std::to_string(i + 1), 0.86f + 0.21f * i, 0.42f + 0.015f * i, 1.18f);
        Texture(helix, "energy_streak");
        helix.renderCommon.maskTextureAssetGuid = AssetGuid{"engine.vfx.texture.soft_line_hd"};
        helix.renderShape.twistDegrees = i == 0 ? 150.0f : -170.0f;
        helix.renderShape.ringSegmentCount = 8;
        helix.rotation.eulerDegrees.fixed = MakeVfxVec3Constant({0, 115.0f * i, 0});
        Flow(helix, i == 0 ? 0.55f : -0.48f, 0.85f, 1, 1);
        AlphaKeys(helix, {0.14f, 1.5f, 4.2f}, {{0, 0}, {0.055f, 0.55f}, {0.6f, 0.38f}, {1, 0}});
        layers.push_back(std::move(helix));
    }

    auto flash = Disc("Muzzle corona", kMuzzle, 0.37f, 1.26f);
    Texture(flash, "star_glint");
    VectorKeys(flash.scale.value, flash.id + ".flash",
               {{0, {0.05f, 0.05f, 0.05f}},
                {0.035f, {1.35f, 1.35f, 1.35f}},
                {0.15f, {0.52f, 0.52f, 0.52f}},
                {0.75f, {0.65f, 0.65f, 0.65f}},
                {1, {0.03f, 0.03f, 0.03f}}});
    AlphaKeys(flash, {2.8f, 4.0f, 4.5f}, {{0, 0}, {0.025f, 1}, {0.2f, 0.7f}, {0.75f, 0.65f}, {1, 0}});
    Light(flash, {0.25f, 0.6f, 1, 1}, 1.8f, 3.5f);
    layers.push_back(std::move(flash));

    auto impact = Disc("Focused contact", -kMuzzle, 0.44f, 1.26f);
    Texture(impact, "star_glint");
    VectorKeys(impact.scale.value, impact.id + ".pulse",
               {{0, {0.08f, 0.08f, 0.08f}},
                {0.045f, {0.95f, 0.95f, 0.95f}},
                {0.18f, {0.6f, 0.6f, 0.6f}},
                {0.72f, {0.68f, 0.68f, 0.68f}},
                {1, {0.01f, 0.01f, 0.01f}}});
    AlphaKeys(impact, {1.8f, 2.7f, 3.3f}, {{0, 0}, {0.035f, 1}, {0.72f, 0.75f}, {1, 0}});
    Light(impact, {0.3f, 0.65f, 1, 1}, 2.2f, 3.8f);
    layers.push_back(std::move(impact));

    auto corona = Shape("Contact pressure ring", VfxRenderShapeType::Ring, 0.43f, 0.42f);
    corona.emitterTransform.transform.position = {-kMuzzle, 1.1f, 0};
    corona.emitterTransform.transform.rotationEulerDegrees = {0, 90, 0};
    corona.renderShape.ringInnerRadius = 0.955f;
    corona.renderShape.ringOuterRadius = 1;
    corona.renderShape.uvSwapAxes = true;
    Texture(corona, "energy_streak");
    VectorKeys(corona.scale.value, corona.id + ".expansion", {{0, {0.12f, 0.12f, 0.12f}}, {1, {1.2f, 1.2f, 1.2f}}});
    AlphaKeys(corona, {0.15f, 1.0f, 2.0f}, {{0, 0}, {0.08f, 0.7f}, {1, 0}});
    layers.push_back(std::move(corona));

    auto sparks = SparkBurst("Contact spray", 0.45f, 18, {0.25f, 1.1f, 2.5f, 1}, 2.7f, 0.48f);
    sparks.emitterTransform.transform.position = {-kMuzzle, 1.1f, 0};
    sparks.spawn.ratePerSecond = 32;
    sparks.spawn.maximumParticles = 64;
    sparks.activeDuration = 0.96f;
    layers.push_back(std::move(sparks));
    return MakePresetAssetFromEmitters(std::move(asset), std::move(layers));
}
} // namespace HIKARI::VFX::PRESETS::INTERNAL
