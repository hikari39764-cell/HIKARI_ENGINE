#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        GpuDrivenPassKind PassFromIndex(size_t index) {
            return static_cast<GpuDrivenPassKind>(
                index < kGpuDrivenPassCount
                    ? index
                    : kGpuDrivenPassCount - 1u);
        }
    }

    size_t ToDrawCommandBackendSlot(GeometryBackendKind backend) {
        const size_t slot = static_cast<size_t>(backend);
        return slot < kGpuDrivenDrawCommandBackendSlotCount
            ? slot
            : 0u;
    }

    void GpuDrivenDrawCommandRange::Reset() {
        const GpuDrivenPassKind oldPass = pass;
        *this = {};
        pass = oldPass;
        sourcePass = oldPass;
    }

    bool GpuDrivenDrawCommandRange::IsActive() const {
        return consumable &&
            producer != GpuDrivenCommandProducerKind::None &&
            commandCount != 0;
    }

    bool GpuDrivenDrawCommandRange::HasTraditionalIndirectView() const {
        return traditionalIndirect != nullptr && commandCount != 0;
    }

    bool GpuDrivenDrawCommandRange::HasGpuCommandLayout() const {
        return gpuCommandLayout != nullptr &&
            argumentBuffer != nullptr &&
            commandSignature != nullptr &&
            commandCount != 0;
    }

    void GpuDrivenPassDrawCommandStream::Reset(GpuDrivenPassKind pass) {
        for (size_t i = 0; i < ranges.size(); ++i) {
            GpuDrivenDrawCommandRange& range = ranges[i];
            range = {};
            range.pass = pass;
            range.sourcePass = pass;
            range.backend = static_cast<GeometryBackendKind>(i);
        }
    }

    GpuDrivenDrawCommandRange&
        GpuDrivenPassDrawCommandStream::GetOrCreateRange(
            GeometryBackendKind backend) {

        GpuDrivenDrawCommandRange& range =
            ranges[ToDrawCommandBackendSlot(backend)];
        range.backend = backend;
        return range;
    }

    const GpuDrivenDrawCommandRange*
        GpuDrivenPassDrawCommandStream::FindRange(
            GeometryBackendKind backend) const {

        const GpuDrivenDrawCommandRange& range =
            ranges[ToDrawCommandBackendSlot(backend)];
        return range.backend == backend && range.IsActive()
            ? &range
            : nullptr;
    }

    GpuDrivenDrawCommandRange*
        GpuDrivenPassDrawCommandStream::FindRange(
            GeometryBackendKind backend) {

        GpuDrivenDrawCommandRange& range =
            ranges[ToDrawCommandBackendSlot(backend)];
        return range.backend == backend && range.IsActive()
            ? &range
            : nullptr;
    }

    size_t GpuDrivenPassDrawCommandStream::CountActiveRanges() const {
        size_t count = 0;
        for (const GpuDrivenDrawCommandRange& range : ranges) {
            if (range.IsActive()) {
                ++count;
            }
        }
        return count;
    }

    void GpuDrivenDrawCommandStream::Reset() {
        for (size_t i = 0; i < passes.size(); ++i) {
            passes[i].Reset(PassFromIndex(i));
        }
    }

    GpuDrivenPassDrawCommandStream&
        GpuDrivenDrawCommandStream::GetPass(GpuDrivenPassKind pass) {

        return passes[ToPassIndex(pass)];
    }

    const GpuDrivenPassDrawCommandStream&
        GpuDrivenDrawCommandStream::GetPass(GpuDrivenPassKind pass) const {

        return passes[ToPassIndex(pass)];
    }

    GpuDrivenDrawCommandRange&
        GpuDrivenDrawCommandStream::SetRange(
            const GpuDrivenDrawCommandRange& range) {

        GpuDrivenDrawCommandRange& target =
            GetPass(range.pass).GetOrCreateRange(range.backend);
        target = range;
        return target;
    }

    const GpuDrivenDrawCommandRange*
        GpuDrivenDrawCommandStream::FindRange(
            GpuDrivenPassKind pass,
            GeometryBackendKind backend) const {

        return GetPass(pass).FindRange(backend);
    }

    size_t GpuDrivenDrawCommandStream::CountActivePasses() const {
        size_t count = 0;
        for (const GpuDrivenPassDrawCommandStream& pass : passes) {
            if (pass.CountActiveRanges() != 0) {
                ++count;
            }
        }
        return count;
    }

    size_t GpuDrivenDrawCommandStream::CountActiveRanges() const {
        size_t count = 0;
        for (const GpuDrivenPassDrawCommandStream& pass : passes) {
            count += pass.CountActiveRanges();
        }
        return count;
    }

    size_t GpuDrivenDrawCommandStream::CountGpuAuthoredCommands() const {
        size_t count = 0;
        for (const GpuDrivenPassDrawCommandStream& pass : passes) {
            for (const GpuDrivenDrawCommandRange& range : pass.ranges) {
                if (range.IsActive() && range.gpuAuthored) {
                    count += range.commandCount;
                }
            }
        }
        return count;
    }

    size_t GpuDrivenDrawCommandStream::CountTraditionalIndirectCommands() const {
        size_t count = 0;
        for (const GpuDrivenPassDrawCommandStream& pass : passes) {
            for (const GpuDrivenDrawCommandRange& range : pass.ranges) {
                if (range.IsActive() &&
                    range.backend == GeometryBackendKind::GpuDrivenTraditionalVsPs) {
                    count += range.commandCount;
                }
            }
        }
        return count;
    }

    size_t GpuDrivenDrawCommandStream::CountGpuCounterBackedRanges() const {
        size_t count = 0;
        for (const GpuDrivenPassDrawCommandStream& pass : passes) {
            for (const GpuDrivenDrawCommandRange& range : pass.ranges) {
                if (range.IsActive() && range.gpuCounterBacked) {
                    ++count;
                }
            }
        }
        return count;
    }

    size_t GpuDrivenDrawCommandStream::CountKnownGpuVisibleCommands() const {
        size_t count = 0;
        for (const GpuDrivenPassDrawCommandStream& pass : passes) {
            for (const GpuDrivenDrawCommandRange& range : pass.ranges) {
                if (range.IsActive() && range.visibleCommandCountKnown) {
                    count += range.visibleCommandCount;
                }
            }
        }
        return count;
    }

    size_t GpuDrivenDrawCommandStream::CountKnownGpuVisibleCommandOverflows() const {
        size_t count = 0;
        for (const GpuDrivenPassDrawCommandStream& pass : passes) {
            for (const GpuDrivenDrawCommandRange& range : pass.ranges) {
                if (range.IsActive() && range.visibleCommandCountKnown) {
                    count += range.visibleCommandOverflowCount;
                }
            }
        }
        return count;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
