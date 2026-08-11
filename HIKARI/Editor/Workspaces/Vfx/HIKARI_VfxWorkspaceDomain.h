#pragma once

#include <string>
#include <string_view>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Assets/Vfx/HIKARI_VfxPropertySchema.h"

namespace HIKARI::EDITOR {

    struct VfxCurveAddress {
        std::string emitterId{};
        std::string moduleId{};
        std::string propertyId{};
        std::string channel{};
    };

    std::string_view GetVfxModuleId(
        const VFX::VfxEmitterData& emitter,
        VFX::VfxModuleKind kind) noexcept;

    const VFX::VfxCurveData* ResolveVfxCurve(
        const VFX::VfxEmitterData& emitter,
        const VfxCurveAddress& address) noexcept;
    VFX::VfxCurveData* ResolveVfxCurve(
        VFX::VfxEmitterData& emitter,
        const VfxCurveAddress& address) noexcept;

    const VFX::VfxColorGradientData* ResolveVfxGradient(
        const VFX::VfxEmitterData& emitter,
        std::string_view moduleId,
        std::string_view propertyId) noexcept;
    VFX::VfxColorGradientData* ResolveVfxGradient(
        VFX::VfxEmitterData& emitter,
        std::string_view moduleId,
        std::string_view propertyId) noexcept;

} // namespace HIKARI::EDITOR
