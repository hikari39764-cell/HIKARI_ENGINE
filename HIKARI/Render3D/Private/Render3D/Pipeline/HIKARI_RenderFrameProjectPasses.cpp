#include "Render3D/Pipeline/Internal/HIKARI_RenderFrameGraphStageInternal.h"

#include <array>
#include <cstdio>
#include <string_view>
#include <utility>

#include "Render3D/Pipeline/HIKARI_RenderFrameContext.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

namespace {
constexpr std::pair<GameProjectRenderSurfaceSemantic, GRAPH::RenderGraphSemantic> kProjectSemantics[]{
    {GameProjectRenderSurfaceSemantic::SceneDepth, GRAPH::RenderGraphSemantic::SceneDepth},
    {GameProjectRenderSurfaceSemantic::SceneColorOpaque, GRAPH::RenderGraphSemantic::SceneColorOpaque},
    {GameProjectRenderSurfaceSemantic::SceneColorPostOpaque, GRAPH::RenderGraphSemantic::SceneColorPostOpaque},
    {GameProjectRenderSurfaceSemantic::SceneColorFinal, GRAPH::RenderGraphSemantic::SceneColorFinal},
    {GameProjectRenderSurfaceSemantic::PackedSurface, GRAPH::RenderGraphSemantic::PackedSurface},
    {GameProjectRenderSurfaceSemantic::SurfaceFeatureMask, GRAPH::RenderGraphSemantic::SurfaceFeatureMask},
    {GameProjectRenderSurfaceSemantic::MotionVectors, GRAPH::RenderGraphSemantic::MotionVectors},
    {GameProjectRenderSurfaceSemantic::MotionMetadata, GRAPH::RenderGraphSemantic::MotionMetadata},
    {GameProjectRenderSurfaceSemantic::CurrentDepthPyramid, GRAPH::RenderGraphSemantic::CurrentDepthPyramid},
    {GameProjectRenderSurfaceSemantic::VisibilityDepthPyramid, GRAPH::RenderGraphSemantic::VisibilityDepthPyramid},
    {GameProjectRenderSurfaceSemantic::HistoryColor, GRAPH::RenderGraphSemantic::HistoryColor},
    {GameProjectRenderSurfaceSemantic::HistoryDepth, GRAPH::RenderGraphSemantic::HistoryDepth},
    {GameProjectRenderSurfaceSemantic::StylizedOutlineStyle, GRAPH::RenderGraphSemantic::StylizedOutlineStyle},
    {GameProjectRenderSurfaceSemantic::AmbientOcclusion, GRAPH::RenderGraphSemantic::AmbientOcclusion},
    {GameProjectRenderSurfaceSemantic::ReactiveMask, GRAPH::RenderGraphSemantic::ReactiveMask},
    {GameProjectRenderSurfaceSemantic::TransparencyMask, GRAPH::RenderGraphSemantic::TransparencyMask},
    {GameProjectRenderSurfaceSemantic::InvalidDepthMotionMask, GRAPH::RenderGraphSemantic::InvalidDepthMotionMask},
    {GameProjectRenderSurfaceSemantic::ProjectDispatchOutput, GRAPH::RenderGraphSemantic::ProjectDispatchOutput},
};
constexpr std::pair<GameProjectRenderSurfacePhase, GRAPH::RenderGraphSemanticPhase> kProjectPhases[]{
    {GameProjectRenderSurfacePhase::Imported, GRAPH::RenderGraphSemanticPhase::Imported},
    {GameProjectRenderSurfacePhase::PreOpaque, GRAPH::RenderGraphSemanticPhase::PreOpaque},
    {GameProjectRenderSurfacePhase::Opaque, GRAPH::RenderGraphSemanticPhase::Opaque},
    {GameProjectRenderSurfacePhase::PostOpaque, GRAPH::RenderGraphSemanticPhase::PostOpaque},
    {GameProjectRenderSurfacePhase::Temporal, GRAPH::RenderGraphSemanticPhase::Temporal},
    {GameProjectRenderSurfacePhase::Final, GRAPH::RenderGraphSemanticPhase::Final},
    {GameProjectRenderSurfacePhase::HistoryRead, GRAPH::RenderGraphSemanticPhase::HistoryRead},
    {GameProjectRenderSurfacePhase::HistoryWrite, GRAPH::RenderGraphSemanticPhase::HistoryWrite},
};
} // namespace

GRAPH::RenderGraphSemantic MapProjectRenderSemantic(GameProjectRenderSurfaceSemantic value) noexcept {
    for (const auto& [project, engine] : kProjectSemantics)
        if (project == value)
            return engine;
    return GRAPH::RenderGraphSemantic::None;
}

GameProjectRenderSurfaceSemantic MapProjectRenderSemantic(GRAPH::RenderGraphSemantic value) noexcept {
    for (const auto& [project, engine] : kProjectSemantics)
        if (engine == value)
            return project;
    return GameProjectRenderSurfaceSemantic::None;
}

GRAPH::RenderGraphSemanticPhase MapProjectRenderPhase(GameProjectRenderSurfacePhase value) noexcept {
    for (const auto& [project, engine] : kProjectPhases)
        if (project == value)
            return engine;
    return GRAPH::RenderGraphSemanticPhase::None;
}

GameProjectRenderSurfacePhase MapProjectRenderPhase(GRAPH::RenderGraphSemanticPhase value) noexcept {
    for (const auto& [project, engine] : kProjectPhases)
        if (engine == value)
            return project;
    return GameProjectRenderSurfacePhase::None;
}

GRAPH::RenderGraphAccess MapProjectRenderAccess(GameProjectRenderSurfaceAccess access) noexcept {
    switch (access) {
    case GameProjectRenderSurfaceAccess::Sampled: return GRAPH::RenderGraphAccess::ReadSampled;
    case GameProjectRenderSurfaceAccess::ReadDepth: return GRAPH::RenderGraphAccess::ReadDepth;
    case GameProjectRenderSurfaceAccess::ReadUav: return GRAPH::RenderGraphAccess::ReadUav;
    case GameProjectRenderSurfaceAccess::CopySource: return GRAPH::RenderGraphAccess::CopySource;
    case GameProjectRenderSurfaceAccess::None: return GRAPH::RenderGraphAccess::Count;
    }
    return GRAPH::RenderGraphAccess::Count;
}

namespace {

bool RejectExtension(GRAPH::RenderGraph& graph, const char* message) noexcept {
    return graph.RejectBuild(GRAPH::RenderGraphResultCode::ExtensionRejected, message);
}

bool IsProjectSurfaceReadAllowed(GRAPH::RenderGraphSemantic semantic, GRAPH::RenderGraphSemanticPhase phase,
                                 GRAPH::RenderGraphAccess access) noexcept {
    SURFACE::SceneSurfaceAccess surfaceAccess{};
    switch (access) {
    case GRAPH::RenderGraphAccess::ReadSampled: surfaceAccess = SURFACE::SceneSurfaceAccess::Sampled; break;
    case GRAPH::RenderGraphAccess::ReadDepth: surfaceAccess = SURFACE::SceneSurfaceAccess::ReadDepth; break;
    case GRAPH::RenderGraphAccess::ReadUav: surfaceAccess = SURFACE::SceneSurfaceAccess::ReadUav; break;
    case GRAPH::RenderGraphAccess::CopySource: surfaceAccess = SURFACE::SceneSurfaceAccess::CopySource; break;
    default: return false;
    }
    const GRAPH::RenderGraphSurfaceKey key{semantic, phase};
    const SURFACE::SceneSurfaceSemanticDesc& contract = SURFACE::GetSceneSurfaceSemanticDesc(semantic);
    return SURFACE::IsSceneSurfaceKeyAllowed(key) &&
           (contract.allowedAccessMask & SURFACE::SceneSurfaceAccessBit(surfaceAccess)) != 0u;
}

} // namespace

bool BuildProjectRenderPassChain(const RenderFrameProjectExtensionSink& extensions,
                                 GameProjectRenderViewPurpose purpose, GRAPH::RenderGraphResourceId dependency,
                                 GRAPH::RenderGraphExecuteFn execute, void* userData, GRAPH::RenderGraph& graph,
                                 GRAPH::RenderGraphResourceId& outDependency) noexcept {
    outDependency = dependency;
    if (!dependency.IsValid()) {
        return RejectExtension(graph, "Project render passes require a valid anchor dependency.");
    }
    if (!extensions.CanExecutePasses()) {
        return true;
    }
    uint32_t count = 0u;
    if (!extensions.getPassCount(extensions.context, count)) {
        return RejectExtension(graph, "Project render pass enumeration failed.");
    }
    if (count > kGameProjectRenderMaximumPassCount) {
        return RejectExtension(graph, "Project render pass capacity exceeded.");
    }
    if (count != 0u && execute == nullptr) {
        return RejectExtension(graph, "Project render pass callback is unavailable.");
    }
    for (uint32_t index = 0u; index < count; ++index) {
        GameProjectRenderPassDesc descriptor{};
        if (!extensions.describePass(extensions.context, index, descriptor) || !descriptor.IsValid()) {
            return RejectExtension(graph, "Project render pass descriptor is invalid.");
        }
        const uint32_t purposeBit = GameProjectRenderViewPurposeBit(purpose);
        if ((descriptor.viewPurposeMask & purposeBit) == 0u) {
            continue;
        }

        std::array<GRAPH::RenderGraphResourceUse, kGameProjectRenderPassMaximumResourceUseCount + 1u> reads{};
        uint32_t readCount = 0u;
        reads[readCount++] = {outDependency, GRAPH::RenderGraphAccess::ReadDependency};
        for (uint32_t useIndex = 0u; useIndex < descriptor.resourceUseCount; ++useIndex) {
            const GameProjectRenderPassResourceUse& projectUse = descriptor.resources[useIndex];
            const GRAPH::RenderGraphSemantic semantic = MapProjectRenderSemantic(projectUse.semantic);
            const GRAPH::RenderGraphSemanticPhase phase = MapProjectRenderPhase(projectUse.phase);
            const GRAPH::RenderGraphAccess access = MapProjectRenderAccess(projectUse.access);
            if (semantic == GRAPH::RenderGraphSemantic::None || phase == GRAPH::RenderGraphSemanticPhase::None ||
                access == GRAPH::RenderGraphAccess::Count || !IsProjectSurfaceReadAllowed(semantic, phase, access)) {
                return RejectExtension(graph, "Project render pass resource mapping is unsupported.");
            }
            if (projectUse.requirement == GameProjectRenderPassResourceRequirement::Optional &&
                SURFACE::GetSceneSurfaceSemanticDesc(semantic).fallback ==
                    SURFACE::SceneSurfaceFallbackPolicy::RequiredFail) {
                return RejectExtension(graph, "A required-fail surface cannot be an optional input.");
            }
            const GRAPH::RenderGraphResourceId resource = graph.FindSemanticResource(semantic, phase);
            if (!resource.IsValid()) {
                if (projectUse.requirement == GameProjectRenderPassResourceRequirement::Optional) {
                    continue;
                }
                return RejectExtension(graph, "Project render pass required input is not in this graph.");
            }
            reads[readCount++] = {resource, access};
        }

        std::array<char, GRAPH::kRenderGraphIdentifierCapacity> markerId{};
        const int markerLength =
            std::snprintf(markerId.data(), markerId.size(), "ProjectRender.Pass.%u.Complete", index);
        if (markerLength <= 0 || static_cast<size_t>(markerLength) >= markerId.size()) {
            return RejectExtension(graph, "Project render pass marker id is invalid.");
        }
        const GRAPH::RenderGraphResourceId marker = AddRenderFrameGraphCompletionMarker(graph, markerId.data());
        if (!marker.IsValid()) {
            return false;
        }
        std::array<GRAPH::RenderGraphResourceUse, 2u> writes{
            GRAPH::RenderGraphResourceUse{marker, GRAPH::RenderGraphAccess::WriteDependency},
            GRAPH::RenderGraphResourceUse{}};
        size_t writeCount = 1u;
        if (descriptor.target == GameProjectRenderPassTarget::ProjectDispatchOutput) {
            const GRAPH::RenderGraphResourceId output = graph.FindSemanticResource(
                GRAPH::RenderGraphSemantic::ProjectDispatchOutput, GRAPH::RenderGraphSemanticPhase::PostOpaque);
            if (!output.IsValid()) {
                return RejectExtension(graph, "Project Dispatch output is missing from this graph.");
            }
            writes[writeCount++] = {output, GRAPH::RenderGraphAccess::WriteUav,
                                    SURFACE::SceneSurfaceProducer::ProjectDispatch};
        }
        if (!graph.AddPass(GRAPH::RenderGraphPassDesc{descriptor.stableId, std::span(reads.data(), readCount),
                                                      std::span(writes.data(), writeCount), execute, userData})) {
            return false;
        }
        outDependency = marker;
    }
    return true;
}

bool ProjectRenderPassesRequireSceneColorOpaque(const RenderFrameProjectExtensionSink& extensions,
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
        if (!extensions.describePass(extensions.context, index, descriptor) || !descriptor.IsValid() ||
            (descriptor.viewPurposeMask & purposeBit) == 0u) {
            continue;
        }
        for (uint32_t useIndex = 0u; useIndex < descriptor.resourceUseCount; ++useIndex) {
            const GameProjectRenderPassResourceUse& use = descriptor.resources[useIndex];
            if (use.semantic == GameProjectRenderSurfaceSemantic::SceneColorOpaque &&
                use.phase == GameProjectRenderSurfacePhase::Opaque &&
                use.requirement == GameProjectRenderPassResourceRequirement::Required) {
                return true;
            }
        }
    }
    return false;
}

bool ProjectRenderPassesRequireSceneColorFinal(const RenderFrameProjectExtensionSink& extensions,
                                               GameProjectRenderViewPurpose purpose) noexcept {
    if (!extensions.CanExecutePasses()) {
        return false;
    }
    uint32_t count = 0u;
    if (!extensions.getPassCount(extensions.context, count) || count > kGameProjectRenderMaximumPassCount) {
        return false;
    }
    const uint32_t purposeBit = GameProjectRenderViewPurposeBit(purpose);
    for (uint32_t index = 0u; index < count; ++index) {
        GameProjectRenderPassDesc descriptor{};
        if (!extensions.describePass(extensions.context, index, descriptor) || !descriptor.IsValid() ||
            (descriptor.viewPurposeMask & purposeBit) == 0u) {
            continue;
        }
        for (uint32_t useIndex = 0u; useIndex < descriptor.resourceUseCount; ++useIndex) {
            const GameProjectRenderPassResourceUse& use = descriptor.resources[useIndex];
            if (use.semantic == GameProjectRenderSurfaceSemantic::SceneColorFinal &&
                use.phase == GameProjectRenderSurfacePhase::Final &&
                use.access == GameProjectRenderSurfaceAccess::Sampled) {
                return true;
            }
        }
    }
    return false;
}

bool ProjectRenderPassesRequireDispatchOutput(const RenderFrameProjectExtensionSink& extensions,
                                              GameProjectRenderViewPurpose purpose) noexcept {
    if (!extensions.CanExecutePasses()) {
        return false;
    }
    uint32_t count = 0u;
    if (!extensions.getPassCount(extensions.context, count) || count > kGameProjectRenderMaximumPassCount) {
        return false;
    }
    const uint32_t purposeBit = GameProjectRenderViewPurposeBit(purpose);
    for (uint32_t index = 0u; index < count; ++index) {
        GameProjectRenderPassDesc descriptor{};
        if (extensions.describePass(extensions.context, index, descriptor) && descriptor.IsValid() &&
            (descriptor.viewPurposeMask & purposeBit) != 0u &&
            descriptor.target == GameProjectRenderPassTarget::ProjectDispatchOutput &&
            (descriptor.flags & GameProjectRenderPassFlag::ComputeCommands) != 0u) {
            return true;
        }
    }
    return false;
}

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
