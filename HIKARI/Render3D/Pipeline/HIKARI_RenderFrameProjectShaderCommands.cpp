#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/Pipeline/HIKARI_ProjectFullscreenShaderRuntime.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

void ReportProjectRenderPassFailure(
    ProjectRenderCommandGate& gate,
    std::string message) noexcept {
    try {
        gate.failureReportedThisPass = true;
        const std::string_view stableId = gate.pass.stableId;
        const uint64_t worldIdentity = gate.context != nullptr
            ? gate.context->frame.projectRenderExtensions.worldIdentity
            : 0u;
        const auto duplicate = std::find_if(
            gate.reportedFailures.begin(),
            gate.reportedFailures.end(),
            [worldIdentity, stableId, &message](
                const ProjectRenderFailureRecord& failure) {
                return failure.worldIdentity == worldIdentity &&
                    failure.stableId == stableId &&
                    failure.message == message;
            });
        if (duplicate != gate.reportedFailures.end()) {
            return;
        }
        auto selected = std::find_if(
            gate.reportedFailures.begin(),
            gate.reportedFailures.end(),
            [](const ProjectRenderFailureRecord& failure) {
                return failure.stableId.empty();
            });
        if (selected == gate.reportedFailures.end()) {
            selected = gate.reportedFailures.begin();
        }
        selected->worldIdentity = worldIdentity;
        selected->stableId = stableId;
        selected->message = message;
        DEBUGLOG::PushRenderError(std::move(message));
    }
    catch (...) {
        // Diagnostics must never widen a project extension failure into an
        // engine render failure.
    }
}

void ClearProjectRenderPassFailure(
    ProjectRenderCommandGate& gate,
    std::string_view stableId) noexcept {
    const uint64_t worldIdentity = gate.context != nullptr
        ? gate.context->frame.projectRenderExtensions.worldIdentity
        : 0u;
    for (ProjectRenderFailureRecord& failure : gate.reportedFailures) {
        if (failure.worldIdentity == worldIdentity &&
            failure.stableId == stableId) {
            failure = {};
        }
    }
}

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
        ReportProjectRenderPassFailure(
            *gate,
            std::string("[ProjectRenderPass][Shader][ERROR] ") +
                gate->pass.stableId + ": " + diagnostic);
        return false;
    }
    return true;
}

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
