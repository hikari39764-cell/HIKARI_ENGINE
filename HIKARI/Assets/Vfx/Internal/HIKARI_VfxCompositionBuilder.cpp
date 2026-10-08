#include "Assets/Vfx/Internal/HIKARI_VfxCompositionBuilder.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {
VfxEmitterData Shape(std::string id, VfxRenderShapeType shape, float start, float duration) {
    auto layer = MakePresetEmitterBase(id, id);
    layer.kind = VfxLayerKind::Shape;
    layer.startDelay = start;
    layer.activeDuration = duration;
    layer.renderShape.type = shape;
    layer.renderShape.billboardMode = VfxBillboardMode::Fixed;
    layer.renderShape.ringSegmentCount = 64;
    layer.renderShape.ribbonSegmentCount = 48;
    layer.renderCommon.blendMode = VfxBlendMode::Additive;
    layer.renderCommon.colorTextureAssetGuid = {};
    layer.spawn.ratePerSecond = 0;
    layer.spawn.burstTimeline.clear();
    layer.spawn.maximumParticles = 1;
    layer.position.radialSpeed = MakeVfxFloatConstant(0);
    layer.color.value.mode = VfxAnimatedColorMode::Fixed;
    layer.color.value.fixed = MakeVfxColorConstant({1, 1, 1, 1});
    return layer;
}
void VectorKeys(VfxAnimatedVec3Data& value, std::string_view id,
                std::initializer_list<std::pair<float, MATH::Vec3>> keys) {
    value.mode = VfxAnimatedValueMode::Curve;
    VfxCurveData* curves[]{&value.curve.x, &value.curve.y, &value.curve.z};
    for (auto* curve : curves) {
        curve->keys.clear();
        curve->interpolation = VfxCurveInterpolation::Smooth;
    }
    for (auto [time, v] : keys) {
        const float xyz[]{v.x, v.y, v.z};
        for (size_t i = 0; i < 3; ++i)
            curves[i]->keys.push_back(
                {std::string(id) + "." + std::to_string(i) + "." + std::to_string(curves[i]->keys.size()), time,
                 xyz[i]});
    }
}
void AlphaKeys(VfxEmitterData& layer, MATH::Vec3 tint, std::initializer_list<MATH::Vec2> keys) {
    layer.color.value.mode = VfxAnimatedColorMode::Curve;
    auto& color = layer.color.value.curve;
    color.r = MakeVfxConstantCurve(layer.id + ".r", tint.x);
    color.g = MakeVfxConstantCurve(layer.id + ".g", tint.y);
    color.b = MakeVfxConstantCurve(layer.id + ".b", tint.z);
    color.a.keys.clear();
    color.a.interpolation = VfxCurveInterpolation::Smooth;
    for (const auto key : keys)
        color.a.keys.push_back({layer.id + ".a." + std::to_string(color.a.keys.size()), key.x, key.y});
}
void ParameterKeys(VfxEmitterData& layer, const std::string& target, const std::string& property,
                   std::initializer_list<MATH::Vec2> keys) {
    VfxParameterTrackData track;
    track.id = target + ".key." + property;
    track.targetId = target;
    track.propertyId = property;
    track.curve.interpolation = VfxCurveInterpolation::Smooth;
    for (auto v : keys)
        track.curve.keys.push_back({track.id + "." + std::to_string(track.curve.keys.size()), v.x, v.y});
    layer.parameterTracks.push_back(std::move(track));
}
MaterialEffectStep& Effect(VfxEmitterData& layer, MaterialEffectKind kind, std::string_view name) {
    layer.effects.push_back(MakeMaterialEffect(kind, layer.id + "." + std::string(name)));
    return layer.effects.back();
}
void Texture(VfxEmitterData& layer, std::string_view name) {
    layer.renderCommon.colorTextureAssetGuid = AssetGuid{"engine.vfx.texture." + std::string(name)};
}
void Flow(VfxEmitterData& layer, float u, float v, float tilesU, float tilesV) {
    Effect(layer, MaterialEffectKind::UvFlow, "advection").parameters = {u, v, tilesU, tilesV};
}
void Erode(VfxEmitterData& layer, float start, float end) {
    layer.renderCommon.noiseTextureAssetGuid = AssetGuid{"engine.vfx.texture.distortion_seamless"};
    auto& dissolve = Effect(layer, MaterialEffectKind::Dissolve, "erosion");
    dissolve.parameters = {start, end, 1.7f, 0.09f};
    dissolve.colorA = {0.03f, 0.14f, 0.25f, 1};
}
void Light(VfxEmitterData& layer, MATH::Vec4 color, float luminousFlux, float range) {
    VfxSpecialOutputData output{};
    output.id = layer.id + ".light";
    output.type = VfxSpecialOutputType::Light;
    output.light.color = color;
    output.light.luminousFlux = luminousFlux;
    output.light.range = range;
    layer.specialOutputs.push_back(std::move(output));
}
VfxEmitterData SparkBurst(std::string id, float start, uint32_t count, MATH::Vec4 tint, float speed, float lifetime) {
    auto layer = Shape(std::move(id), VfxRenderShapeType::Sprite, start, 0.035f);
    layer.kind = VfxLayerKind::Particles;
    layer.spawn.maximumParticles = (std::max)(count, 16u);
    if (count > 0)
        layer.spawn.burstTimeline = {{layer.spawn.id + ".burst", 0, count}};
    layer.basic.lifetime = MakeVfxFloatConstant(lifetime);
    layer.shape.type = VfxShapeType::Sphere;
    layer.shape.radius = MakeVfxFloatConstant(0.08f);
    layer.position.radialSpeed = MakeVfxFloatConstant(speed);
    layer.position.radialSpeed.mode = VfxDistributionMode::RandomRange;
    layer.position.radialSpeed.minimum = speed * 0.65f;
    layer.position.radialSpeed.maximum = speed * 1.1f;
    layer.forces.gravity = {0, -3, 0};
    layer.forces.drag = 1.5f;
    layer.renderShape.billboardMode = VfxBillboardMode::DirectionalBillboard;
    Texture(layer, "soft_line");
    VectorKeys(layer.scale.value, layer.id + ".size",
               {{0, {0.028f, 0.32f, 0.028f}}, {0.65f, {0.014f, 0.12f, 0.014f}}, {1, {0, 0.01f, 0}}});
    AlphaKeys(layer, {tint.x, tint.y, tint.z}, {{0, tint.w}, {0.25f, tint.w}, {1, 0}});
    return layer;
}
} // namespace HIKARI::VFX::PRESETS::INTERNAL
