#include "Render3D/Graph/HIKARI_RenderGraph.h"

#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::GRAPH {

    bool RenderGraph::BeginFrame(
        SURFACE::SceneSurfaceSet& surfaces) noexcept {
        if (!BeginFrame(surfaces.GetFrameToken())) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph requires a valid SceneSurfaceSet frame.");
            return false;
        }
        if (!surfaces.IsFrameValid() ||
            !surfaces.MatchesFrame(frame_)) {
            frame_ = {};
            graphGeneration_ = 0u;
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph SceneSurfaceSet binding is inconsistent.");
            return false;
        }
        sceneSurfaces_ = &surfaces;
        sceneSurfaceDeviceGeneration_ = surfaces.GetDeviceGeneration();
        return true;
    }

    bool RenderGraph::BeginFrame(
        SURFACE::SceneSurfaceSet& surfaces,
        SURFACE::RenderFrameSurfaceReceipt receipt) noexcept {
        if (!receipt.IsValid() ||
            !receipt.frame.Matches(surfaces.GetFrameToken()) ||
            receipt.deviceGeneration != surfaces.GetDeviceGeneration() ||
            SURFACE::TryGetRenderFrameSurfaces(receipt) != &surfaces) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph requires a live matching surface receipt.");
            return false;
        }
        if (!BeginFrame(surfaces)) {
            return false;
        }
        surfaceReceipt_ = receipt;
        return true;
    }

    bool RenderGraph::ValidateLiveSurfaceBinding() const noexcept {
        if (sceneSurfaces_ == nullptr) {
            return true;
        }
        if (!sceneSurfaces_->MatchesFrame(frame_) ||
            sceneSurfaces_->GetDeviceGeneration() !=
                sceneSurfaceDeviceGeneration_) {
            return false;
        }
        return !surfaceReceipt_.IsValid() ||
            (surfaceReceipt_.frame.Matches(frame_) &&
             surfaceReceipt_.deviceGeneration ==
                sceneSurfaceDeviceGeneration_ &&
             SURFACE::TryGetRenderFrameSurfaces(surfaceReceipt_) ==
                sceneSurfaces_);
    }

    bool RenderGraph::CaptureSurfaceBinding(
        uint32_t resourceIndex,
        const SURFACE::SceneSurfaceLease* baseline,
        uint32_t passIndex,
        bool write) noexcept {
        ResourceRecord& record = resources_[resourceIndex];
        record.binding = {};
        if (sceneSurfaces_ == nullptr ||
            record.info.kind != RenderGraphResourceKind::SceneSurface) {
            return true;
        }
        SURFACE::SceneSurfaceDescriptor descriptor{};
        if (!sceneSurfaces_->TryDescribe(
                record.info.semantic,
                descriptor)) {
            if (record.info.requirement ==
                RenderGraphResourceRequirement::Optional) {
                return true;
            }
            SetDiagnostic(
                RenderGraphResultCode::MissingSurfacePublication,
                write
                    ? "RenderGraph pass did not publish a required surface."
                    : "RenderGraph requires an unavailable imported surface.",
                passIndex,
                resourceIndex);
            return false;
        }
        const SURFACE::SceneSurfacePublication& publication =
            descriptor.publication;
        if (descriptor.semantic.semantic != record.info.semantic ||
            publication.semantic != record.info.semantic ||
            !publication.lease.frame.Matches(frame_) ||
            publication.lease.deviceGeneration !=
                sceneSurfaceDeviceGeneration_ ||
            publication.producer != record.info.expectedProducer ||
            (baseline != nullptr && baseline->IsValid() &&
             publication.lease.Matches(*baseline))) {
            SetDiagnostic(
                RenderGraphResultCode::SurfacePublicationMismatch,
                "RenderGraph surface snapshot does not match its resource, "
                "frame, device, phase version, or producer contract.",
                passIndex,
                resourceIndex);
            return false;
        }

        RenderGraphSurfaceBinding binding{};
        binding.descriptor = descriptor;
        if (descriptor.semantic.resourceKind ==
                SURFACE::SceneSurfaceResourceKind::Texture2D) {
            const RenderTextureView* texture =
                sceneSurfaces_->TryGetTexture(
                    record.info.semantic,
                    frame_);
            if (texture == nullptr) {
                SetDiagnostic(
                    RenderGraphResultCode::SurfacePublicationMismatch,
                    "RenderGraph texture publication has no physical view.",
                    passIndex,
                    resourceIndex);
                return false;
            }
            binding.texture = *texture;
        }
        else {
            const DEPTH::DepthPyramidView* pyramid =
                sceneSurfaces_->TryGetDepthPyramid(
                    record.info.semantic,
                    frame_);
            if (pyramid == nullptr) {
                SetDiagnostic(
                    RenderGraphResultCode::SurfacePublicationMismatch,
                    "RenderGraph depth publication has no physical view.",
                    passIndex,
                    resourceIndex);
                return false;
            }
            binding.depthPyramid = *pyramid;
        }
        binding.valid = true;
        record.binding = binding;
        return true;
    }

    bool RenderGraph::PrepareImportedSurfaceBindings() noexcept {
        if (sceneSurfaces_ == nullptr) {
            return true;
        }
        if (!ValidateLiveSurfaceBinding()) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph surface frame or device changed before compile.");
            return false;
        }
        for (uint32_t resourceIndex = 0u;
            resourceIndex < resourceCount_;
            ++resourceIndex) {
            const RenderGraphResourceInfo& info =
                resources_[resourceIndex].info;
            if (info.kind == RenderGraphResourceKind::SceneSurface &&
                info.imported &&
                !CaptureSurfaceBinding(
                    resourceIndex,
                    nullptr,
                    kInvalidRenderGraphIndex,
                    false)) {
                return false;
            }
        }
        return true;
    }

    bool RenderGraph::Execute() noexcept {
        if (executionState_.status !=
                RenderGraphExecutionStatus::NotStarted) {
            if (executionState_.status !=
                    RenderGraphExecutionStatus::Failed) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidExecutionState,
                    "RenderGraph Execute cannot replay partial or completed execution.");
            }
            return false;
        }
        return ExecuteCompiledRange(compiledPassCount_);
    }

    bool RenderGraph::ExecuteThrough(std::string_view passId) noexcept {
        if (!compiled_) {
            SetDiagnostic(
                RenderGraphResultCode::NotCompiled,
                "RenderGraph phased execute requires a successful compile.");
            return false;
        }
        if (executionState_.status !=
                RenderGraphExecutionStatus::NotStarted &&
            executionState_.status != RenderGraphExecutionStatus::Paused) {
            if (executionState_.status !=
                    RenderGraphExecutionStatus::Failed) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidExecutionState,
                    "RenderGraph phased execute cannot continue this state.");
            }
            return false;
        }
        uint32_t targetOrder = kInvalidRenderGraphIndex;
        for (uint32_t order = 0u;
            order < compiledPassCount_;
            ++order) {
            const uint32_t passIndex = compiledOrder_[order];
            if (std::string_view(passes_[passIndex].id.data()) == passId) {
                targetOrder = order;
                break;
            }
        }
        if (targetOrder == kInvalidRenderGraphIndex) {
            SetDiagnostic(
                RenderGraphResultCode::PassNotFound,
                "RenderGraph phased execution target was not found.");
            return false;
        }
        if (targetOrder < executionState_.nextOrder) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidExecutionState,
                "RenderGraph phased execution cannot rewind completed passes.");
            return false;
        }
        return ExecuteCompiledRange(targetOrder + 1u);
    }

    bool RenderGraph::ContinueExecute() noexcept {
        if (executionState_.status != RenderGraphExecutionStatus::Paused) {
            if (executionState_.status !=
                    RenderGraphExecutionStatus::Failed) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidExecutionState,
                    "RenderGraph continue requires a paused execution.");
            }
            return false;
        }
        return ExecuteCompiledRange(compiledPassCount_);
    }

    bool RenderGraph::AbortExecution() noexcept {
        if (executing_) {
            SetDiagnostic(
                RenderGraphResultCode::MutationDuringExecution,
                "RenderGraph abort rejected during a pass callback.");
            return false;
        }
        if (executionState_.status !=
                RenderGraphExecutionStatus::NotStarted &&
            executionState_.status != RenderGraphExecutionStatus::Paused) {
            if (executionState_.status !=
                    RenderGraphExecutionStatus::Failed) {
                SetDiagnostic(
                    RenderGraphResultCode::InvalidExecutionState,
                    "RenderGraph abort requires a non-terminal execution.");
            }
            return false;
        }
        executionState_.status = RenderGraphExecutionStatus::Failed;
        executionState_.failurePassIndex = kInvalidRenderGraphIndex;
        SetDiagnostic(
            RenderGraphResultCode::ExecutionAborted,
            "RenderGraph execution was explicitly aborted.");
        return true;
    }

    bool RenderGraph::ExecuteCompiledRange(
        uint32_t endOrderExclusive) noexcept {
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
        if ((executionState_.status !=
                RenderGraphExecutionStatus::NotStarted &&
             executionState_.status !=
                RenderGraphExecutionStatus::Paused) ||
            endOrderExclusive < executionState_.nextOrder ||
            endOrderExclusive > compiledPassCount_) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidExecutionState,
                "RenderGraph execution range is invalid for its current state.");
            return false;
        }
        if (!ValidateLiveSurfaceBinding()) {
            executionState_.status = RenderGraphExecutionStatus::Failed;
            executionState_.failurePassIndex =
                executionState_.nextOrder < compiledPassCount_
                ? compiledOrder_[executionState_.nextOrder]
                : kInvalidRenderGraphIndex;
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph surface receipt changed before execute.");
            return false;
        }

        const auto validateRead = [this](
            const RenderGraphResourceUse& use,
            uint32_t passIndex) noexcept {
            const ResourceRecord& record = resources_[use.resource.index];
            if (sceneSurfaces_ == nullptr ||
                record.info.kind != RenderGraphResourceKind::SceneSurface) {
                return true;
            }
            if (!record.binding.valid) {
                if (record.info.requirement ==
                    RenderGraphResourceRequirement::Optional) {
                    if (sceneSurfaces_->TryGetPublication(
                            record.info.semantic) != nullptr) {
                        SetDiagnostic(
                            RenderGraphResultCode::
                                SurfacePublicationMismatch,
                            "An optional graph surface appeared after its "
                            "resource snapshot was captured.",
                            passIndex,
                            use.resource.index);
                        return false;
                    }
                    return true;
                }
                SetDiagnostic(
                    RenderGraphResultCode::MissingSurfacePublication,
                    "RenderGraph read has no resource-ID surface snapshot.",
                    passIndex,
                    use.resource.index);
                return false;
            }
            const auto& lease =
                record.binding.descriptor.publication.lease;
            if (!lease.frame.Matches(frame_) ||
                lease.deviceGeneration != sceneSurfaceDeviceGeneration_ ||
                record.binding.descriptor.publication.producer !=
                    record.info.expectedProducer ||
                !sceneSurfaces_->IsLeaseCurrent(
                    record.info.semantic,
                    lease)) {
                SetDiagnostic(
                    RenderGraphResultCode::SurfacePublicationMismatch,
                    "RenderGraph read snapshot is no longer current or "
                    "does not match its frame, device, or producer.",
                    passIndex,
                    use.resource.index);
                return false;
            }
            return true;
        };

        executing_ = true;
        for (uint32_t order = executionState_.nextOrder;
            order < endOrderExclusive;
            ++order) {
            const uint32_t passIndex = compiledOrder_[order];
            const PassRecord& pass = passes_[passIndex];
            if (!ValidateLiveSurfaceBinding()) {
                executing_ = false;
                executionState_.status =
                    RenderGraphExecutionStatus::Failed;
                executionState_.failurePassIndex = passIndex;
                SetDiagnostic(
                    RenderGraphResultCode::InvalidFrame,
                    "RenderGraph surface receipt changed before a pass.",
                    passIndex);
                return false;
            }
            for (uint32_t offset = 0u;
                offset < pass.readCount;
                ++offset) {
                if (!validateRead(
                        uses_[pass.readBegin + offset],
                        passIndex)) {
                    executing_ = false;
                    executionState_.status =
                        RenderGraphExecutionStatus::Failed;
                    executionState_.failurePassIndex = passIndex;
                    return false;
                }
            }
            std::array<SURFACE::SceneSurfaceLease,
                kRenderGraphMaximumResourceUseCount> writeBaselines{};
            if (sceneSurfaces_ != nullptr) {
                for (uint32_t offset = 0u;
                    offset < pass.writeCount;
                    ++offset) {
                    const RenderGraphResourceUse& use =
                        uses_[pass.writeBegin + offset];
                    if (resources_[use.resource.index].info.kind !=
                        RenderGraphResourceKind::SceneSurface) {
                        continue;
                    }
                    const auto* publication =
                        sceneSurfaces_->TryGetPublication(
                            resources_[use.resource.index].info.semantic);
                    if (publication != nullptr) {
                        writeBaselines[offset] = publication->lease;
                    }
                }
            }
            const RenderGraphPassContext context{
                this,
                frame_,
                passIndex,
                std::string_view(pass.id.data())
            };
            if (!pass.execute(context, pass.userData)) {
                executing_ = false;
                executionState_.status =
                    RenderGraphExecutionStatus::Failed;
                executionState_.failurePassIndex = passIndex;
                SetDiagnostic(
                    RenderGraphResultCode::ExecuteFailed,
                    "RenderGraph pass execute callback failed.",
                    passIndex);
                return false;
            }
            if (!ValidateLiveSurfaceBinding()) {
                executing_ = false;
                executionState_.status =
                    RenderGraphExecutionStatus::Failed;
                executionState_.failurePassIndex = passIndex;
                SetDiagnostic(
                    RenderGraphResultCode::InvalidFrame,
                    "RenderGraph surface receipt changed during a pass.",
                    passIndex);
                return false;
            }
            for (uint32_t offset = 0u;
                offset < pass.readCount;
                ++offset) {
                if (!validateRead(
                        uses_[pass.readBegin + offset],
                        passIndex)) {
                    executing_ = false;
                    executionState_.status =
                        RenderGraphExecutionStatus::Failed;
                    executionState_.failurePassIndex = passIndex;
                    return false;
                }
            }
            for (uint32_t offset = 0u;
                offset < pass.writeCount;
                ++offset) {
                const RenderGraphResourceUse& use =
                    uses_[pass.writeBegin + offset];
                if (!CaptureSurfaceBinding(
                        use.resource.index,
                        &writeBaselines[offset],
                        passIndex,
                        true)) {
                    executing_ = false;
                    executionState_.status =
                        RenderGraphExecutionStatus::Failed;
                    executionState_.failurePassIndex = passIndex;
                    return false;
                }
            }
            executionState_.lastPassIndex = passIndex;
            executionState_.nextOrder = order + 1u;
        }
        executing_ = false;
        executionState_.status =
            executionState_.nextOrder == compiledPassCount_
            ? RenderGraphExecutionStatus::Completed
            : RenderGraphExecutionStatus::Paused;
        ClearDiagnostic();
        return true;
    }

} // namespace HIKARI::RENDER3D::GRAPH
