#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"

#include <algorithm>
#include <cstring>

#include <d3dx12.h>
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"

#include "Render3D/Runtime/HIKARI_SurfaceGpuScene.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

namespace {
D3D12_SHADER_RESOURCE_VIEW_DESC BuildSurfaceGpuSceneSrvDesc(size_t elementCount) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = static_cast<UINT>(elementCount > 0 ? elementCount : 1);
    srvDesc.Buffer.StructureByteStride = sizeof(RUNTIME::SurfaceGpuSceneInstance);
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    return srvDesc;
}
} // namespace

void SurfaceGpuSceneFrameBuffer::Finalize(const GFX::Context& graphics) {
    for (auto& slot : slots_) {
        slot.uploads.Finalize(&graphics);
        GFX::RetireD3D12ObjectForFrame(slot.defaultBuffer, graphics, "SurfaceGpuScene.Frame");
        slot = {};
    }
    activeSlotIndex_ = 0;
    capacity_ = 0;
    stats_ = {};
}

bool SurfaceGpuSceneFrameBuffer::Initialize(ID3D12Device* device, D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
                                            D3D12_GPU_DESCRIPTOR_HANDLE srvGpu, UINT descriptorSize, size_t capacity) {
    if (device == nullptr || srvCpu.ptr == 0 || srvGpu.ptr == 0 || descriptorSize == 0 || capacity == 0) {
        return false;
    }

    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> srvCpuFrames{};
    std::array<D3D12_GPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount> srvGpuFrames{};
    for (uint32_t slotIndex = 0; slotIndex < GFX::kFrameResourceCount; ++slotIndex) {
        srvCpuFrames[slotIndex] = srvCpu;
        srvCpuFrames[slotIndex].ptr += static_cast<SIZE_T>(descriptorSize) * slotIndex;
        srvGpuFrames[slotIndex] = srvGpu;
        srvGpuFrames[slotIndex].ptr += static_cast<UINT64>(descriptorSize) * slotIndex;
    }
    return Initialize(device, srvCpuFrames, srvGpuFrames, capacity);
}

bool SurfaceGpuSceneFrameBuffer::Initialize(
    ID3D12Device* device, const std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount>& srvCpu,
    const std::array<D3D12_GPU_DESCRIPTOR_HANDLE, GFX::kFrameResourceCount>& srvGpu, size_t capacity) {
    if (device == nullptr || capacity == 0) {
        return false;
    }
    for (uint32_t slotIndex = 0; slotIndex < GFX::kFrameResourceCount; ++slotIndex) {
        if (srvCpu[slotIndex].ptr == 0 || srvGpu[slotIndex].ptr == 0) {
            return false;
        }
    }

    Finalize(GFX::GetCurrentContext());

    for (size_t attemptCapacity = capacity; attemptCapacity > 0;
         attemptCapacity = attemptCapacity > 1 ? attemptCapacity / 2 : 0) {
        const UINT64 bufferBytes =
            static_cast<UINT64>(sizeof(RUNTIME::SurfaceGpuSceneInstance)) * static_cast<UINT64>(attemptCapacity);
        const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferBytes);

        bool allSlotsReady = true;
        for (uint32_t slotIndex = 0; slotIndex < GFX::kFrameResourceCount; ++slotIndex) {
            FrameSlot& slot = slots_[slotIndex];
            slot = {};
            slot.srvCpu = srvCpu[slotIndex];
            slot.srvGpu = srvGpu[slotIndex];

            try {
                slot.staged.resize(attemptCapacity);
            } catch (...) {
                allSlotsReady = false;
                break;
            }
            if (!slot.uploads.Init(device, (std::min)(size_t(bufferBytes), size_t(65536)),
                                   GFX::UPLOAD_TRAFFIC::Owner::GpuScene)) {
                allSlotsReady = false;
                break;
            }

            auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
            if (FAILED(GFX::VIDEO_MEMORY::CreateCommittedResource(
                    device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
                    GFX::VIDEO_MEMORY::Owner::GpuDriven, IID_PPV_ARGS(slot.defaultBuffer.GetAddressOf())))) {
                allSlotsReady = false;
                break;
            }
            slot.defaultState = D3D12_RESOURCE_STATE_COMMON;
        }

        if (allSlotsReady) {
            capacity_ = attemptCapacity;
            break;
        }

        for (FrameSlot& slot : slots_) {
            slot = {};
        }
    }

    if (capacity_ == 0) {
        return false;
    }

    const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = BuildSurfaceGpuSceneSrvDesc(capacity_);
    for (FrameSlot& slot : slots_) {
        device->CreateShaderResourceView(slot.defaultBuffer.Get(), &srvDesc, slot.srvCpu);
    }

    BeginFrame(0);
    return true;
}

SurfaceGpuSceneFrameBuffer::FrameSlot* SurfaceGpuSceneFrameBuffer::ActiveSlot() {
    return &slots_[activeSlotIndex_ % GFX::kFrameResourceCount];
}

const SurfaceGpuSceneFrameBuffer::FrameSlot* SurfaceGpuSceneFrameBuffer::ActiveSlot() const {
    return &slots_[activeSlotIndex_ % GFX::kFrameResourceCount];
}

void SurfaceGpuSceneFrameBuffer::MarkDirtyRange(FrameSlot& slot, size_t firstInstance, size_t endInstance) {
    slot.dirtyRanges.Add(firstInstance, endInstance);
}

void SurfaceGpuSceneFrameBuffer::BeginFrame(uint32_t frameIndex) {
    activeSlotIndex_ = frameIndex % GFX::kFrameResourceCount;
    FrameSlot* slot = ActiveSlot();

    stats_ = {};
    stats_.capacity = capacity_;
    stats_.initialized = capacity_ != 0 && slot != nullptr && !slot->staged.empty() && slot->defaultBuffer != nullptr;
    stats_.srv = slot != nullptr ? slot->srvGpu : D3D12_GPU_DESCRIPTOR_HANDLE{};

    if (slot != nullptr) {
        slot->uploads.ResetForFrame(GFX::GetCurrentContext().currentFrameRetireFenceValue);
        slot->cursor = 0;
        slot->dirtyRanges.Clear();
    }
}

void SurfaceGpuSceneFrameBuffer::ResetSlotResidency(FrameSlot& slot) {
    slot.cursor = 0;
    slot.resident = false;
    slot.residentInstanceCount = 0;
    slot.layoutVersion = 0;
    slot.sourceVersion = 0;
    slot.sourceIdentity = 0;
    slot.materialBindingVersion = 0;
    slot.dirtyRanges.Clear();
}

void SurfaceGpuSceneFrameBuffer::ResetFrame() {
    FrameSlot* slot = ActiveSlot();
    if (slot != nullptr) {
        ResetSlotResidency(*slot);
    }

    const size_t capacity = stats_.capacity;
    const bool initialized = stats_.initialized;
    const D3D12_GPU_DESCRIPTOR_HANDLE srv = stats_.srv;
    stats_ = {};
    stats_.capacity = capacity;
    stats_.initialized = initialized;
    stats_.srv = srv;
}

void SurfaceGpuSceneFrameBuffer::InvalidateResidency() {
    for (FrameSlot& slot : slots_) {
        ResetSlotResidency(slot);
    }
    ResetFrame();
}

bool SurfaceGpuSceneFrameBuffer::CanReuseFrame(uintptr_t sourceIdentity, size_t residentInstanceCount,
                                               uint64_t layoutVersion, uint64_t sourceVersion) const {
    const FrameSlot* slot = ActiveSlot();
    return slot != nullptr && slot->resident && slot->sourceIdentity == sourceIdentity &&
           slot->residentInstanceCount == residentInstanceCount && slot->layoutVersion == layoutVersion &&
           slot->sourceVersion == sourceVersion && residentInstanceCount <= capacity_;
}

bool SurfaceGpuSceneFrameBuffer::CanPatchFrame(uintptr_t sourceIdentity, size_t residentInstanceCount,
                                               uint64_t layoutVersion, uint64_t baseSourceVersion) const {
    const FrameSlot* slot = ActiveSlot();
    return slot != nullptr && slot->resident && slot->sourceIdentity == sourceIdentity &&
           slot->residentInstanceCount == residentInstanceCount && slot->layoutVersion == layoutVersion &&
           slot->sourceVersion == baseSourceVersion && residentInstanceCount <= capacity_;
}

void SurfaceGpuSceneFrameBuffer::ReuseFrame(size_t residentInstanceCount) {
    FrameSlot* slot = ActiveSlot();
    const size_t residentCount =
        (std::min)(residentInstanceCount, (std::min)(slot != nullptr ? slot->residentInstanceCount : 0u, capacity_));
    if (slot != nullptr) {
        // BeginFrame はフレーム統計を初期化するため cursor を 0 に戻すが、
        // resident reuse では CPU staging 側にも同じ論理範囲が残っている。
        // 材質 binding version が変わった場合に全 resident instance を再 patch
        // できるよう、論理的な使用範囲を復元する。
        slot->cursor = residentCount;
    }

    const size_t capacity = stats_.capacity;
    const bool initialized = stats_.initialized;
    const D3D12_GPU_DESCRIPTOR_HANDLE srv = stats_.srv;
    stats_ = {};
    stats_.capacity = capacity;
    stats_.initialized = initialized;
    stats_.srv = srv;
    stats_.requestedInstanceCount = residentInstanceCount;
    stats_.uploadedInstanceCount = residentCount;
    if (residentCount < residentInstanceCount) {
        stats_.overflowInstanceCount = residentInstanceCount - residentCount;
    }
}

void SurfaceGpuSceneFrameBuffer::Upload(const RUNTIME::SurfaceGpuSceneInstance* instances, size_t count) {
    FrameSlot* slot = ActiveSlot();
    stats_.requestedInstanceCount += count;
    ++stats_.uploadCallCount;
    if (instances == nullptr || count == 0) {
        return;
    }
    if (slot == nullptr || slot->staged.empty() || capacity_ == 0) {
        stats_.overflowInstanceCount += count;
        return;
    }

    const size_t remainingCapacity = capacity_ > slot->cursor ? capacity_ - slot->cursor : 0;
    const size_t uploadCount = (std::min)(count, remainingCapacity);
    if (uploadCount > 0) {
        const size_t firstUploadedInstance = slot->cursor;
        std::memcpy(slot->staged.data() + slot->cursor, instances,
                    sizeof(RUNTIME::SurfaceGpuSceneInstance) * uploadCount);
        slot->cursor += uploadCount;
        MarkDirtyRange(*slot, firstUploadedInstance, slot->cursor);
        stats_.uploadedInstanceCount += uploadCount;
    }
    if (uploadCount < count) {
        stats_.overflowInstanceCount += count - uploadCount;
    }
}

void SurfaceGpuSceneFrameBuffer::Upload(const std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances) {
    Upload(instances.data(), instances.size());
}

bool SurfaceGpuSceneFrameBuffer::UpdateRange(size_t firstInstance, const RUNTIME::SurfaceGpuSceneInstance* instances,
                                             size_t count) {
    FrameSlot* slot = ActiveSlot();
    stats_.requestedInstanceCount += count;
    ++stats_.uploadCallCount;
    if (instances == nullptr || count == 0) {
        return true;
    }
    if (slot == nullptr || slot->staged.empty() || firstInstance >= capacity_ || count > capacity_ - firstInstance) {
        stats_.overflowInstanceCount += count;
        return false;
    }

    std::memcpy(slot->staged.data() + firstInstance, instances, sizeof(RUNTIME::SurfaceGpuSceneInstance) * count);
    stats_.uploadedInstanceCount = (std::max)(stats_.uploadedInstanceCount, firstInstance + count);
    slot->cursor = (std::max)(slot->cursor, firstInstance + count);
    MarkDirtyRange(*slot, firstInstance, firstInstance + count);
    return true;
}

bool SurfaceGpuSceneFrameBuffer::PatchMaterialDataIndex(size_t instanceIndex, uint32_t materialDataIndex) {
    FrameSlot* slot = ActiveSlot();
    if (slot == nullptr || slot->staged.empty() || instanceIndex >= stats_.uploadedInstanceCount ||
        instanceIndex >= capacity_) {
        return false;
    }

    ++stats_.materialPatchCount;
    if (slot->staged[instanceIndex].materialDataIndex == materialDataIndex) {
        ++stats_.materialPatchUnchangedCount;
        return true;
    }

    slot->staged[instanceIndex].materialDataIndex = materialDataIndex;
    MarkDirtyRange(*slot, instanceIndex, instanceIndex + 1);
    ++stats_.materialPatchChangedCount;
    return true;
}

void SurfaceGpuSceneFrameBuffer::ApplyMaterialBindings(std::span<const uint32_t> materialSlotBySourceRecord,
                                                       uint64_t bindingVersion) {
    FrameSlot* slot = ActiveSlot();
    if (slot == nullptr || slot->staged.empty()) {
        return;
    }

    const size_t instanceCount = (std::min)(stats_.uploadedInstanceCount, (std::min)(slot->cursor, capacity_));
    if (instanceCount == 0u) {
        slot->materialBindingVersion = bindingVersion;
        stats_.materialBindingVersion = bindingVersion;
        return;
    }

    // 更新区間の走査中に新しい材質パッチ区間を追加しても iterator を無効にしない。
    std::array<GFX::DirtyUploadRange, GFX::DirtyUploadRanges::kMaximumRanges> visits{};
    size_t visitCount = 0;
    if (slot->materialBindingVersion != bindingVersion) {
        visits[visitCount++] = {0, instanceCount};
    } else {
        for (const auto range : slot->dirtyRanges.Ranges())
            visits[visitCount++] = range;
    }
    const auto patch = [&](size_t instanceIndex) {
        RUNTIME::SurfaceGpuSceneInstance& instance = slot->staged[instanceIndex];
        uint32_t materialSlot = 0u;
        if (instance.sourceRecordIndex < materialSlotBySourceRecord.size()) {
            const uint32_t resolved = materialSlotBySourceRecord[instance.sourceRecordIndex];
            if (resolved != RUNTIME::kInvalidRenderSurfaceIndex) {
                materialSlot = resolved;
            } else {
                ++stats_.materialBindingMissingCount;
            }
        } else {
            ++stats_.materialBindingMissingCount;
        }

        ++stats_.materialBindingVisitCount;
        ++stats_.materialPatchCount;
        if (instance.materialDataIndex == materialSlot) {
            ++stats_.materialPatchUnchangedCount;
            return;
        }

        instance.materialDataIndex = materialSlot;
        ++stats_.materialPatchChangedCount;
        MarkDirtyRange(*slot, instanceIndex, instanceIndex + 1u);
    };
    for (size_t visit = 0; visit < visitCount; ++visit) {
        const auto range = visits[visit];
        for (size_t index = range.first; index < (std::min)(range.end, instanceCount); ++index)
            patch(index);
    }
    slot->materialBindingVersion = bindingVersion;
    stats_.materialBindingVersion = bindingVersion;
}

void SurfaceGpuSceneFrameBuffer::MarkResident(uintptr_t sourceIdentity, uint64_t layoutVersion, uint64_t sourceVersion,
                                              size_t instanceCount) {
    FrameSlot* slot = ActiveSlot();
    if (slot == nullptr || stats_.overflowInstanceCount != 0u) {
        return;
    }

    slot->resident = true;
    slot->sourceIdentity = sourceIdentity;
    slot->residentInstanceCount = instanceCount;
    slot->layoutVersion = layoutVersion;
    slot->sourceVersion = sourceVersion;
}

bool SurfaceGpuSceneFrameBuffer::CommitFrame(ID3D12GraphicsCommandList* commandList) {
    FrameSlot* slot = ActiveSlot();
    if (slot == nullptr || commandList == nullptr || slot->staged.empty() || slot->defaultBuffer == nullptr) {
        return false;
    }
    if (slot->dirtyRanges.Empty() || stats_.uploadedInstanceCount == 0u)
        return true;
    size_t copyBytes = 0;
    for (const auto range : slot->dirtyRanges.Ranges()) {
        const size_t end = (std::min)(range.end, stats_.uploadedInstanceCount);
        if (range.first < end)
            copyBytes += (end - range.first) * sizeof(RUNTIME::SurfaceGpuSceneInstance);
    }
    if (copyBytes == 0) {
        slot->dirtyRanges.Clear();
        return true;
    }
    const DX::UploadAllocation upload = slot->uploads.Allocate(copyBytes);
    if (!upload) {
        slot->resident = false;
        return false;
    }

    if (slot->defaultState != D3D12_RESOURCE_STATE_COPY_DEST) {
        const auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(slot->defaultBuffer.Get(), slot->defaultState,
                                                                     D3D12_RESOURCE_STATE_COPY_DEST);
        commandList->ResourceBarrier(1, &toCopyDest);
        slot->defaultState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    // 一つの不変 staging snapshot に疎な区間を詰める。GPU 在途の view は上書きしない。
    size_t packedOffset = 0;
    for (const auto range : slot->dirtyRanges.Ranges()) {
        const size_t end = (std::min)(range.end, stats_.uploadedInstanceCount);
        if (range.first >= end)
            continue;
        const size_t bytes = (end - range.first) * sizeof(RUNTIME::SurfaceGpuSceneInstance);
        std::memcpy(static_cast<std::byte*>(upload.cpuAddress) + packedOffset, slot->staged.data() + range.first,
                    bytes);
        commandList->CopyBufferRegion(slot->defaultBuffer.Get(), range.first * sizeof(RUNTIME::SurfaceGpuSceneInstance),
                                      upload.resource, upload.offset + packedOffset, bytes);
        packedOffset += bytes;
        stats_.committedInstanceCount += end - range.first;
        ++stats_.committedRangeCount;
    }
    stats_.committedBytes += packedOffset;

    const D3D12_RESOURCE_STATES shaderState =
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    const auto toShader =
        CD3DX12_RESOURCE_BARRIER::Transition(slot->defaultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, shaderState);
    commandList->ResourceBarrier(1, &toShader);
    slot->defaultState = shaderState;
    slot->dirtyRanges.Clear();
    return true;
}

D3D12_GPU_DESCRIPTOR_HANDLE SurfaceGpuSceneFrameBuffer::GetSrv() const { return stats_.srv; }

D3D12_GPU_VIRTUAL_ADDRESS SurfaceGpuSceneFrameBuffer::GetGpuVirtualAddress() const {
    const FrameSlot* slot = ActiveSlot();
    return slot != nullptr && slot->defaultBuffer != nullptr ? slot->defaultBuffer->GetGPUVirtualAddress() : 0;
}

const SurfaceGpuSceneFrameBufferStats& SurfaceGpuSceneFrameBuffer::GetStats() const { return stats_; }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
