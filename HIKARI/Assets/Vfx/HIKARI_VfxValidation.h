#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX {

enum class VfxDiagnosticSeverity : uint8_t {
    Info,
    Warning,
    Error,
};

struct VfxDiagnostic {
    VfxDiagnosticSeverity severity = VfxDiagnosticSeverity::Error;
    std::string code{};
    std::string message{};
    std::string subjectId{};
};

std::vector<VfxDiagnostic> ValidateVfxAsset(const VfxAssetData& asset);
bool HasVfxErrors(const std::vector<VfxDiagnostic>& diagnostics) noexcept;

} // namespace HIKARI::VFX
