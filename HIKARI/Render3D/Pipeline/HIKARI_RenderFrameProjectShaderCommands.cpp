#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Render3D/Pipeline/HIKARI_ProjectComputeShaderRuntime.h"
#include "Render3D/Pipeline/HIKARI_ProjectFullscreenShaderRuntime.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

namespace {
std::atomic<uint64_t> gNextProjectPassCommandGeneration{1u};

uint64_t AllocateProjectPassCommandGeneration() noexcept {
    uint64_t generation = 0u;
    do {
        generation = gNextProjectPassCommandGeneration.fetch_add(1u, std::memory_order_relaxed);
    } while (generation == 0u);
    return generation;
}

bool FindPassDescriptor(const RenderFrameProjectExtensionSink& extensions, std::string_view stableId,
                        GameProjectRenderPassDesc& outDescriptor) noexcept {
    outDescriptor = {};
    if (!extensions.CanExecutePasses()) {
        return false;
    }
    uint32_t count = 0u;
    if (!extensions.getPassCount(extensions.context, count) || count > kGameProjectRenderMaximumPassCount) {
        return false;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        GameProjectRenderPassDesc descriptor{};
        if (!extensions.describePass(extensions.context, index, descriptor) || !descriptor.IsValid()) {
            return false;
        }
        if (std::string_view(descriptor.stableId) == stableId) {
            outDescriptor = descriptor;
            return true;
        }
    }
    return false;
}

bool ClearWorkingColor(void* opaque, uint64_t commandGeneration, float red, float green, float blue,
                       float alpha) noexcept {
    auto* gate = static_cast<ProjectRenderCommandGate*>(opaque);
    if (gate == nullptr) {
        return false;
    }
    const std::lock_guard lock(gate->mutex);
    if (!gate->active || commandGeneration == 0u || commandGeneration != gate->generation) {
        return false;
    }
    if (gate->ownerThread != std::this_thread::get_id()) {
        gate->failed = true;
        return false;
    }
    if (gate->failed || gate->context == nullptr ||
        gate->pass.target != GameProjectRenderPassTarget::WorkingSceneColor ||
        !gate->frame.Matches(gate->context->frame.token) || !std::isfinite(red) || !std::isfinite(green) ||
        !std::isfinite(blue) || !std::isfinite(alpha) ||
        !gate->context->frame.targetAccess.ClearColor(red, green, blue, alpha)) {
        gate->failed = true;
        return false;
    }
    return true;
}
} // namespace

void ReportProjectRenderPassFailure(ProjectRenderCommandGate& gate, std::string message) noexcept {
    try {
        gate.failureReportedThisPass = true;
        const std::string_view stableId = gate.pass.stableId;
        const uint64_t worldIdentity =
            gate.context != nullptr ? gate.context->frame.projectRenderExtensions.worldIdentity : 0u;
        const auto duplicate =
            std::find_if(gate.reportedFailures.begin(), gate.reportedFailures.end(),
                         [worldIdentity, stableId, &message](const ProjectRenderFailureRecord& failure) {
                             return failure.worldIdentity == worldIdentity && failure.stableId == stableId &&
                                    failure.message == message;
                         });
        if (duplicate != gate.reportedFailures.end()) {
            return;
        }
        auto selected =
            std::find_if(gate.reportedFailures.begin(), gate.reportedFailures.end(),
                         [](const ProjectRenderFailureRecord& failure) { return failure.stableId.empty(); });
        if (selected == gate.reportedFailures.end()) {
            selected = gate.reportedFailures.begin();
        }
        selected->worldIdentity = worldIdentity;
        selected->stableId = stableId;
        selected->message = message;
        DEBUGLOG::PushRenderError(std::move(message));
    } catch (...) {
        // Diagnostics must never widen a project extension failure into an
        // engine render failure.
    }
}

void ClearProjectRenderPassFailure(ProjectRenderCommandGate& gate, std::string_view stableId) noexcept {
    const uint64_t worldIdentity =
        gate.context != nullptr ? gate.context->frame.projectRenderExtensions.worldIdentity : 0u;
    for (ProjectRenderFailureRecord& failure : gate.reportedFailures) {
        if (failure.worldIdentity == worldIdentity && failure.stableId == stableId) {
            failure = {};
        }
    }
}

bool ExecuteProjectFullscreenShaderCommand(void* opaque, uint64_t commandGeneration,
                                           const GameProjectFullscreenDrawDesc* draw) noexcept {
    auto* gate = static_cast<ProjectRenderCommandGate*>(opaque);
    if (gate == nullptr || draw == nullptr) {
        return false;
    }
    const std::lock_guard lock(gate->mutex);
    if (!gate->active || commandGeneration == 0u || commandGeneration != gate->generation) {
        return false;
    }
    if (gate->ownerThread != std::this_thread::get_id()) {
        gate->failed = true;
        return false;
    }
    if (gate->failed || gate->context == nullptr || gate->context->frame.assetRegistry == nullptr ||
        (gate->pass.flags & GameProjectRenderPassFlag::ShaderCommands) == 0u ||
        gate->pass.target != GameProjectRenderPassTarget::WorkingSceneColor ||
        !gate->frame.Matches(gate->context->frame.token) || !draw->IsValid()) {
        gate->failed = true;
        return false;
    }

    bool declaredInput = false;
    for (uint32_t index = 0u; index < gate->pass.resourceUseCount; ++index) {
        const GameProjectRenderPassResourceUse& use = gate->pass.resources[index];
        if (use.semantic == draw->inputSemantic && use.phase == draw->inputPhase &&
            use.access == GameProjectRenderSurfaceAccess::Sampled) {
            declaredInput = true;
            break;
        }
    }
    const GRAPH::RenderGraphSurfaceKey key{MapProjectRenderSemantic(draw->inputSemantic),
                                           MapProjectRenderPhase(draw->inputPhase)};
    const RenderTextureView* input = declaredInput ? gate->context->sceneSurfaces.TryGetTexture(key) : nullptr;
    if (input == nullptr || !gate->context->frame.targetAccess.Rebind()) {
        gate->failed = true;
        return false;
    }

    std::string diagnostic{};
    if (!DrawProjectFullscreenShader(gate->context->frame.graphics, *gate->context->frame.assetRegistry, draw->shader,
                                     *input, gate->context->frame.renderWidth, gate->context->frame.renderHeight,
                                     gate->context->frame.screenReadability, diagnostic)) {
        gate->failed = true;
        ReportProjectRenderPassFailure(*gate, std::string("[ProjectRenderPass][Shader][ERROR] ") + gate->pass.stableId +
                                                  ": " + diagnostic);
        return false;
    }
    return true;
}

bool ExecuteProjectComputeShaderCommand(void* opaque, uint64_t commandGeneration,
                                        const GameProjectComputeDispatchDesc* dispatch) noexcept {
    auto* gate = static_cast<ProjectRenderCommandGate*>(opaque);
    if (gate == nullptr || dispatch == nullptr) {
        return false;
    }
    const std::lock_guard lock(gate->mutex);
    if (!gate->active || commandGeneration == 0u || commandGeneration != gate->generation) {
        return false;
    }
    if (gate->ownerThread != std::this_thread::get_id()) {
        gate->failed = true;
        return false;
    }
    if (gate->failed || gate->commandSubmitted || gate->context == nullptr ||
        gate->context->frame.assetRegistry == nullptr ||
        (gate->pass.flags & GameProjectRenderPassFlag::ComputeCommands) == 0u ||
        gate->pass.target != GameProjectRenderPassTarget::ProjectDispatchOutput ||
        !gate->frame.Matches(gate->context->frame.token) || !dispatch->IsValid()) {
        gate->failed = true;
        return false;
    }
    bool declaredInput = false;
    for (uint32_t index = 0u; index < gate->pass.resourceUseCount; ++index) {
        const GameProjectRenderPassResourceUse& use = gate->pass.resources[index];
        if (use.semantic == dispatch->inputSemantic && use.phase == dispatch->inputPhase &&
            use.requirement == GameProjectRenderPassResourceRequirement::Required &&
            use.access == GameProjectRenderSurfaceAccess::Sampled) {
            declaredInput = true;
            break;
        }
    }
    const GRAPH::RenderGraphSurfaceKey inputKey{MapProjectRenderSemantic(dispatch->inputSemantic),
                                                MapProjectRenderPhase(dispatch->inputPhase)};
    const RenderTextureView* input = declaredInput ? gate->context->sceneSurfaces.TryGetTexture(inputKey) : nullptr;
    std::string diagnostic{};
    if (input == nullptr || !DispatchProjectComputeShader(
                                gate->context->frame.graphics, *gate->context->frame.assetRegistry, dispatch->shader,
                                *input, gate->context->sceneSurfaces, gate->context->frame.token,
                                gate->context->frame.renderWidth, gate->context->frame.renderHeight, diagnostic)) {
        gate->failed = true;
        ReportProjectRenderPassFailure(*gate, std::string("[ProjectRenderPass][Dispatch][ERROR] ") +
                                                  gate->pass.stableId + ": " + diagnostic);
        return false;
    }
    gate->commandSubmitted = true;
    return true;
}

bool ExecuteProjectDispatchResolve(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    std::string diagnostic{};
    if (!ResolveProjectDispatchOutput(context.frame.graphics, context.frame.targetAccess, context.sceneSurfaces,
                                      diagnostic)) {
        DEBUGLOG::PushRenderError(std::string("[ProjectRenderPass][DispatchResolve][ERROR] ") + diagnostic);
        return false;
    }
    return context.frame.targetAccess.Rebind();
}

bool ExecuteProjectRenderPass(const GRAPH::RenderGraphPassContext& graphPass, void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    const RenderFrameProjectExtensionSink& extensions = context.frame.projectRenderExtensions;
    GameProjectRenderPassDesc descriptor{};
    if (!extensions.CanExecutePasses() || !FindPassDescriptor(extensions, graphPass.passId, descriptor)) {
        return false;
    }
    const uint32_t purposeBit = GameProjectRenderViewPurposeBit(MapProjectRenderPurpose(context.frame.purpose));
    if ((descriptor.viewPurposeMask & purposeBit) == 0u) {
        return false;
    }
    ProjectRenderFrameStorage storage{};
    if (!BuildProjectRenderFrameView(context, std::span(descriptor.resources, descriptor.resourceUseCount), false,
                                     storage)) {
        return false;
    }
    ProjectRenderCommandGate* gate = state.projectCommandGate;
    if (gate == nullptr) {
        return false;
    }
    uint64_t commandGeneration = 0u;
    {
        const std::lock_guard lock(gate->mutex);
        if (gate->active) {
            return false;
        }
        gate->context = &context;
        gate->pass = descriptor;
        gate->frame = graphPass.frame;
        gate->ownerThread = std::this_thread::get_id();
        gate->generation = AllocateProjectPassCommandGeneration();
        commandGeneration = gate->generation;
        gate->active = true;
        gate->failed = false;
        gate->commandSubmitted = false;
        gate->failureReportedThisPass = false;
    }
    GameProjectRenderPassContext pass{};
    pass.frame = &storage.frame;
    pass.encoder.context = gate;
    pass.encoder.commandGeneration = commandGeneration;
    const bool computeCommands = (descriptor.flags & GameProjectRenderPassFlag::ComputeCommands) != 0u;
    if (computeCommands) {
        pass.encoder.version = kGameProjectRenderComputeCommandEncoderVersion;
        pass.encoder.dispatchCompute = &ExecuteProjectComputeShaderCommand;
        pass.encoder.capabilityMask = static_cast<uint32_t>(GameProjectRenderCommandCapability::ComputeCommands);
    } else {
        pass.encoder.clearWorkingColor = &ClearWorkingColor;
        pass.encoder.capabilityMask = static_cast<uint32_t>(GameProjectRenderCommandCapability::ClearWorkingColor);
    }
    if (!computeCommands && (descriptor.flags & GameProjectRenderPassFlag::ShaderCommands) != 0u) {
        pass.encoder.version = kGameProjectRenderCommandEncoderVersion;
        pass.encoder.drawFullscreen = &ExecuteProjectFullscreenShaderCommand;
        pass.encoder.capabilityMask |= static_cast<uint32_t>(GameProjectRenderCommandCapability::ShaderCommands);
    }
    bool callbackEntered = false;
    const bool executed = extensions.executePass(extensions.context, descriptor.stableId, pass, callbackEntered);
    {
        const std::lock_guard lock(gate->mutex);
        if (callbackEntered && executed && !gate->failed && computeCommands && !gate->commandSubmitted) {
            gate->failed = true;
            ReportProjectRenderPassFailure(*gate, std::string("[ProjectRenderPass][Dispatch][ERROR] ") +
                                                      descriptor.stableId + ": callback submitted no Dispatch command");
        }
        if (computeCommands && gate->commandSubmitted && (!executed || gate->failed)) {
            (void)context.sceneSurfaces.Invalidate(
                {GRAPH::RenderGraphSemantic::ProjectDispatchOutput, GRAPH::RenderGraphSemanticPhase::PostOpaque});
            gate->commandSubmitted = false;
        }
        const bool commandFailed = gate->failed;
        if (callbackEntered && (!executed || commandFailed)) {
            if (!gate->failureReportedThisPass) {
                ReportProjectRenderPassFailure(*gate, std::string("[ProjectRenderPass][ERROR] ") + descriptor.stableId +
                                                          (executed ? ": a project render command was rejected"
                                                                    : ": project callback returned failure"));
            }
        } else if (callbackEntered) {
            ClearProjectRenderPassFailure(*gate, descriptor.stableId);
        }
        gate->context = nullptr;
        gate->pass = {};
        gate->frame = {};
        gate->ownerThread = {};
        gate->generation = 0u;
        gate->active = false;
        gate->failed = false;
        gate->commandSubmitted = false;
        gate->failureReportedThisPass = false;
    }
    // Only a callback that actually entered project code is isolated.
    // Scheduler/engine admission failures remain fail-closed.
    return callbackEntered;
}

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
