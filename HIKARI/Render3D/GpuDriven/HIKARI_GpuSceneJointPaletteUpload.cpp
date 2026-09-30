#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

#include "Gfx/HIKARI_DynamicUploadBuffer.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
namespace {

constexpr size_t kDefaultJointPaletteUploadPageSize = 64u * 1024u;

struct CachedSnapshotUpload {
    GpuSceneJointPaletteSnapshotPtr snapshot{};
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0u;
};

struct FrameUploadSlot {
    DX::DynamicUploadBuffer uploads{};
    std::vector<CachedSnapshotUpload> cachedSnapshots{};
    GpuSceneJointPaletteUploadStats stats{};
    GFX::Context lastContext{};
    uint64_t frameSerial = 0u;
    uint64_t retireFenceValue = 0u;
    bool active = false;
};

struct UploadOwner {
    std::array<FrameUploadSlot, GFX::kFrameResourceCount> frames{};
    ID3D12Device* ownerDevice = nullptr;
    uint64_t ownerDeviceGeneration = 0u;
};

UploadOwner gOwner{};

GpuSceneJointPaletteSnapshotPtr EmptySnapshot() {
    static const GpuSceneJointPaletteSnapshotPtr snapshot = std::make_shared<const GpuSceneJointPaletteSnapshot>();
    return snapshot;
}

void FinalizeOwner(const GFX::Context* graphics) {
    const bool explicitContextMatchesOwner = graphics != nullptr && graphics->device == gOwner.ownerDevice &&
                                             graphics->deviceGeneration == gOwner.ownerDeviceGeneration;
    for (FrameUploadSlot& frame : gOwner.frames) {
        const GFX::Context* retireContext = nullptr;
        if (explicitContextMatchesOwner) {
            retireContext = graphics;
        } else if (frame.lastContext.device == gOwner.ownerDevice &&
                   frame.lastContext.deviceGeneration == gOwner.ownerDeviceGeneration) {
            retireContext = &frame.lastContext;
        }
        frame.uploads.Finalize(retireContext);
        frame.cachedSnapshots.clear();
        frame.stats = {};
        frame.lastContext = {};
        frame.frameSerial = 0u;
        frame.retireFenceValue = 0u;
        frame.active = false;
    }
    gOwner.ownerDevice = nullptr;
    gOwner.ownerDeviceGeneration = 0u;
}

bool ActivateFrameSlot(FrameUploadSlot& frame, const GFX::Context& graphics) {
    const uint64_t frameSerial = graphics.currentFrameRetireFenceValue;
    if (graphics.device == nullptr || graphics.frameFence == nullptr || frameSerial == 0u ||
        graphics.frameIndex >= GFX::kFrameResourceCount) {
        return false;
    }

    if (!frame.active || frame.frameSerial != frameSerial) {
        if (frame.active) {
            const uint64_t completedFence = graphics.frameFence->GetCompletedValue();
            if (completedFence == (std::numeric_limits<uint64_t>::max)() || completedFence < frame.retireFenceValue) {
                return false;
            }
            frame.cachedSnapshots.clear();
            frame.stats = {};
        }
        if (!frame.uploads.IsInitialized() &&
            !frame.uploads.Init(graphics.device, kDefaultJointPaletteUploadPageSize)) {
            return false;
        }
        frame.uploads.ResetForFrame(frameSerial);
        frame.frameSerial = frameSerial;
        frame.retireFenceValue = frameSerial;
        frame.active = true;
    }
    frame.lastContext = graphics;
    return true;
}

} // namespace

D3D12_GPU_VIRTUAL_ADDRESS UploadGpuSceneJointPaletteSnapshot(const GFX::Context& graphics,
                                                             const GpuSceneJointPaletteSnapshotPtr& sourceSnapshot) {
    if (graphics.device == nullptr) {
        return 0u;
    }
    if (gOwner.ownerDevice != graphics.device || gOwner.ownerDeviceGeneration != graphics.deviceGeneration) {
        FinalizeOwner(nullptr);
        gOwner.ownerDevice = graphics.device;
        gOwner.ownerDeviceGeneration = graphics.deviceGeneration;
    }

    const GpuSceneJointPaletteSnapshotPtr snapshot = sourceSnapshot != nullptr ? sourceSnapshot : EmptySnapshot();
    if (snapshot->matrices.size() > kMaxGpuSceneJointPaletteMatrixCount) {
        return 0u;
    }

    FrameUploadSlot& frame = gOwner.frames[graphics.frameIndex % GFX::kFrameResourceCount];
    if (!ActivateFrameSlot(frame, graphics)) {
        return 0u;
    }
    const auto cached =
        std::find_if(frame.cachedSnapshots.begin(), frame.cachedSnapshots.end(),
                     [&](const CachedSnapshotUpload& entry) { return entry.snapshot.get() == snapshot.get(); });
    if (cached != frame.cachedSnapshots.end()) {
        ++frame.stats.snapshotReuseCount;
        return cached->gpuAddress;
    }

    const size_t matrixCount = (std::max)(size_t(1u), snapshot->matrices.size());
    const DX::UploadAllocation allocation = frame.uploads.Allocate(matrixCount * sizeof(MATH::Mat4));
    if (!allocation) {
        return 0u;
    }
    auto* destination = static_cast<MATH::Mat4*>(allocation.cpuAddress);
    if (snapshot->matrices.empty()) {
        *destination = MATH::Mat4::Identity();
        ++frame.stats.emptyBindingUploadCount;
    } else {
        std::copy(snapshot->matrices.begin(), snapshot->matrices.end(), destination);
        frame.stats.matrixPayloadBytes += static_cast<uint64_t>(snapshot->matrices.size() * sizeof(MATH::Mat4));
    }
    try {
        frame.cachedSnapshots.push_back({snapshot, allocation.gpuAddress});
    } catch (...) {
        return 0u;
    }
    ++frame.stats.snapshotUploadCount;
    return allocation.gpuAddress;
}

GpuSceneJointPaletteUploadStats GetGpuSceneJointPaletteUploadStats(const GFX::Context& graphics) {
    if (graphics.frameIndex >= GFX::kFrameResourceCount) {
        return {};
    }
    const FrameUploadSlot& frame = gOwner.frames[graphics.frameIndex];
    return frame.active && frame.frameSerial == graphics.currentFrameRetireFenceValue
               ? frame.stats
               : GpuSceneJointPaletteUploadStats{};
}

void FinalizeGpuSceneJointPaletteUploads(const GFX::Context& graphics) { FinalizeOwner(&graphics); }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
