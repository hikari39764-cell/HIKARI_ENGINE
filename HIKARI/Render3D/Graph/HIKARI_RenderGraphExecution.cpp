#include "Render3D/Graph/HIKARI_RenderGraph.h"

#include <algorithm>

namespace HIKARI::RENDER3D::GRAPH {

    bool RenderGraph::Compile() noexcept {
        if (executing_) {
            SetDiagnostic(
                RenderGraphResultCode::MutationDuringExecution,
                "RenderGraph compile rejected during execution.");
            return false;
        }
        if (!frame_.IsValid()) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph compile requires a valid frame token.");
            return false;
        }

        compiled_ = false;
        compiledPassCount_ = 0u;
        outgoingEdges_.fill(0u);
        compiledOrder_.fill(kInvalidRenderGraphIndex);
        for (uint32_t resourceIndex = 0u;
            resourceIndex < resourceCount_;
            ++resourceIndex) {
            RenderGraphResourceInfo& info =
                resources_[resourceIndex].info;
            info.writerPassIndex = kInvalidRenderGraphIndex;
            info.firstUseOrder = kInvalidRenderGraphIndex;
            info.lastUseOrder = kInvalidRenderGraphIndex;
        }

        for (uint32_t passIndex = 0u;
            passIndex < passCount_;
            ++passIndex) {
            const PassRecord& pass = passes_[passIndex];
            for (uint32_t offset = 0u;
                offset < pass.writeCount;
                ++offset) {
                const RenderGraphResourceUse& use =
                    uses_[pass.writeBegin + offset];
                RenderGraphResourceInfo& info =
                    resources_[use.resource.index].info;
                if (info.writerPassIndex != kInvalidRenderGraphIndex) {
                    SetDiagnostic(
                        RenderGraphResultCode::DuplicateWriter,
                        "RenderGraph resource has more than one writer.",
                        passIndex,
                        use.resource.index);
                    return false;
                }
                info.writerPassIndex = passIndex;
            }
        }

        std::array<uint32_t,
            kRenderGraphMaximumPassCount> indegrees{};
        for (uint32_t passIndex = 0u;
            passIndex < passCount_;
            ++passIndex) {
            const PassRecord& pass = passes_[passIndex];
            for (uint32_t offset = 0u;
                offset < pass.readCount;
                ++offset) {
                const RenderGraphResourceUse& use =
                    uses_[pass.readBegin + offset];
                const RenderGraphResourceInfo& info =
                    resources_[use.resource.index].info;
                if (info.writerPassIndex == kInvalidRenderGraphIndex) {
                    if (!info.imported) {
                        SetDiagnostic(
                            RenderGraphResultCode::MissingProducer,
                            "RenderGraph read has no producer or imported source.",
                            passIndex,
                            use.resource.index);
                        return false;
                    }
                    continue;
                }
                const uint64_t consumerBit = 1ull << passIndex;
                uint64_t& outgoing =
                    outgoingEdges_[info.writerPassIndex];
                if ((outgoing & consumerBit) == 0u) {
                    outgoing |= consumerBit;
                    ++indegrees[passIndex];
                }
            }
        }

        uint64_t selectedMask = 0u;
        for (uint32_t order = 0u; order < passCount_; ++order) {
            uint32_t selected = kInvalidRenderGraphIndex;
            for (uint32_t candidate = 0u;
                candidate < passCount_;
                ++candidate) {
                const uint64_t bit = 1ull << candidate;
                if ((selectedMask & bit) == 0u &&
                    indegrees[candidate] == 0u) {
                    selected = candidate;
                    break;
                }
            }
            if (selected == kInvalidRenderGraphIndex) {
                SetDiagnostic(
                    RenderGraphResultCode::CycleDetected,
                    "RenderGraph pass dependency cycle detected.");
                return false;
            }
            compiledOrder_[order] = selected;
            selectedMask |= 1ull << selected;
            uint64_t outgoing = outgoingEdges_[selected];
            while (outgoing != 0u) {
                uint32_t target = 0u;
                while ((outgoing & (1ull << target)) == 0u) {
                    ++target;
                }
                outgoing &= ~(1ull << target);
                if (indegrees[target] > 0u) {
                    --indegrees[target];
                }
            }
        }

        for (uint32_t order = 0u; order < passCount_; ++order) {
            const PassRecord& pass = passes_[compiledOrder_[order]];
            const auto recordUse = [&](const RenderGraphResourceUse& use) {
                RenderGraphResourceInfo& info =
                    resources_[use.resource.index].info;
                info.firstUseOrder = info.firstUseOrder ==
                        kInvalidRenderGraphIndex
                    ? order
                    : (std::min)(info.firstUseOrder, order);
                info.lastUseOrder = info.lastUseOrder ==
                        kInvalidRenderGraphIndex
                    ? order
                    : (std::max)(info.lastUseOrder, order);
            };
            for (uint32_t offset = 0u;
                offset < pass.readCount;
                ++offset) {
                recordUse(uses_[pass.readBegin + offset]);
            }
            for (uint32_t offset = 0u;
                offset < pass.writeCount;
                ++offset) {
                recordUse(uses_[pass.writeBegin + offset]);
            }
        }

        compiledPassCount_ = passCount_;
        compiled_ = true;
        ClearDiagnostic();
        return true;
    }

    bool RenderGraph::Execute() noexcept {
        if (!compiled_) {
            SetDiagnostic(
                RenderGraphResultCode::NotCompiled,
                "RenderGraph execute requires a successful compile.");
            return false;
        }
        if (executing_) {
            SetDiagnostic(
                RenderGraphResultCode::MutationDuringExecution,
                "RenderGraph recursive execution rejected.");
            return false;
        }

        executing_ = true;
        for (uint32_t order = 0u;
            order < compiledPassCount_;
            ++order) {
            const uint32_t passIndex = compiledOrder_[order];
            const PassRecord& pass = passes_[passIndex];
            const RenderGraphPassContext context{
                this,
                frame_,
                passIndex,
                std::string_view(pass.id.data())
            };
            if (!pass.execute(context, pass.userData)) {
                executing_ = false;
                SetDiagnostic(
                    RenderGraphResultCode::ExecuteFailed,
                    "RenderGraph pass execute callback failed.",
                    passIndex);
                return false;
            }
        }
        executing_ = false;
        ClearDiagnostic();
        return true;
    }

} // namespace HIKARI::RENDER3D::GRAPH
