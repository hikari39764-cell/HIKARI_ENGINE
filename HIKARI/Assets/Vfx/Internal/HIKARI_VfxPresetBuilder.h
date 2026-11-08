#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX::PRESETS::INTERNAL {

VfxEmitterData MakePresetEmitterBase(std::string idSuffix, std::string name);

VfxAssetData MakePresetAssetFromEmitters(VfxAssetData asset, std::vector<VfxEmitterData> emitters);

// 预设の旋钮：資産の公開入力（倍率は既定 1、色は既定の白）と、それを層の実行時の値（HIKARI_VfxPropertySchema.h の連結先）
// へ掛ける link。一つの旋钮を複数の層へ繋いでよい（層ごとに同じ値へは一つだけ）。
void AddKnob(VfxAssetData& asset, std::string id, std::string name, VfxValueType type);
void LinkSpawnRate(VfxEmitterData& emitter, std::string_view inputId);
void LinkTemperature(VfxEmitterData& emitter, std::string_view inputId);
void LinkTurbulence(VfxEmitterData& emitter, std::string_view inputId);
void LinkColor(VfxEmitterData& emitter, std::string_view inputId);

} // namespace HIKARI::VFX::PRESETS::INTERNAL
