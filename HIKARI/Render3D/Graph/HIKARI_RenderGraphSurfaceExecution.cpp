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
        if (!sceneSurfaces_->MatchesFrame(frame_) ||
            sceneSurfaces_->GetDeviceGeneration() !=
                sceneSurfaceDeviceGeneration_) {
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
        if (sceneSurfaces_ != nullptr &&
            (!sceneSurfaces_->MatchesFrame(frame_) ||
             sceneSurfaces_->GetDeviceGeneration() !=
                sceneSurfaceDeviceGeneration_)) {
            SetDiagnostic(
                RenderGraphResultCode::InvalidFrame,
                "RenderGraph surface frame or device changed before execute.");
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
        for (uint32_t order = 0u;
            order < compiledPassCount_;
            ++order) {
            const uint32_t passIndex = compiledOrder_[order];
            const PassRecord& pass = passes_[passIndex];
            for (uint32_t offset = 0u;
                offset < pass.readCount;
                ++offset) {
                if (!validateRead(
                        uses_[pass.readBegin + offset],
                        passIndex)) {
                    executing_ = false;
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
                SetDiagnostic(
                    RenderGraphResultCode::ExecuteFailed,
                    "RenderGraph pass execute callback failed.",
                    passIndex);
                return false;
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
                    return false;
                }
            }
        }
        executing_ = false;
        ClearDiagnostic();
        return true;
    }

} // namespace HIKARI::RENDER3D::GRAPH
