#pragma once

#include <cstddef>
#include <string_view>

#include "Assets/Vfx/HIKARI_VfxProgram.h"

namespace HIKARI::VFX {

// Compiled HVFX is the only source for special-output intent.  The
// renderer-facing services use this view to select one typed profile;
// they do not interpret authoring graphs or retain the program pointer.
inline const VfxSpecialOutputData* FindVfxSpecialOutput(const VfxProgram& program, std::string_view outputId,
                                                        VfxSpecialOutputType type) noexcept {
    for (const VfxProgramEmitter& emitter : program.emitters) {
        for (const VfxSpecialOutputData& output : emitter.specialOutputs) {
            if (output.enabled && output.type == type && (outputId.empty() || output.id == outputId)) {
                return &output;
            }
        }
    }
    return nullptr;
}

} // namespace HIKARI::VFX
