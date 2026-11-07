#pragma once
#include "Assets/Vfx/Internal/HIKARI_VfxPresetBuilder.h"
#include <initializer_list>
#include <utility>

namespace HIKARI::VFX::PRESETS::INTERNAL {
// 値キーはレイヤーの正規化時間。秒への暗黙変換を持たない。
VfxEmitterData Shape(std::string id, VfxRenderShapeType shape, float start, float duration);
void VectorKeys(VfxAnimatedVec3Data& value, std::string_view id,
                std::initializer_list<std::pair<float, MATH::Vec3>> keys);
void AlphaKeys(VfxEmitterData& layer, MATH::Vec3 tint, std::initializer_list<MATH::Vec2> keys);
void ParameterKeys(VfxEmitterData& layer, const std::string& target, const std::string& property,
                   std::initializer_list<MATH::Vec2> keys);
MaterialEffectStep& Effect(VfxEmitterData& layer, MaterialEffectKind kind, std::string_view name);
void Texture(VfxEmitterData& layer, std::string_view name);
void Flow(VfxEmitterData& layer, float u, float v, float tilesU = 1, float tilesV = 1);
void Erode(VfxEmitterData& layer, float start, float end);
void Light(VfxEmitterData& layer, MATH::Vec4 color, float intensity, float range);
VfxEmitterData SparkBurst(std::string id, float start, uint32_t count, MATH::Vec4 tint, float speed, float lifetime);
VfxAssetData StyleModule(std::string_view id);
VfxAssetData Slash();
VfxAssetData Beam();
VfxAssetData FireTornado();
// 燃焼する流体の火と爆発（fluid_fire、fluid_fire_wall、fluid_fireball、fluid_ground_explosion、fluid_air_explosion）。
VfxAssetData FluidPreset(std::string_view id);
VfxAssetData SupportingComposition(std::string_view id);
VfxAssetData ParticlePreset(std::string_view id);
} // namespace HIKARI::VFX::PRESETS::INTERNAL
