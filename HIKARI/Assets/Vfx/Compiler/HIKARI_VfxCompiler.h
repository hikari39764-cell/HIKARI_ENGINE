#pragma once

#include <vector>

#include "Assets/Vfx/HIKARI_VfxProgram.h"
#include "Assets/Vfx/HIKARI_VfxValidation.h"

namespace HIKARI::VFX {

struct VfxCompileResult {
    bool success = false;
    VfxProgram program{};
    std::vector<VfxDiagnostic> diagnostics{};
};

VfxCompileResult CompileVfxAsset(const VfxAssetData& asset);

} // namespace HIKARI::VFX
