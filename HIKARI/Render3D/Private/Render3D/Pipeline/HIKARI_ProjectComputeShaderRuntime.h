#pragma once

#include <cstdint>
#include <string>

#include "HIKARI/Project/RenderDispatch.h"
#include "Render3D/Core/HIKARI_RenderFrameToken.h"

namespace HIKARI {
class AssetRegistry;
}

namespace HIKARI::GFX {
struct Context;
}

namespace HIKARI::RENDER3D {
struct RenderTextureView;
namespace SURFACE {
struct SceneSurfaceSet;
}
} // namespace HIKARI::RENDER3D

namespace HIKARI::RENDER3D::PIPELINE {
struct RenderTargetAccess;

bool DispatchProjectComputeShader(const GFX::Context& graphics, const AssetRegistry& assets,
                                  const GameProjectComputeShaderProgramRef& program, const RenderTextureView& input,
                                  SURFACE::SceneSurfaceSet& surfaces, RenderFrameToken frame, uint32_t width,
                                  uint32_t height, std::string& outDiagnostic) noexcept;

bool ResolveProjectDispatchOutput(const GFX::Context& graphics, const RenderTargetAccess& targetAccess,
                                  const SURFACE::SceneSurfaceSet& surfaces, std::string& outDiagnostic) noexcept;

void ReleaseProjectComputeShaderRuntime() noexcept;

} // namespace HIKARI::RENDER3D::PIPELINE
