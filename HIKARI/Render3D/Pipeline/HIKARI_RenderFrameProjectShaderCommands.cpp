#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <string>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/Pipeline/HIKARI_ProjectFullscreenShaderRuntime.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

bool ExecuteProjectFullscreenShaderCommand(
    void* opaque,
    uint64_t commandGeneration,
    const GameProjectFullscreenDrawDesc* draw) noexcept {
    auto* gate = static_cast<ProjectRenderCommandGate*>(opaque);
    if (gate == nullptr || draw == nullptr) {
        return false;
    }
    const std::lock_guard lock(gate->mutex);
    if (!gate->active || commandGeneration == 0u ||
        commandGeneration != gate->generation) {
        return false;
    }
    if (gate->ownerThread != std::this_thread::get_id()) {
        gate->failed = true;
        return false;
    }
    if (gate->failed || gate->context == nullptr ||
        gate->context->frame.assetRegistry == nullptr ||
        (gate->pass.flags &
            GameProjectRenderPassFlag::ShaderCommands) == 0u ||
        gate->pass.target !=
            GameProjectRenderPassTarget::WorkingSceneColor ||
        !gate->frame.Matches(gate->context->frame.token) ||
        !draw->IsValid()) {
        gate->failed = true;
        return false;
    }

    bool declaredInput = false;
    for (uint32_t index = 0u;
        index < gate->pass.resourceUseCount;
        ++index) {
        const GameProjectRenderPassResourceUse& use =
            gate->pass.resources[index];
        if (use.semantic == draw->inputSemantic &&
            use.phase == draw->inputPhase &&
            use.access == GameProjectRenderSurfaceAccess::Sampled) {
            declaredInput = true;
            break;
        }
    }
    const GRAPH::RenderGraphSurfaceKey key{
        MapProjectRenderSemantic(draw->inputSemantic),
        MapProjectRenderPhase(draw->inputPhase)
    };
    const RenderTextureView* input = declaredInput
        ? gate->context->sceneSurfaces.TryGetTexture(key)
        : nullptr;
    if (input == nullptr ||
        !gate->context->frame.targetAccess.Rebind()) {
        gate->failed = true;
        return false;
    }

    std::string diagnostic{};
    if (!DrawProjectFullscreenShader(
            gate->context->frame.graphics,
            *gate->context->frame.assetRegistry,
            draw->shader,
            *input,
            gate->context->frame.renderWidth,
            gate->context->frame.renderHeight,
            diagnostic)) {
        gate->failed = true;
        DEBUGLOG::PushRenderError(
            std::string("[ProjectRenderPass][Shader][ERROR] ") +
            gate->pass.stableId + ": " + diagnostic);
        return false;
    }
    return true;
}

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
