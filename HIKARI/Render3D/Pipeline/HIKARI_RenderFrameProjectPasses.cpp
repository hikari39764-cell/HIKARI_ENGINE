#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string_view>

#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

namespace {
    std::atomic<uint64_t> gNextProjectPassCommandGeneration{ 1u };

    uint64_t AllocateProjectPassCommandGeneration() noexcept {
        uint64_t generation = 0u;
        do {
            generation = gNextProjectPassCommandGeneration.fetch_add(
                1u,
                std::memory_order_relaxed);
        } while (generation == 0u);
        return generation;
    }
} // namespace

GRAPH::RenderGraphSemantic MapProjectRenderSemantic(
    GameProjectRenderSurfaceSemantic value) noexcept {
    using Source = GameProjectRenderSurfaceSemantic;
    using Target = GRAPH::RenderGraphSemantic;
    switch (value) {
    case Source::SceneDepth: return Target::SceneDepth;
    case Source::SceneColorOpaque: return Target::SceneColorOpaque;
    case Source::SceneColorPostOpaque: return Target::SceneColorPostOpaque;
    case Source::SceneColorFinal: return Target::SceneColorFinal;
    case Source::PackedSurface: return Target::PackedSurface;
    case Source::SurfaceFeatureMask: return Target::SurfaceFeatureMask;
    case Source::MotionVectors: return Target::MotionVectors;
    case Source::MotionMetadata: return Target::MotionMetadata;
    case Source::CurrentDepthPyramid: return Target::CurrentDepthPyramid;
    case Source::VisibilityDepthPyramid:
        return Target::VisibilityDepthPyramid;
    case Source::HistoryColor: return Target::HistoryColor;
    case Source::HistoryDepth: return Target::HistoryDepth;
    case Source::StylizedOutlineStyle: return Target::StylizedOutlineStyle;
    case Source::AmbientOcclusion: return Target::AmbientOcclusion;
    case Source::ReactiveMask: return Target::ReactiveMask;
    case Source::TransparencyMask: return Target::TransparencyMask;
    case Source::InvalidDepthMotionMask:
        return Target::InvalidDepthMotionMask;
    case Source::None:
    case Source::Count:
        return Target::None;
    }
    return Target::None;
}

GRAPH::RenderGraphSemanticPhase MapProjectRenderPhase(
    GameProjectRenderSurfacePhase value) noexcept {
    using Source = GameProjectRenderSurfacePhase;
    using Target = GRAPH::RenderGraphSemanticPhase;
    switch (value) {
    case Source::Imported: return Target::Imported;
    case Source::PreOpaque: return Target::PreOpaque;
    case Source::Opaque: return Target::Opaque;
    case Source::PostOpaque: return Target::PostOpaque;
    case Source::Temporal: return Target::Temporal;
    case Source::Final: return Target::Final;
    case Source::HistoryRead: return Target::HistoryRead;
    case Source::HistoryWrite: return Target::HistoryWrite;
    case Source::None:
    case Source::Count:
        return Target::None;
    }
    return Target::None;
}

GRAPH::RenderGraphAccess MapProjectRenderAccess(
    GameProjectRenderSurfaceAccess access) noexcept {
    switch (access) {
    case GameProjectRenderSurfaceAccess::Sampled:
        return GRAPH::RenderGraphAccess::ReadSampled;
    case GameProjectRenderSurfaceAccess::ReadDepth:
        return GRAPH::RenderGraphAccess::ReadDepth;
    case GameProjectRenderSurfaceAccess::ReadUav:
        return GRAPH::RenderGraphAccess::ReadUav;
    case GameProjectRenderSurfaceAccess::CopySource:
        return GRAPH::RenderGraphAccess::CopySource;
    case GameProjectRenderSurfaceAccess::None:
        return GRAPH::RenderGraphAccess::Count;
    }
    return GRAPH::RenderGraphAccess::Count;
}

namespace {

    bool RejectExtension(
        GRAPH::RenderGraph& graph,
        const char* message) noexcept {
        return graph.RejectBuild(
            GRAPH::RenderGraphResultCode::ExtensionRejected,
            message);
    }

    bool IsProjectSurfaceReadAllowed(
        GRAPH::RenderGraphSemantic semantic,
        GRAPH::RenderGraphSemanticPhase phase,
        GRAPH::RenderGraphAccess access) noexcept {
        SURFACE::SceneSurfaceAccess surfaceAccess{};
        switch (access) {
        case GRAPH::RenderGraphAccess::ReadSampled:
            surfaceAccess = SURFACE::SceneSurfaceAccess::Sampled;
            break;
        case GRAPH::RenderGraphAccess::ReadDepth:
            surfaceAccess = SURFACE::SceneSurfaceAccess::ReadDepth;
            break;
        case GRAPH::RenderGraphAccess::ReadUav:
            surfaceAccess = SURFACE::SceneSurfaceAccess::ReadUav;
            break;
        case GRAPH::RenderGraphAccess::CopySource:
            surfaceAccess = SURFACE::SceneSurfaceAccess::CopySource;
            break;
        default:
            return false;
        }
        const GRAPH::RenderGraphSurfaceKey key{ semantic, phase };
        const SURFACE::SceneSurfaceSemanticDesc& contract =
            SURFACE::GetSceneSurfaceSemanticDesc(semantic);
        return SURFACE::IsSceneSurfaceKeyAllowed(key) &&
            (contract.allowedAccessMask &
                SURFACE::SceneSurfaceAccessBit(surfaceAccess)) != 0u;
    }

    bool FindPassDescriptor(
        const RenderFrameProjectExtensionSink& extensions,
        std::string_view stableId,
        GameProjectRenderPassDesc& outDescriptor) noexcept {
        outDescriptor = {};
        if (!extensions.CanExecutePasses()) {
            return false;
        }
        uint32_t count = 0u;
        if (!extensions.getPassCount(extensions.context, count)) {
            return false;
        }
        if (count > kGameProjectRenderMaximumPassCount) {
            return false;
        }
        for (uint32_t index = 0u; index < count; ++index) {
            GameProjectRenderPassDesc descriptor{};
            if (!extensions.describePass(
                    extensions.context,
                    index,
                    descriptor) ||
                !descriptor.IsValid()) {
                return false;
            }
            if (std::string_view(descriptor.stableId) == stableId) {
                outDescriptor = descriptor;
                return true;
            }
        }
        return false;
    }

    bool ClearWorkingColor(
        void* opaque,
        uint64_t commandGeneration,
        float red,
        float green,
        float blue,
        float alpha) noexcept {
        auto* gate = static_cast<ProjectRenderCommandGate*>(opaque);
        if (gate == nullptr) {
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
            gate->pass.target !=
                GameProjectRenderPassTarget::WorkingSceneColor ||
            !gate->frame.Matches(gate->context->frame.token) ||
            !std::isfinite(red) || !std::isfinite(green) ||
            !std::isfinite(blue) || !std::isfinite(alpha)) {
            gate->failed = true;
            return false;
        }
        if (!gate->context->frame.targetAccess.ClearColor(
                red, green, blue, alpha)) {
            gate->failed = true;
            return false;
        }
        return true;
    }

} // namespace

bool BuildProjectRenderPassChain(
    const RenderFrameProjectExtensionSink& extensions,
    GameProjectRenderViewPurpose purpose,
    GRAPH::RenderGraphResourceId dependency,
    GRAPH::RenderGraphExecuteFn execute,
    void* userData,
    GRAPH::RenderGraph& graph,
    GRAPH::RenderGraphResourceId& outDependency) noexcept {
    outDependency = dependency;
    if (!dependency.IsValid()) {
        return RejectExtension(
            graph,
            "Project render passes require a valid anchor dependency.");
    }
    if (!extensions.CanExecutePasses()) {
        return true;
    }
    uint32_t count = 0u;
    if (!extensions.getPassCount(extensions.context, count)) {
        return RejectExtension(
            graph,
            "Project render pass enumeration failed.");
    }
    if (count > kGameProjectRenderMaximumPassCount) {
        return RejectExtension(
            graph,
            "Project render pass capacity exceeded.");
    }
    if (count != 0u && execute == nullptr) {
        return RejectExtension(
            graph,
            "Project render pass callback is unavailable.");
    }
    for (uint32_t index = 0u; index < count; ++index) {
        GameProjectRenderPassDesc descriptor{};
        if (!extensions.describePass(
                extensions.context,
                index,
                descriptor) ||
            !descriptor.IsValid()) {
            return RejectExtension(
                graph,
                "Project render pass descriptor is invalid.");
        }
        const uint32_t purposeBit =
            GameProjectRenderViewPurposeBit(purpose);
        if ((descriptor.viewPurposeMask & purposeBit) == 0u) {
            continue;
        }

        std::array<
            GRAPH::RenderGraphResourceUse,
            kGameProjectRenderPassMaximumResourceUseCount + 1u> reads{};
        uint32_t readCount = 0u;
        reads[readCount++] = {
            outDependency,
            GRAPH::RenderGraphAccess::ReadDependency
        };
        for (uint32_t useIndex = 0u;
            useIndex < descriptor.resourceUseCount;
            ++useIndex) {
            const GameProjectRenderPassResourceUse& projectUse =
                descriptor.resources[useIndex];
            const GRAPH::RenderGraphSemantic semantic =
                MapProjectRenderSemantic(projectUse.semantic);
            const GRAPH::RenderGraphSemanticPhase phase =
                MapProjectRenderPhase(projectUse.phase);
            const GRAPH::RenderGraphAccess access =
                MapProjectRenderAccess(projectUse.access);
            if (semantic == GRAPH::RenderGraphSemantic::None ||
                phase == GRAPH::RenderGraphSemanticPhase::None ||
                access == GRAPH::RenderGraphAccess::Count ||
                !IsProjectSurfaceReadAllowed(
                    semantic,
                    phase,
                    access)) {
                return RejectExtension(
                    graph,
                    "Project render pass resource mapping is unsupported.");
            }
            if (projectUse.requirement ==
                    GameProjectRenderPassResourceRequirement::Optional &&
                SURFACE::GetSceneSurfaceSemanticDesc(semantic).fallback ==
                    SURFACE::SceneSurfaceFallbackPolicy::RequiredFail) {
                return RejectExtension(
                    graph,
                    "A required-fail surface cannot be an optional input.");
            }
            const GRAPH::RenderGraphResourceId resource =
                graph.FindSemanticResource(semantic, phase);
            if (!resource.IsValid()) {
                if (projectUse.requirement ==
                    GameProjectRenderPassResourceRequirement::Optional) {
                    continue;
                }
                return RejectExtension(
                    graph,
                    "Project render pass required input is not in this graph.");
            }
            reads[readCount++] = { resource, access };
        }

        std::array<char, GRAPH::kRenderGraphIdentifierCapacity> markerId{};
        const int markerLength = std::snprintf(
            markerId.data(),
            markerId.size(),
            "ProjectRender.Pass.%u.Complete",
            index);
        if (markerLength <= 0 ||
            static_cast<size_t>(markerLength) >= markerId.size()) {
            return RejectExtension(
                graph,
                "Project render pass marker id is invalid.");
        }
        const GRAPH::RenderGraphResourceId marker =
            AddRenderFrameGraphCompletionMarker(graph, markerId.data());
        if (!marker.IsValid()) {
            return false;
        }
        const std::array writes{
            GRAPH::RenderGraphResourceUse{
                marker,
                GRAPH::RenderGraphAccess::WriteDependency
            }
        };
        if (!graph.AddPass(GRAPH::RenderGraphPassDesc{
                descriptor.stableId,
                std::span(reads.data(), readCount),
                writes,
                execute,
                userData
            })) {
            return false;
        }
        outDependency = marker;
    }
    return true;
}

bool ProjectRenderPassesRequireSceneColorOpaque(
    const RenderFrameProjectExtensionSink& extensions,
    GameProjectRenderViewPurpose purpose) noexcept {
    if (!extensions.CanExecutePasses()) {
        return false;
    }
    uint32_t count = 0u;
    if (!extensions.getPassCount(extensions.context, count)) {
        return false;
    }
    if (count > kGameProjectRenderMaximumPassCount) {
        return false;
    }
    const uint32_t purposeBit = GameProjectRenderViewPurposeBit(purpose);
    for (uint32_t index = 0u; index < count; ++index) {
        GameProjectRenderPassDesc descriptor{};
        if (!extensions.describePass(
                extensions.context,
                index,
                descriptor) ||
            !descriptor.IsValid() ||
            (descriptor.viewPurposeMask & purposeBit) == 0u) {
            continue;
        }
        for (uint32_t useIndex = 0u;
            useIndex < descriptor.resourceUseCount;
            ++useIndex) {
            const GameProjectRenderPassResourceUse& use =
                descriptor.resources[useIndex];
            if (use.semantic ==
                    GameProjectRenderSurfaceSemantic::SceneColorOpaque &&
                use.phase == GameProjectRenderSurfacePhase::Opaque &&
                use.requirement ==
                    GameProjectRenderPassResourceRequirement::Required) {
                return true;
            }
        }
    }
    return false;
}

bool ExecuteProjectRenderPass(
    const GRAPH::RenderGraphPassContext& graphPass,
    void* userData) noexcept {
    auto& state = *static_cast<StageExecutionState*>(userData);
    if (!ValidateRenderFrameGraphPassContext(graphPass, state)) {
        return false;
    }
    const RenderFrameGraphStageContext& context = *state.context;
    const RenderFrameProjectExtensionSink& extensions =
        context.frame.projectRenderExtensions;
    if (!extensions.CanExecutePasses()) {
        return false;
    }
    GameProjectRenderPassDesc descriptor{};
    if (!FindPassDescriptor(
            extensions,
            graphPass.passId,
            descriptor)) {
        return false;
    }
    const uint32_t purposeBit = GameProjectRenderViewPurposeBit(
        MapProjectRenderPurpose(context.frame.purpose));
    if ((descriptor.viewPurposeMask & purposeBit) == 0u) {
        return false;
    }

    ProjectRenderFrameStorage storage{};
    if (!BuildProjectRenderFrameView(
            context,
            std::span(
                descriptor.resources,
                descriptor.resourceUseCount),
            false,
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
    }
    GameProjectRenderPassContext pass{};
    pass.frame = &storage.frame;
    pass.encoder.context = gate;
    pass.encoder.commandGeneration = commandGeneration;
    pass.encoder.clearWorkingColor = &ClearWorkingColor;
    pass.encoder.capabilityMask = static_cast<uint32_t>(
        GameProjectRenderCommandCapability::ClearWorkingColor);
    const bool executed = extensions.executePass(
        extensions.context,
        descriptor.stableId,
        pass);
    bool commandFailed = false;
    {
        const std::lock_guard lock(gate->mutex);
        commandFailed = gate->failed;
        gate->context = nullptr;
        gate->pass = {};
        gate->frame = {};
        gate->ownerThread = {};
        gate->generation = 0u;
        gate->active = false;
        gate->failed = false;
    }
    return executed && !commandFailed;
}

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
