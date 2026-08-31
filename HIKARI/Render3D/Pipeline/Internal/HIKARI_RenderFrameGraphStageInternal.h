#pragma once

#include <array>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "HIKARI/Project/RenderPass.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameGraphStage.h"
#include "Render3D/Pipeline/HIKARI_RenderFrameGraphSession.h"

namespace HIKARI::RENDER3D::PIPELINE::INTERNAL {

    struct ProjectRenderFailureRecord {
        uint64_t worldIdentity = 0u;
        std::string stableId{};
        std::string message{};
    };

    struct ProjectRenderCommandGate {
        std::mutex mutex{};
        const RenderFrameGraphStageContext* context = nullptr;
        GameProjectRenderPassDesc pass{};
        RenderFrameToken frame{};
        std::thread::id ownerThread{};
        uint64_t generation = 0u;
        std::array<
            ProjectRenderFailureRecord,
            kGameProjectRenderMaximumPassCount * 2u> reportedFailures{};
        bool active = false;
        bool failed = false;
        bool failureReportedThisPass = false;
    };

    struct StageExecutionState {
        // Borrowed only while the Pipeline prefix executes. A resumable suffix
        // may use session-owned fields below, but must not retain this pointer.
        const RenderFrameGraphStageContext* context = nullptr;
        RenderFrameGraphStageResult result{};
        RenderFrameGraphSessionReceipt session{};
        ProjectRenderCommandGate* projectCommandGate = nullptr;
        bool presentationResumed = false;
    };

    struct RenderFrameGraphPlanOptions {
        bool captureSceneColor = false;
        bool includeSsr = false;
        bool includeDepthAware = false;
        bool includeTransparent = false;
        const RenderFrameProjectExtensionSink* projectExtensions = nullptr;
        GameProjectRenderViewPurpose projectPurpose =
            GameProjectRenderViewPurpose::Game;
    };

    struct ProjectRenderFrameStorage {
        std::array<
            GameProjectRenderSurfaceDesc,
            kGameProjectRenderMaximumSurfaceCount> surfaces{};
        GameProjectRenderCameraFrame camera{};
        GameProjectRenderFrameView frame{};
    };

    struct RenderFrameGraphPassCallbacks {
        GRAPH::RenderGraphExecuteFn forwardOpaque = nullptr;
        GRAPH::RenderGraphExecuteFn postOpaqueSurfaces = nullptr;
        GRAPH::RenderGraphExecuteFn clouds = nullptr;
        GRAPH::RenderGraphExecuteFn motionVectors = nullptr;
        GRAPH::RenderGraphExecuteFn sceneColorOpaqueCapture = nullptr;
        GRAPH::RenderGraphExecuteFn ssr = nullptr;
        GRAPH::RenderGraphExecuteFn depthAware = nullptr;
        GRAPH::RenderGraphExecuteFn transparent = nullptr;
        GRAPH::RenderGraphExecuteFn featurePlan = nullptr;
        GRAPH::RenderGraphExecuteFn geometryFeatures = nullptr;
        GRAPH::RenderGraphExecuteFn visibilityFeatures = nullptr;
        GRAPH::RenderGraphExecuteFn sceneColorPostOpaqueCapture = nullptr;
        GRAPH::RenderGraphExecuteFn stylizedFeatureComposite = nullptr;
        GRAPH::RenderGraphExecuteFn projectRenderPass = nullptr;
        GRAPH::RenderGraphExecuteFn projectRenderObserver = nullptr;
        GRAPH::RenderGraphExecuteFn presentationResume = nullptr;
    };

    struct RenderFrameGraphTailResources {
        GRAPH::RenderGraphResourceId completionDependency{};
        GRAPH::RenderGraphResourceId sceneDepth{};
        GRAPH::RenderGraphResourceId sceneColorOpaque{};
        GRAPH::RenderGraphResourceId packedSurface{};
        GRAPH::RenderGraphResourceId ambientOcclusion{};
        GRAPH::RenderGraphResourceId currentDepthPyramid{};
        GRAPH::RenderGraphResourceId visibilityDepthPyramid{};
        GRAPH::RenderGraphResourceId surfaceFeatureMask{};
        GRAPH::RenderGraphResourceId stylizedOutlineStyle{};
    };

    bool ValidateRenderFrameGraphPassContext(
        const GRAPH::RenderGraphPassContext& graphPass,
        const StageExecutionState& state) noexcept;

    GRAPH::RenderGraphResourceId AddRenderFrameGraphSurfaceResource(
        GRAPH::RenderGraph& graph,
        std::string_view id,
        GRAPH::RenderGraphSemantic semantic,
        bool imported,
        GRAPH::RenderGraphSemanticPhase phase =
            GRAPH::RenderGraphSemanticPhase::None,
        SURFACE::SceneSurfaceProducer producer =
            SURFACE::SceneSurfaceProducer::None,
        GRAPH::RenderGraphResourceRequirement requirement =
            GRAPH::RenderGraphResourceRequirement::Required) noexcept;
    GRAPH::RenderGraphResourceId AddRenderFrameGraphCompletionMarker(
        GRAPH::RenderGraph& graph,
        std::string_view id) noexcept;

    bool ExecuteForwardOpaque(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecutePostOpaqueSurfaces(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteClouds(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteMotionVectors(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteSceneColorOpaqueCapture(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteSsr(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteDepthAware(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteForwardTransparent(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteRenderFeaturePlan(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteGeometryFeatures(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteVisibilityFeatures(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteSceneColorPostOpaqueCapture(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteStylizedFeatureComposite(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteProjectRenderPass(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecuteProjectFullscreenShaderCommand(
        void* context,
        uint64_t commandGeneration,
        const GameProjectFullscreenDrawDesc* draw) noexcept;
    void ReportProjectRenderPassFailure(
        ProjectRenderCommandGate& gate,
        std::string message) noexcept;
    void ClearProjectRenderPassFailure(
        ProjectRenderCommandGate& gate,
        std::string_view stableId) noexcept;
    bool ExecuteProjectRenderObserver(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;
    bool ExecutePresentationResume(
        const GRAPH::RenderGraphPassContext& context,
        void* userData) noexcept;

    const RenderFrameGraphPassCallbacks&
        GetRenderFrameGraphPassCallbacks() noexcept;
    void ReleaseRenderFrameGraphPassResources() noexcept;

    bool BuildRenderFrameGraphPlan(
        const RenderFrameGraphPlanOptions& options,
        void* userData,
        const RenderFrameGraphPassCallbacks& callbacks,
        GRAPH::RenderGraph& graph) noexcept;
    bool BuildRenderFrameGraphTailPlan(
        const RenderFrameGraphPlanOptions& options,
        void* userData,
        const RenderFrameGraphPassCallbacks& callbacks,
        const RenderFrameGraphTailResources& resources,
        GRAPH::RenderGraph& graph) noexcept;

    bool BuildProjectRenderFrameView(
        const RenderFrameGraphStageContext& context,
        std::span<const GameProjectRenderPassResourceUse> resources,
        bool includeAllPublishedSurfaces,
        ProjectRenderFrameStorage& storage) noexcept;
    GRAPH::RenderGraphSemantic MapProjectRenderSemantic(
        GameProjectRenderSurfaceSemantic semantic) noexcept;
    GRAPH::RenderGraphSemanticPhase MapProjectRenderPhase(
        GameProjectRenderSurfacePhase phase) noexcept;
    GRAPH::RenderGraphAccess MapProjectRenderAccess(
        GameProjectRenderSurfaceAccess access) noexcept;
    GameProjectRenderViewPurpose MapProjectRenderPurpose(
        RenderViewPurpose purpose) noexcept;
    bool BuildProjectRenderPassChain(
        const RenderFrameProjectExtensionSink& extensions,
        GameProjectRenderViewPurpose purpose,
        GRAPH::RenderGraphResourceId dependency,
        GRAPH::RenderGraphExecuteFn execute,
        void* userData,
        GRAPH::RenderGraph& graph,
        GRAPH::RenderGraphResourceId& outDependency) noexcept;
    bool ProjectRenderPassesRequireSceneColorOpaque(
        const RenderFrameProjectExtensionSink& extensions,
        GameProjectRenderViewPurpose purpose) noexcept;

} // namespace HIKARI::RENDER3D::PIPELINE::INTERNAL
