#pragma once
#include "Render3D/ScreenSpace/HIKARI_ScreenReadability.h"

#include <cstdint>
#include <string>

#include "HIKARI/Project/RenderShader.h"

namespace HIKARI {
class AssetRegistry;
}

namespace HIKARI::GFX {
struct Context;
}

namespace HIKARI::RENDER3D {
struct RenderTextureView;
}

namespace HIKARI::RENDER3D::PIPELINE {

bool DrawProjectFullscreenShader(const GFX::Context& graphics, const AssetRegistry& assets,
                                 const GameProjectShaderProgramRef& program, const RenderTextureView& input,
                                 uint32_t width, uint32_t height, const ScreenReadabilityFrame& readability,
                                 std::string& outDiagnostic) noexcept;

void ReleaseProjectFullscreenShaderRuntime() noexcept;

} // namespace HIKARI::RENDER3D::PIPELINE
