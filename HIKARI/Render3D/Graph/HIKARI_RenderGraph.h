#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "Render3D/Core/HIKARI_RenderFrameToken.h"

namespace HIKARI::RENDER3D::GRAPH {

    inline constexpr uint32_t kRenderGraphMaximumResourceCount = 64u;
    inline constexpr uint32_t kRenderGraphMaximumPassCount = 64u;
    inline constexpr uint32_t kRenderGraphMaximumResourceUseCount = 128u;
    inline constexpr size_t kRenderGraphIdentifierCapacity = 64u;
    inline constexpr uint32_t kInvalidRenderGraphIndex =
        (std::numeric_limits<uint32_t>::max)();

    enum class RenderGraphSemantic : uint16_t {
        None = 0u,
        SceneDepth,
        SceneColorOpaque,
        SceneColorPostOpaque,
        SceneColorFinal,
        PackedSurface,
        SurfaceFeatureMask,
        MotionVectors,
        MotionMetadata,
        CurrentDepthPyramid,
        VisibilityDepthPyramid,
        HistoryColor,
        HistoryDepth,
        StylizedOutlineStyle,
        AmbientOcclusion,
        ReactiveMask,
        TransparencyMask,
        InvalidDepthMotionMask,
        Count,
    };

    enum class RenderGraphAccess : uint8_t {
        ReadSampled = 0u,
        ReadDepth,
        ReadUav,
        CopySource,
        WriteColor,
        WriteDepth,
        WriteUav,
        CopyDestination,
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
        DuplicateUse,
        ReadWriteConflict,
        DuplicateWriter,
        MissingProducer,
        CycleDetected,
        NotCompiled,
        ExecuteFailed,
        MutationDuringExecution,
    };

    using RenderGraphFrameToken = RenderFrameToken;

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
    };

    struct RenderGraphResourceDesc {
        std::string_view id{};
        RenderGraphSemantic semantic = RenderGraphSemantic::None;
        // Imported resources remain producer-managed: Stage 1 records access
        // and lifetime but never allocates, transitions, aliases, or releases
        // their GPU storage. Non-imported records are logical versions that
        // require one producer pass; they do not allocate GPU storage either.
        bool imported = false;
    };

    class RenderGraph;

    struct RenderGraphPassContext {
        const RenderGraph* graph = nullptr;
        RenderGraphFrameToken frame{};
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
        uint32_t writerPassIndex = kInvalidRenderGraphIndex;
        uint32_t firstUseOrder = kInvalidRenderGraphIndex;
        uint32_t lastUseOrder = kInvalidRenderGraphIndex;
    };

    // Fixed-capacity, frame/view-local pass planner. Stage 1 owns dependency
    // validation and execution order only; Scene, GPU resources, barriers,
    // descriptors, queues, materials, lights, and mesh submission stay with
    // their existing owners.
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
        bool BeginFrame(RenderGraphFrameToken frame) noexcept;

        RenderGraphResourceId AddResource(
            const RenderGraphResourceDesc& desc) noexcept;
        bool AddPass(const RenderGraphPassDesc& desc) noexcept;
        bool Compile() noexcept;
        bool Execute() noexcept;

        RenderGraphFrameToken GetFrameToken() const noexcept {
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
        bool IsResourceValid(
            RenderGraphResourceId resource) const noexcept;
        RenderGraphResourceId FindSemanticResource(
            RenderGraphSemantic semantic) const noexcept;
        bool GetResourceInfo(
            RenderGraphResourceId resource,
            RenderGraphResourceInfo& outInfo) const noexcept;
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
        bool IsReadAccess(RenderGraphAccess access) const noexcept;
        bool IsWriteAccess(RenderGraphAccess access) const noexcept;
        void SetDiagnostic(
            RenderGraphResultCode code,
            const char* message,
            uint32_t passIndex = kInvalidRenderGraphIndex,
            uint32_t resourceIndex = kInvalidRenderGraphIndex) noexcept;
        void ClearDiagnostic() noexcept;

        RenderGraphFrameToken frame_{};
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
        bool compiled_ = false;
        bool executing_ = false;
    };

    const char* ToString(RenderGraphSemantic semantic) noexcept;
    const char* ToString(RenderGraphResultCode code) noexcept;

    static_assert(
        sizeof(RenderGraph) <= 64u * 1024u,
        "RenderGraph Stage 1 metadata must remain at or below 64 KiB.");

} // namespace HIKARI::RENDER3D::GRAPH
