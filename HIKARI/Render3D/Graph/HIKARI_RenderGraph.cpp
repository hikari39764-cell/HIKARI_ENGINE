#include "Render3D/Graph/HIKARI_RenderGraph.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace HIKARI::RENDER3D::GRAPH {
namespace {

    std::atomic<uint64_t> gNextRenderGraphGeneration{ 1u };

    uint64_t AllocateRenderGraphGeneration() noexcept {
        uint64_t generation = 0u;
        do {
            generation = gNextRenderGraphGeneration.fetch_add(
                1u,
                std::memory_order_relaxed);
        } while (generation == 0u);
        return generation;
    }

} // namespace

    RenderGraph::RenderGraph() noexcept = default;

    bool RenderGraph::BeginFrame(
        uint64_t frameIndex,
        RenderViewId viewId) noexcept {
        return BeginFrame(AllocateRenderFrameToken(frameIndex, viewId));
    }

    bool RenderGraph::BeginFrame(
        RenderFrameToken frame) noexcept {
        if (executing_ ||
            executionState_.status == RenderGraphExecutionStatus::Paused) {
            SetDiagnostic(
                executing_
                    ? RenderGraphResultCode::MutationDuringExecution
                    : RenderGraphResultCode::InvalidExecutionState,
                "RenderGraph frame reset rejected while execution is active or paused.");
            return false;
        }
        frame_ = frame;
        sceneSurfaces_ = nullptr;
        surfaceReceipt_ = {};
        sceneSurfaceDeviceGeneration_ = 0u;
        graphGeneration_ = 0u;
        resourceCount_ = 0u;
        passCount_ = 0u;
        useCount_ = 0u;
        compiledPassCount_ = 0u;
        executionState_ = {};
        compiled_ = false;
        outgoingEdges_.fill(0u);
        compiledOrder_.fill(kInvalidRenderGraphIndex);
        if (!frame_.IsValid()) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph requires a valid frame token.");
            return false;
        }
        graphGeneration_ = AllocateRenderGraphGeneration();
        ClearDiagnostic();
        return true;
    }

    RenderGraphResourceId RenderGraph::AddResource(
        const RenderGraphResourceDesc& desc) noexcept {
        if (executing_ ||
            executionState_.status !=
                RenderGraphExecutionStatus::NotStarted) {
            if (!executing_ && executionState_.status ==
                    RenderGraphExecutionStatus::Failed) {
                return {};
            }
            SetDiagnostic(
                executing_
                    ? RenderGraphResultCode::MutationDuringExecution
                    : RenderGraphResultCode::InvalidExecutionState,
                "RenderGraph resource mutation rejected after execution began.");
            return {};
        }
        if (!frame_.IsValid()) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph resource requires a valid frame token.");
            return {};
        }
        if (!ValidateIdentifier(desc.id)) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidIdentifier,
                "RenderGraph resource id is empty, too long, or contains NUL.");
            return {};
        }
        if (!ValidateResourceDesc(desc)) {
            return {};
        }
        if (resourceCount_ >= kRenderGraphMaximumResourceCount) {
            SetDiagnostic(
                RenderGraphResultCode::CapacityExceeded,
                "RenderGraph resource capacity exceeded.");
            return {};
        }

        for (uint32_t index = 0u;
            index < resourceCount_;
            ++index) {
            const ResourceRecord& existing = resources_[index];
            if (std::string_view(existing.id.data()) == desc.id) {
                SetDiagnostic(
                    RenderGraphResultCode::DuplicateResourceId,
                    "RenderGraph resource id is already registered.",
                    kInvalidRenderGraphIndex,
                    index);
                return {};
            }
            if (desc.kind == RenderGraphResourceKind::SceneSurface &&
                existing.info.semantic == desc.semantic &&
                existing.info.phase == desc.phase) {
                SetDiagnostic(
                    RenderGraphResultCode::DuplicateSemantic,
                    "RenderGraph semantic already has a resource.",
                    kInvalidRenderGraphIndex,
                    index);
                return {};
            }
        }

        ResourceRecord& record = resources_[resourceCount_];
        record = {};
        if (!CopyIdentifier(desc.id, record.id)) {
            return {};
        }
        record.info.semantic = desc.semantic;
        record.info.imported = desc.imported;
        record.info.kind = desc.kind;
        record.info.declaredAccessMask = desc.declaredAccessMask;
        record.info.phase = desc.phase;
        record.info.requirement = desc.requirement;
        record.info.expectedProducer = desc.expectedProducer;
        const RenderGraphResourceId result{
            resourceCount_,
            frame_.generation,
            graphGeneration_
        };
        ++resourceCount_;
        compiled_ = false;
        compiledPassCount_ = 0u;
        ClearDiagnostic();
        return result;
    }

    bool RenderGraph::AddPass(
        const RenderGraphPassDesc& desc) noexcept {
        if (executing_ ||
            executionState_.status !=
                RenderGraphExecutionStatus::NotStarted) {
            if (!executing_ && executionState_.status ==
                    RenderGraphExecutionStatus::Failed) {
                return false;
            }
            SetDiagnostic(
                executing_
                    ? RenderGraphResultCode::MutationDuringExecution
                    : RenderGraphResultCode::InvalidExecutionState,
                "RenderGraph pass mutation rejected after execution began.");
            return false;
        }
        if (!frame_.IsValid()) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph pass requires a valid frame token.");
            return false;
        }
        if (!ValidateIdentifier(desc.id) || desc.execute == nullptr) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidIdentifier,
                "RenderGraph pass requires a valid id and execute callback.");
            return false;
        }
        const size_t remainingUseCapacity =
            kRenderGraphMaximumResourceUseCount - useCount_;
        if (passCount_ >= kRenderGraphMaximumPassCount ||
            desc.reads.size() > remainingUseCapacity ||
            desc.writes.size() >
                remainingUseCapacity - desc.reads.size()) {
            SetDiagnostic(
                RenderGraphResultCode::CapacityExceeded,
                "RenderGraph pass or resource-use capacity exceeded.");
            return false;
        }
        for (uint32_t index = 0u; index < passCount_; ++index) {
            if (std::string_view(passes_[index].id.data()) == desc.id) {
                SetDiagnostic(
                    RenderGraphResultCode::DuplicatePassId,
                    "RenderGraph pass id is already registered.",
                    index);
                return false;
            }
        }

        uint64_t readMask = 0u;
        uint64_t writeMask = 0u;
        for (const RenderGraphResourceUse& use : desc.reads) {
            if (!IsResourceValid(use.resource)) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidResource,
                    "RenderGraph pass reads an invalid resource.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            if (use.producer != SURFACE::SceneSurfaceProducer::None) {
                SetDiagnostic(
                    RenderGraphResultCode::ProducerNotAllowed,
                    "RenderGraph reads cannot declare a surface producer.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            if (!IsReadAccess(use.access)) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidAccess,
                    "RenderGraph read declaration uses write access.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            if (!IsAccessAllowed(
                    resources_[use.resource.index].info,
                    use.access)) {
                SetDiagnostic(
                    RenderGraphResultCode::AccessNotAllowed,
                    "RenderGraph read access is not allowed by the semantic.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            const uint64_t bit = 1ull << use.resource.index;
            if ((readMask & bit) != 0u) {
                SetDiagnostic(
                    RenderGraphResultCode::DuplicateUse,
                    "RenderGraph pass declares the same read twice.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            readMask |= bit;
        }
        for (const RenderGraphResourceUse& use : desc.writes) {
            if (!IsResourceValid(use.resource)) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidResource,
                    "RenderGraph pass writes an invalid resource.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            const RenderGraphResourceInfo& resourceInfo =
                resources_[use.resource.index].info;
            const bool writesSceneSurface = resourceInfo.kind ==
                RenderGraphResourceKind::SceneSurface;
            if ((writesSceneSurface &&
                 use.producer != resourceInfo.expectedProducer) ||
                (!writesSceneSurface &&
                 use.producer !=
                    SURFACE::SceneSurfaceProducer::None)) {
                SetDiagnostic(
                    RenderGraphResultCode::ProducerNotAllowed,
                    "RenderGraph writer does not match the resource producer contract.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            if (!IsWriteAccess(use.access)) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidAccess,
                    "RenderGraph write declaration uses read access.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            if (!IsAccessAllowed(
                    resources_[use.resource.index].info,
                    use.access)) {
                SetDiagnostic(
                    RenderGraphResultCode::AccessNotAllowed,
                    "RenderGraph write access is not allowed by the semantic.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            const uint64_t bit = 1ull << use.resource.index;
            if ((writeMask & bit) != 0u) {
                SetDiagnostic(
                    RenderGraphResultCode::DuplicateUse,
                    "RenderGraph pass declares the same write twice.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            if ((readMask & bit) != 0u) {
                SetDiagnostic(
                    RenderGraphResultCode::ReadWriteConflict,
                    "RenderGraph pass cannot read and write one resource version.",
                    passCount_,
                    use.resource.index);
                return false;
            }
            writeMask |= bit;
        }

        PassRecord& record = passes_[passCount_];
        record = {};
        if (!CopyIdentifier(desc.id, record.id)) {
            return false;
        }
        record.readBegin = useCount_;
        record.readCount = static_cast<uint32_t>(desc.reads.size());
        for (const RenderGraphResourceUse& use : desc.reads) {
            uses_[useCount_++] = use;
        }
        record.writeBegin = useCount_;
        record.writeCount = static_cast<uint32_t>(desc.writes.size());
        for (const RenderGraphResourceUse& use : desc.writes) {
            uses_[useCount_++] = use;
        }
        record.execute = desc.execute;
        record.userData = desc.userData;
        ++passCount_;
        compiled_ = false;
        compiledPassCount_ = 0u;
        ClearDiagnostic();
        return true;
    }

    bool RenderGraph::IsResourceValid(
        RenderGraphResourceId resource) const noexcept {
        return frame_.IsValid() &&
            resource.IsValid() &&
            resource.generation == frame_.generation &&
            resource.graphGeneration == graphGeneration_ &&
            resource.index < resourceCount_;
    }

    RenderGraphResourceId RenderGraph::FindSemanticResource(
        RenderGraphSemantic semantic,
        RenderGraphSemanticPhase phase) const noexcept {
        if (!frame_.IsValid() ||
            semantic == RenderGraphSemantic::None ||
            semantic >= RenderGraphSemantic::Count ||
            phase == RenderGraphSemanticPhase::None ||
            phase >= RenderGraphSemanticPhase::Count) {
            return {};
        }
        for (uint32_t index = 0u;
             index < resourceCount_;
             ++index) {
            if (resources_[index].info.semantic == semantic &&
                resources_[index].info.phase == phase) {
                return RenderGraphResourceId{
                    index,
                    frame_.generation,
                    graphGeneration_
                };
            }
        }
        return {};
    }

    bool RenderGraph::GetResourceInfo(
        RenderGraphResourceId resource,
        RenderGraphResourceInfo& outInfo) const noexcept {
        if (!IsResourceValid(resource)) {
            return false;
        }
        outInfo = resources_[resource.index].info;
        if (!compiled_) {
            outInfo.writerPassIndex = kInvalidRenderGraphIndex;
            outInfo.firstUseOrder = kInvalidRenderGraphIndex;
            outInfo.lastUseOrder = kInvalidRenderGraphIndex;
        }
        return true;
    }

    bool RenderGraph::GetSurfaceBinding(
        RenderGraphResourceId resource,
        RenderGraphSurfaceBinding& outBinding) const noexcept {
        outBinding = {};
        if (!compiled_ || !ValidateLiveSurfaceBinding() ||
            !IsResourceValid(resource) ||
            resources_[resource.index].info.kind !=
                RenderGraphResourceKind::SceneSurface ||
            !resources_[resource.index].binding.valid) {
            return false;
        }
        const RenderGraphSurfaceBinding& binding =
            resources_[resource.index].binding;
        if (sceneSurfaces_ != nullptr &&
            !sceneSurfaces_->IsLeaseCurrent(
                resources_[resource.index].info.semantic,
                binding.descriptor.publication.lease)) {
            return false;
        }
        outBinding = binding;
        return true;
    }

    bool RenderGraph::GetCompiledPassIndex(
        uint32_t order,
        uint32_t& outPassIndex) const noexcept {
        if (!compiled_ || order >= compiledPassCount_) {
            return false;
        }
        outPassIndex = compiledOrder_[order];
        return true;
    }

    std::string_view RenderGraph::GetPassId(
        uint32_t passIndex) const noexcept {
        return passIndex < passCount_
            ? std::string_view(passes_[passIndex].id.data())
            : std::string_view{};
    }

    bool RenderGraph::ValidateIdentifier(
        std::string_view id) const noexcept {
        return !id.empty() &&
            id.size() < kRenderGraphIdentifierCapacity &&
            id.find('\0') == std::string_view::npos;
    }

    bool RenderGraph::CopyIdentifier(
        std::string_view id,
        std::array<char, kRenderGraphIdentifierCapacity>& out) noexcept {
        if (!ValidateIdentifier(id)) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidIdentifier,
                "RenderGraph identifier copy rejected.");
            return false;
        }
        out.fill('\0');
        std::memcpy(out.data(), id.data(), id.size());
        return true;
    }

    void RenderGraph::SetDiagnostic(
        RenderGraphResultCode code,
        const char* message,
        uint32_t passIndex,
        uint32_t resourceIndex) noexcept {
        diagnostic_ = {};
        diagnostic_.code = code;
        diagnostic_.passIndex = passIndex;
        diagnostic_.resourceIndex = resourceIndex;
        if (message != nullptr) {
            std::snprintf(
                diagnostic_.message.data(),
                diagnostic_.message.size(),
                "%s",
                message);
        }
    }

    void RenderGraph::ClearDiagnostic() noexcept {
        diagnostic_ = {};
    }

} // namespace HIKARI::RENDER3D::GRAPH
