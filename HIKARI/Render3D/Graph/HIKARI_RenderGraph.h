#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "Render3D/Core/HIKARI_RenderFrameToken.h"
#include "Render3D/Graph/HIKARI_RenderGraphSurfaceContract.h"
#include "Render3D/Surface/HIKARI_RenderFrameSurfaceRegistry.h"
#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::GRAPH {

    inline constexpr uint32_t kRenderGraphMaximumResourceCount = 64u;
    inline constexpr uint32_t kRenderGraphMaximumPassCount = 64u;
    inline constexpr uint32_t kRenderGraphMaximumResourceUseCount = 256u;
    inline constexpr size_t kRenderGraphIdentifierCapacity = 64u;
    inline constexpr uint32_t kInvalidRenderGraphIndex =
        (std::numeric_limits<uint32_t>::max)();

    enum class RenderGraphAccess : uint8_t {
        ReadSampled = 0u,
        ReadDepth,
        ReadUav,
        CopySource,
        WriteColor,
        WriteDepth,
        WriteUav,
        CopyDestination,
        ReadDependency,
        WriteDependency,
        Count,
    };

    constexpr uint32_t RenderGraphAccessBit(
        RenderGraphAccess access) noexcept {
        return access < RenderGraphAccess::Count
            ? 1u << static_cast<uint8_t>(access)
            : 0u;
    }

    inline constexpr uint32_t kRenderGraphPhysicalAccessMask =
        RenderGraphAccessBit(RenderGraphAccess::ReadSampled) |
        RenderGraphAccessBit(RenderGraphAccess::ReadDepth) |
        RenderGraphAccessBit(RenderGraphAccess::ReadUav) |
        RenderGraphAccessBit(RenderGraphAccess::CopySource) |
        RenderGraphAccessBit(RenderGraphAccess::WriteColor) |
        RenderGraphAccessBit(RenderGraphAccess::WriteDepth) |
        RenderGraphAccessBit(RenderGraphAccess::WriteUav) |
        RenderGraphAccessBit(RenderGraphAccess::CopyDestination);

    inline constexpr uint32_t kRenderGraphDependencyAccessMask =
        RenderGraphAccessBit(RenderGraphAccess::ReadDependency) |
        RenderGraphAccessBit(RenderGraphAccess::WriteDependency);

    enum class RenderGraphResourceKind : uint8_t {
        Internal = 0u,
        SceneSurface,
        CompletionMarker,
        Count,
    };

    enum class RenderGraphResourceRequirement : uint8_t {
        Required = 0u,
        Optional,
        Count,
    };

    enum class RenderGraphResultCode : uint8_t {
        Success = 0u,
        InvalidFrame,
        InvalidIdentifier,
        CapacityExceeded,
        DuplicateResourceId,
        DuplicatePassId,
        DuplicateSemantic,
        InvalidResource,
        InvalidAccess,
        AccessNotAllowed,
        SemanticContractMismatch,
        ProducerNotAllowed,
        DuplicateUse,
        ReadWriteConflict,
        DuplicateWriter,
        MissingProducer,
        CycleDetected,
        ExtensionRejected,
        NotCompiled,
        ExecuteFailed,
        MissingSurfacePublication,
        SurfacePublicationMismatch,
        MutationDuringExecution,
        InvalidExecutionState,
        PassNotFound,
        ExecutionAborted,
    };

    enum class RenderGraphExecutionStatus : uint8_t {
        NotStarted = 0u,
        Paused,
        Completed,
        Failed,
        Count,
    };

    struct RenderGraphExecutionState {
        RenderGraphExecutionStatus status =
            RenderGraphExecutionStatus::NotStarted;
        uint32_t nextOrder = 0u;
        uint32_t lastPassIndex = kInvalidRenderGraphIndex;
        uint32_t failurePassIndex = kInvalidRenderGraphIndex;
    };

    struct RenderGraphResourceId {
        uint32_t index = kInvalidRenderGraphIndex;
        uint64_t generation = 0u;
        uint64_t graphGeneration = 0u;

        bool IsValid() const noexcept {
            return index != kInvalidRenderGraphIndex &&
                generation != 0u &&
                graphGeneration != 0u;
        }

        friend bool operator==(
            RenderGraphResourceId lhs,
            RenderGraphResourceId rhs) noexcept {
            return lhs.index == rhs.index &&
                lhs.generation == rhs.generation &&
                lhs.graphGeneration == rhs.graphGeneration;
        }
    };

    struct RenderGraphResourceUse {
        RenderGraphResourceId resource{};
        RenderGraphAccess access = RenderGraphAccess::ReadSampled;
        SURFACE::SceneSurfaceProducer producer =
            SURFACE::SceneSurfaceProducer::None;
    };

    struct RenderGraphResourceDesc {
        std::string_view id{};
        RenderGraphSemantic semantic = RenderGraphSemantic::None;
        // Imported resources remain producer-managed: Stage 1 records access
        // and lifetime but never allocates, transitions, aliases, or releases
        // their GPU storage. Non-imported surfaces and completion markers
        // require one producer; consumed internal resources do as well.
        bool imported = false;
        RenderGraphResourceKind kind = RenderGraphResourceKind::Internal;
        // Internal resources must explicitly declare their permitted physical
        // accesses. SceneSurface resources derive access from the catalog;
        // completion markers only accept dependency accesses.
        uint32_t declaredAccessMask = 0u;
        RenderGraphSemanticPhase phase = RenderGraphSemanticPhase::None;
        RenderGraphResourceRequirement requirement =
            RenderGraphResourceRequirement::Required;
        SURFACE::SceneSurfaceProducer expectedProducer =
            SURFACE::SceneSurfaceProducer::None;
    };

    class RenderGraph;

    struct RenderGraphPassContext {
        const RenderGraph* graph = nullptr;
        RenderFrameToken frame{};
        uint32_t passIndex = kInvalidRenderGraphIndex;
        std::string_view passId{};
    };

    using RenderGraphExecuteFn = bool (*)(
        const RenderGraphPassContext& context,
        void* userData) noexcept;

    struct RenderGraphPassDesc {
        std::string_view id{};
        std::span<const RenderGraphResourceUse> reads{};
        std::span<const RenderGraphResourceUse> writes{};
        RenderGraphExecuteFn execute = nullptr;
        // Borrowed until execution reaches Completed/Failed or is explicitly
        // aborted. A phased graph must keep every not-yet-run callback state
        // in the same owning frame session as the graph.
        void* userData = nullptr;
    };

    struct RenderGraphDiagnostic {
        RenderGraphResultCode code = RenderGraphResultCode::Success;
        uint32_t passIndex = kInvalidRenderGraphIndex;
        uint32_t resourceIndex = kInvalidRenderGraphIndex;
        std::array<char, 192u> message{};

        bool Succeeded() const noexcept {
            return code == RenderGraphResultCode::Success;
        }
    };

    struct RenderGraphResourceInfo {
        RenderGraphSemantic semantic = RenderGraphSemantic::None;
        bool imported = false;
        RenderGraphResourceKind kind = RenderGraphResourceKind::Internal;
        uint32_t declaredAccessMask = 0u;
        RenderGraphSemanticPhase phase = RenderGraphSemanticPhase::None;
        RenderGraphResourceRequirement requirement =
            RenderGraphResourceRequirement::Required;
        SURFACE::SceneSurfaceProducer expectedProducer =
            SURFACE::SceneSurfaceProducer::None;
        uint32_t writerPassIndex = kInvalidRenderGraphIndex;
        uint32_t firstUseOrder = kInvalidRenderGraphIndex;
        uint32_t lastUseOrder = kInvalidRenderGraphIndex;
    };

    struct RenderGraphSurfaceBinding {
        SURFACE::SceneSurfaceDescriptor descriptor{};
        RenderTextureView texture{};
        DEPTH::DepthPyramidView depthPyramid{};
        bool valid = false;
    };

    // Fixed-capacity, frame/view-local graph. It owns dependency order and
    // resource-ID snapshots of producer-owned SceneSurface publications.
    // Allocation, barriers, descriptors, queues, materials, lights, and mesh
    // submission stay with their existing owners until the encoder stage.
    class RenderGraph final {
    public:
        RenderGraph() noexcept;
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;
        RenderGraph(RenderGraph&&) = delete;
        RenderGraph& operator=(RenderGraph&&) = delete;

        // Standalone graphs may allocate their own token. Any graph that
        // consumes SceneSurfaceSet resources must use the shared-token
        // overload below instead.
        bool BeginFrame(
            uint64_t frameIndex,
            RenderViewId viewId = kPrimaryRenderViewId) noexcept;
        bool BeginFrame(RenderFrameToken frame) noexcept;
        bool BeginFrame(SURFACE::SceneSurfaceSet& surfaces) noexcept;
        bool BeginFrame(
            SURFACE::SceneSurfaceSet& surfaces,
            SURFACE::RenderFrameSurfaceReceipt receipt) noexcept;

        RenderGraphResourceId AddResource(
            const RenderGraphResourceDesc& desc) noexcept;
        bool AddPass(const RenderGraphPassDesc& desc) noexcept;
        bool RejectBuild(
            RenderGraphResultCode code,
            const char* message) noexcept;
        bool Compile() noexcept;
        bool Execute() noexcept;
        // Executes through and including passId. A successful non-terminal
        // range enters Paused; continuation is forward-only and never replays.
        bool ExecuteThrough(std::string_view passId) noexcept;
        bool ContinueExecute() noexcept;
        bool AbortExecution() noexcept;

        RenderFrameToken GetFrameToken() const noexcept {
            return frame_;
        }
        uint32_t GetResourceCount() const noexcept {
            return resourceCount_;
        }
        uint32_t GetPassCount() const noexcept {
            return passCount_;
        }
        uint32_t GetCompiledPassCount() const noexcept {
            return compiledPassCount_;
        }
        bool IsCompiled() const noexcept {
            return compiled_;
        }
        RenderGraphExecutionState GetExecutionState() const noexcept {
            return executionState_;
        }
        bool IsResourceValid(
            RenderGraphResourceId resource) const noexcept;
        RenderGraphResourceId FindSemanticResource(
            RenderGraphSemantic semantic,
            RenderGraphSemanticPhase phase) const noexcept;
        bool GetResourceInfo(
            RenderGraphResourceId resource,
            RenderGraphResourceInfo& outInfo) const noexcept;
        bool GetSurfaceBinding(
            RenderGraphResourceId resource,
            RenderGraphSurfaceBinding& outBinding) const noexcept;
        bool GetCompiledPassIndex(
            uint32_t order,
            uint32_t& outPassIndex) const noexcept;
        std::string_view GetPassId(uint32_t passIndex) const noexcept;
        const RenderGraphDiagnostic& GetDiagnostic() const noexcept {
            return diagnostic_;
        }

    private:
        struct ResourceRecord {
            std::array<char, kRenderGraphIdentifierCapacity> id{};
            RenderGraphResourceInfo info{};
            RenderGraphSurfaceBinding binding{};
        };

        struct PassRecord {
            std::array<char, kRenderGraphIdentifierCapacity> id{};
            uint32_t readBegin = 0u;
            uint32_t readCount = 0u;
            uint32_t writeBegin = 0u;
            uint32_t writeCount = 0u;
            RenderGraphExecuteFn execute = nullptr;
            void* userData = nullptr;
        };

        bool ValidateIdentifier(std::string_view id) const noexcept;
        bool CopyIdentifier(
            std::string_view id,
            std::array<char, kRenderGraphIdentifierCapacity>& out) noexcept;
        bool ValidateResourceDesc(
            const RenderGraphResourceDesc& desc) noexcept;
        bool IsReadAccess(RenderGraphAccess access) const noexcept;
        bool IsWriteAccess(RenderGraphAccess access) const noexcept;
        bool IsAccessAllowed(
            const RenderGraphResourceInfo& resource,
            RenderGraphAccess access) const noexcept;
        bool PrepareImportedSurfaceBindings() noexcept;
        bool CaptureSurfaceBinding(
            uint32_t resourceIndex,
            const SURFACE::SceneSurfaceLease* baseline,
            uint32_t passIndex,
            bool write) noexcept;
        bool ValidateLiveSurfaceBinding() const noexcept;
        bool ExecuteCompiledRange(uint32_t endOrderExclusive) noexcept;
        void SetDiagnostic(
            RenderGraphResultCode code,
            const char* message,
            uint32_t passIndex = kInvalidRenderGraphIndex,
            uint32_t resourceIndex = kInvalidRenderGraphIndex) noexcept;
        void ClearDiagnostic() noexcept;

        RenderFrameToken frame_{};
        SURFACE::SceneSurfaceSet* sceneSurfaces_ = nullptr;
        SURFACE::RenderFrameSurfaceReceipt surfaceReceipt_{};
        uint64_t sceneSurfaceDeviceGeneration_ = 0u;
        uint64_t graphGeneration_ = 0u;
        std::array<ResourceRecord,
            kRenderGraphMaximumResourceCount> resources_{};
        std::array<PassRecord,
            kRenderGraphMaximumPassCount> passes_{};
        std::array<RenderGraphResourceUse,
            kRenderGraphMaximumResourceUseCount> uses_{};
        std::array<uint64_t,
            kRenderGraphMaximumPassCount> outgoingEdges_{};
        std::array<uint32_t,
            kRenderGraphMaximumPassCount> compiledOrder_{};
        RenderGraphDiagnostic diagnostic_{};
        uint32_t resourceCount_ = 0u;
        uint32_t passCount_ = 0u;
        uint32_t useCount_ = 0u;
        uint32_t compiledPassCount_ = 0u;
        RenderGraphExecutionState executionState_{};
        bool compiled_ = false;
        bool executing_ = false;
    };

    const char* ToString(RenderGraphSemantic semantic) noexcept;
    const char* ToString(RenderGraphSemanticPhase phase) noexcept;
    const char* ToString(RenderGraphResultCode code) noexcept;

    static_assert(
        sizeof(RenderGraph) <= 64u * 1024u,
        "RenderGraph Stage 1 metadata must remain at or below 64 KiB.");

} // namespace HIKARI::RENDER3D::GRAPH
