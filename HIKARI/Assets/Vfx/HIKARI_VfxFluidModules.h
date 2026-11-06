#pragma once

#include <string>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Render3D/Vfx/HIKARI_FluidTypes.h"

// VFX の流体層の模块。値の本体は Render3D の FluidDomainSettings / FluidSourceSettings（字段表もそこ）で、
// ここは編集操作の対象を指す模块 ID を足すだけ。源 JSON では modules.fluidDomain / modules.fluidSource に入る。
namespace HIKARI::VFX {

struct VfxFluidDomainModuleData : RENDER3D::FLUID::FluidDomainSettings {
    std::string id{};
};

struct VfxFluidSourceModuleData : RENDER3D::FLUID::FluidSourceSettings {
    std::string id{};
};

inline constexpr auto kVfxFluidDomainModuleFields = [](auto&& field) {
    field("id", &VfxFluidDomainModuleData::id, JSONFIELDS::ReadOnly{}, JSONFIELDS::Hidden{});
    RENDER3D::FLUID::kFluidDomainSettingsFields(field);
};

inline constexpr auto kVfxFluidSourceModuleFields = [](auto&& field) {
    field("id", &VfxFluidSourceModuleData::id, JSONFIELDS::ReadOnly{}, JSONFIELDS::Hidden{});
    RENDER3D::FLUID::kFluidSourceSettingsFields(field);
};

} // namespace HIKARI::VFX
