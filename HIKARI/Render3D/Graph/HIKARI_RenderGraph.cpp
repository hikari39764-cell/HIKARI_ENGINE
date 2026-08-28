#include "Render3D/Graph/HIKARI_RenderGraph.h"

#include <cstdio>
#include <cstring>

namespace HIKARI::RENDER3D::GRAPH {

    RenderGraph::RenderGraph() noexcept = default;

    bool RenderGraph::BeginFrame(
        uint64_t frameIndex,
        RenderViewId viewId) noexcept {
        if (executing_) {
            SetDiagnostic(
                RenderGraphResultCode::MutationDuringExecution,
                "RenderGraph frame reset rejected during execution.");
            return false;
        }

        uint32_t generation = frame_.generation + 1u;
        if (generation == 0u) {
            generation = 1u;
        }
        frame_ = RenderGraphFrameToken{
            frameIndex,
            viewId,
            generation
        };
        resourceCount_ = 0u;
        passCount_ = 0u;
        useCount_ = 0u;
        compiledPassCount_ = 0u;
        compiled_ = false;
        outgoingEdges_.fill(0u);
        compiledOrder_.fill(kInvalidRenderGraphIndex);
        if (!frame_.IsValid()) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph requires a non-zero RenderViewId.");
            return false;
        }
        ClearDiagnostic();
        return true;
    }

    RenderGraphResourceId RenderGraph::AddResource(
        const RenderGraphResourceDesc& desc) noexcept {
        if (executing_) {
            SetDiagnostic(
                RenderGraphResultCode::MutationDuringExecution,
                "RenderGraph resource mutation rejected during execution.");
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
        if (desc.semantic >= RenderGraphSemantic::Count) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidResource,
                "RenderGraph resource semantic is invalid.");
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
            if (desc.semantic != RenderGraphSemantic::None &&
                existing.info.semantic == desc.semantic) {
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
        const RenderGraphResourceId result{
            resourceCount_,
            frame_.generation
        };
        ++resourceCount_;
        compiled_ = false;
        compiledPassCount_ = 0u;
        ClearDiagnostic();
        return result;
    }

    bool RenderGraph::AddPass(
        const RenderGraphPassDesc& desc) noexcept {
        if (executing_) {
            SetDiagnostic(
                RenderGraphResultCode::MutationDuringExecution,
                "RenderGraph pass mutation rejected during execution.");
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
            if (!IsReadAccess(use.access)) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidAccess,
                    "RenderGraph read declaration uses write access.",
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
            if (!IsWriteAccess(use.access)) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidAccess,
                    "RenderGraph write declaration uses read access.",
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
            resource.index < resourceCount_;
    }

    RenderGraphResourceId RenderGraph::FindSemanticResource(
        RenderGraphSemantic semantic) const noexcept {
        if (!frame_.IsValid() ||
            semantic == RenderGraphSemantic::None ||
            semantic >= RenderGraphSemantic::Count) {
            return {};
        }
        for (uint32_t index = 0u;
            index < resourceCount_;
            ++index) {
            if (resources_[index].info.semantic == semantic) {
                return RenderGraphResourceId{
                    index,
                    frame_.generation
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

    bool RenderGraph::IsReadAccess(
        RenderGraphAccess access) const noexcept {
        switch (access) {
        case RenderGraphAccess::ReadSampled:
        case RenderGraphAccess::ReadDepth:
        case RenderGraphAccess::ReadUav:
        case RenderGraphAccess::CopySource:
            return true;
        default:
            return false;
        }
    }

    bool RenderGraph::IsWriteAccess(
        RenderGraphAccess access) const noexcept {
        switch (access) {
        case RenderGraphAccess::WriteColor:
        case RenderGraphAccess::WriteDepth:
        case RenderGraphAccess::WriteUav:
        case RenderGraphAccess::CopyDestination:
            return true;
        default:
            return false;
        }
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

    const char* ToString(RenderGraphSemantic semantic) noexcept {
        switch (semantic) {
        case RenderGraphSemantic::None: return "None";
        case RenderGraphSemantic::SceneDepth: return "SceneDepth";
        case RenderGraphSemantic::SceneColorOpaque:
            return "SceneColorOpaque";
        case RenderGraphSemantic::SceneColorPostOpaque:
            return "SceneColorPostOpaque";
        case RenderGraphSemantic::SceneColorFinal:
            return "SceneColorFinal";
        case RenderGraphSemantic::PackedSurface: return "PackedSurface";
        case RenderGraphSemantic::SurfaceFeatureMask:
            return "SurfaceFeatureMask";
        case RenderGraphSemantic::MotionVectors: return "MotionVectors";
        case RenderGraphSemantic::MotionMetadata: return "MotionMetadata";
        case RenderGraphSemantic::CurrentDepthPyramid:
            return "CurrentDepthPyramid";
        case RenderGraphSemantic::VisibilityDepthPyramid:
            return "VisibilityDepthPyramid";
        case RenderGraphSemantic::HistoryColor: return "HistoryColor";
        case RenderGraphSemantic::HistoryDepth: return "HistoryDepth";
        case RenderGraphSemantic::Count: break;
        }
        return "Unknown";
    }

    const char* ToString(RenderGraphResultCode code) noexcept {
        switch (code) {
        case RenderGraphResultCode::Success: return "Success";
        case RenderGraphResultCode::InvalidFrame: return "InvalidFrame";
        case RenderGraphResultCode::InvalidIdentifier:
            return "InvalidIdentifier";
        case RenderGraphResultCode::CapacityExceeded:
            return "CapacityExceeded";
        case RenderGraphResultCode::DuplicateResourceId:
            return "DuplicateResourceId";
        case RenderGraphResultCode::DuplicatePassId:
            return "DuplicatePassId";
        case RenderGraphResultCode::DuplicateSemantic:
            return "DuplicateSemantic";
        case RenderGraphResultCode::InvalidResource:
            return "InvalidResource";
        case RenderGraphResultCode::InvalidAccess: return "InvalidAccess";
        case RenderGraphResultCode::DuplicateUse: return "DuplicateUse";
        case RenderGraphResultCode::ReadWriteConflict:
            return "ReadWriteConflict";
        case RenderGraphResultCode::DuplicateWriter:
            return "DuplicateWriter";
        case RenderGraphResultCode::MissingProducer:
            return "MissingProducer";
        case RenderGraphResultCode::CycleDetected: return "CycleDetected";
        case RenderGraphResultCode::NotCompiled: return "NotCompiled";
        case RenderGraphResultCode::ExecuteFailed: return "ExecuteFailed";
        case RenderGraphResultCode::MutationDuringExecution:
            return "MutationDuringExecution";
        }
        return "Unknown";
    }

} // namespace HIKARI::RENDER3D::GRAPH
