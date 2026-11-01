#include "Render3D/GpuDriven/HIKARI_GpuDrivenDrawCommandStream.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include <algorithm>
#include <limits>

namespace HIKARI::RENDER3D::GPUDRIVEN {

std::optional<GpuDrivenIndirectBucketStream> GpuDrivenDrawCommandRange::TraditionalStream(bool skinned) const {
    if (!gpuAuthored || !gpuCounterBacked || !counterBuffer || !commandCount || !commandBucketCapacity ||
        !commandBucketCount || !counterBucketStride || !traditionalIndirect || !traditionalIndirect->bucketVariants ||
        traditionalIndirect->bucketVariants->empty())
        return std::nullopt;
    GpuDrivenIndirectBucketStream stream{
        skinned ? skinnedArgumentBuffer : argumentBuffer,
        counterBuffer,
        skinned ? skinnedCommandSignature : commandSignature,
        skinned ? skinnedArgumentBufferOffset : argumentBufferOffset,
        skinned ? skinnedCounterBufferOffset : counterBufferOffset,
        skinned ? skinnedArgumentBucketStride : argumentBucketStride,
        counterBucketStride,
        static_cast<UINT>((std::min)({skinned ? skinnedCommandCount : staticCommandCount, commandBucketCapacity,
                                      static_cast<size_t>((std::numeric_limits<UINT>::max)())})),
        (std::min)(commandBucketCount, traditionalIndirect->bucketVariants->size())};
    if (!stream.arguments || !stream.signature || !stream.argumentStride || !stream.commandLimit)
        return std::nullopt;
    return stream;
}

void GpuDrivenIndirectBucketStream::Execute(ID3D12GraphicsCommandList* cmd, size_t bucket) const {
    if (!cmd || bucket >= bucketCount)
        return;
    cmd->ExecuteIndirect(signature, commandLimit, arguments,
                         argumentOffset + static_cast<UINT64>(bucket) * argumentStride, counters,
                         counterOffset + static_cast<UINT64>(bucket) * counterStride);
}

namespace {
GpuDrivenPassKind PassFromIndex(size_t index) {
    return static_cast<GpuDrivenPassKind>(index < kGpuDrivenPassCount ? index : kGpuDrivenPassCount - 1u);
}
} // namespace

size_t ToDrawCommandBackendSlot(GeometryBackendKind backend) {
    const size_t slot = static_cast<size_t>(backend);
    return slot < kGpuDrivenDrawCommandBackendSlotCount ? slot : 0u;
}

void GpuDrivenDrawCommandRange::Reset() {
    const GpuDrivenPassKind oldPass = pass;
    *this = {};
    pass = oldPass;
    sourcePass = oldPass;
}

bool GpuDrivenDrawCommandRange::IsActive() const {
    return consumable && producer != GpuDrivenCommandProducerKind::None && commandCount != 0;
}

bool GpuDrivenDrawCommandRange::HasGpuCommandLayout() const {
    return gpuCommandLayout != nullptr && argumentBuffer != nullptr && commandSignature != nullptr && commandCount != 0;
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

GpuDrivenDrawCommandRange& GpuDrivenPassDrawCommandStream::GetOrCreateRange(GeometryBackendKind backend) {
    GpuDrivenDrawCommandRange& range = ranges[ToDrawCommandBackendSlot(backend)];
    range.backend = backend;
    return range;
}

const GpuDrivenDrawCommandRange* GpuDrivenPassDrawCommandStream::FindRange(GeometryBackendKind backend) const {
    const GpuDrivenDrawCommandRange& range = ranges[ToDrawCommandBackendSlot(backend)];
    return range.backend == backend && range.IsActive() ? &range : nullptr;
}

GpuDrivenDrawCommandRange* GpuDrivenPassDrawCommandStream::FindRange(GeometryBackendKind backend) {
    GpuDrivenDrawCommandRange& range = ranges[ToDrawCommandBackendSlot(backend)];
    return range.backend == backend && range.IsActive() ? &range : nullptr;
}

void GpuDrivenDrawCommandStream::Reset() {
    for (size_t i = 0; i < passes.size(); ++i) {
        passes[i].Reset(PassFromIndex(i));
    }
}

GpuDrivenPassDrawCommandStream& GpuDrivenDrawCommandStream::GetPass(GpuDrivenPassKind pass) {
    return passes[ToPassIndex(pass)];
}

const GpuDrivenPassDrawCommandStream& GpuDrivenDrawCommandStream::GetPass(GpuDrivenPassKind pass) const {
    return passes[ToPassIndex(pass)];
}

GpuDrivenDrawCommandRange& GpuDrivenDrawCommandStream::SetRange(const GpuDrivenDrawCommandRange& range) {
    GpuDrivenDrawCommandRange& target = GetPass(range.pass).GetOrCreateRange(range.backend);
    target = range;
    return target;
}

const GpuDrivenDrawCommandRange* GpuDrivenDrawCommandStream::FindRange(GpuDrivenPassKind pass,
                                                                       GeometryBackendKind backend) const {
    return GetPass(pass).FindRange(backend);
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
            if (range.IsActive() && range.backend == GeometryBackendKind::GpuDrivenTraditionalVsPs) {
                count += range.commandCount;
            }
        }
    }
    return count;
}

} // namespace HIKARI::RENDER3D::GPUDRIVEN
