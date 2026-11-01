#include "Render3D/GpuDriven/HIKARI_MassGpuSceneFrameBuffer.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include <d3dx12.h>

#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
bool MassGpuSceneFrameBuffer::Initialize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap) {
    if (device == nullptr || srvHeap == nullptr) {
        return false;
    }
    if (device_ == device && srvHeap_ == srvHeap) {
        stats_.initialized = true;
        return true;
    }
    if (device_ != nullptr || srvHeap_ != nullptr) {
        Shutdown();
    }
    device_ = device;
    srvHeap_ = srvHeap;
    descriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for (uint32_t frameIndex = 0u; frameIndex < GFX::kFrameResourceCount; ++frameIndex) {
        const UINT descriptorIndex =
            GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::MeshMassRenderProxyFrameSlot(frameIndex));
        slots_[frameIndex].srvCpu = GFX::DESCRIPTOR::CpuAt(srvHeap_, descriptorSize_, descriptorIndex);
        slots_[frameIndex].srvGpu = GFX::DESCRIPTOR::GpuAt(srvHeap_, descriptorSize_, descriptorIndex);
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
        nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        nullSrv.Format = DXGI_FORMAT_UNKNOWN;
        nullSrv.Buffer.FirstElement = 0u;
        nullSrv.Buffer.NumElements = 1u;
        nullSrv.Buffer.StructureByteStride = sizeof(RUNTIME::MassRenderProxyGpuRecord);
        device_->CreateShaderResourceView(nullptr, &nullSrv, slots_[frameIndex].srvCpu);
    }
    stats_.initialized = true;
    return true;
}

void MassGpuSceneFrameBuffer::Shutdown() {
    for (FrameSlot& slot : slots_) {
        RetireSlot(slot);
    }
    device_ = nullptr;
    srvHeap_ = nullptr;
    descriptorSize_ = 0u;
    activeSlotIndex_ = 0u;
    uploadRanges_.clear();
    stats_ = {};
}

void MassGpuSceneFrameBuffer::InvalidateResidency() noexcept {
    for (FrameSlot& slot : slots_) {
        slot.worldIdentity = 0u;
        slot.layoutVersion = 0u;
        slot.sourceVersion = 0u;
        slot.materialBindingVersion = 0u;
        slot.residentInstanceCount = 0u;
        slot.resident = false;
    }
}

size_t MassGpuSceneFrameBuffer::GrowCapacity(size_t requestedCapacity) noexcept {
    if (requestedCapacity == 0u || requestedCapacity > kMassGpuSceneMaximumCapacity) {
        return 0u;
    }
    size_t capacity = kMassGpuSceneMinimumCapacity;
    while (capacity < requestedCapacity) {
        capacity = (std::min)(capacity * 2u, kMassGpuSceneMaximumCapacity);
    }
    return capacity;
}

bool MassGpuSceneFrameBuffer::EnsureCapacity(size_t requestedCapacity) {
    FrameSlot* active = ActiveSlot();
    if (active == nullptr) {
        return false;
    }
    if (requestedCapacity <= active->capacity && active->capacity != 0u) {
        return true;
    }
    const size_t capacity = GrowCapacity(requestedCapacity);
    if (capacity == 0u || device_ == nullptr || srvHeap_ == nullptr) {
        return false;
    }
    FrameSlot replacement{};
    replacement.srvCpu = active->srvCpu;
    replacement.srvGpu = active->srvGpu;
    if (!CreateSlot(capacity, replacement)) {
        RetireSlot(replacement);
        return false;
    }
    RetireSlot(*active);
    *active = std::move(replacement);
    ++stats_.resourceGrowthCount;
    return true;
}

bool MassGpuSceneFrameBuffer::CreateSlot(size_t capacity, FrameSlot& slot) {
    if (capacity == 0u || capacity > (std::numeric_limits<UINT64>::max)() / sizeof(RUNTIME::MassRenderProxyGpuRecord)) {
        return false;
    }
    const UINT64 bytes = static_cast<UINT64>(capacity) * sizeof(RUNTIME::MassRenderProxyGpuRecord);
    const auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    const auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(bytes);

    if (!slot.uploads.Init(device_, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT)) {
        return false;
    }
    const HRESULT hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
        device_, &defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
        GFX::VIDEO_MEMORY::Owner::GpuDriven, IID_PPV_ARGS(slot.defaultBuffer.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "MassGpuScene::CreateDefault")) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv.Format = DXGI_FORMAT_UNKNOWN;
    srv.Buffer.FirstElement = 0u;
    srv.Buffer.NumElements = static_cast<UINT>(capacity);
    srv.Buffer.StructureByteStride = sizeof(RUNTIME::MassRenderProxyGpuRecord);
    srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    device_->CreateShaderResourceView(slot.defaultBuffer.Get(), &srv, slot.srvCpu);
    slot.defaultState = D3D12_RESOURCE_STATE_COMMON;
    slot.capacity = capacity;
    return true;
}

void MassGpuSceneFrameBuffer::RetireSlot(FrameSlot& slot) noexcept {
    slot.uploads.Finalize(&GFX::GetCurrentContext());
    GFX::RetireD3D12ObjectForCurrentFrame(slot.defaultBuffer, "MassGpuScene.Default");
    const D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = slot.srvCpu;
    const D3D12_GPU_DESCRIPTOR_HANDLE srvGpu = slot.srvGpu;
    slot = {};
    slot.srvCpu = srvCpu;
    slot.srvGpu = srvGpu;
}

void MassGpuSceneFrameBuffer::ResetStatsForFrame() {
    const uint32_t growthCount = stats_.resourceGrowthCount;
    const bool initialized = device_ != nullptr && srvHeap_ != nullptr;
    stats_ = {};
    stats_.capacity = ActiveSlot() != nullptr ? ActiveSlot()->capacity : 0u;
    stats_.resourceGrowthCount = growthCount;
    stats_.initialized = initialized;
    if (const FrameSlot* slot = ActiveSlot()) {
        stats_.srv = slot->srvGpu;
        stats_.gpuAddress = slot->defaultBuffer != nullptr ? slot->defaultBuffer->GetGPUVirtualAddress() : 0u;
    }
}

bool MassGpuSceneFrameBuffer::UploadFrame(uint32_t frameIndex, ID3D12GraphicsCommandList* commandList,
                                          const MassGpuSceneSource* source, std::span<const uint32_t> materialBindings,
                                          uint64_t materialBindingVersion) {
    activeSlotIndex_ = frameIndex % GFX::kFrameResourceCount;
    ResetStatsForFrame();
    stats_.publicationFrameIndex = source != nullptr ? source->publicationFrameIndex : 0u;
    stats_.publicationGeneration = source != nullptr ? source->publicationGeneration : 0u;
    uploadRanges_.clear();
    if (source == nullptr) {
        if (FrameSlot* slot = ActiveSlot()) {
            slot->resident = false;
            slot->residentInstanceCount = 0u;
        }
        return true;
    }
    stats_.worldIdentity = source->worldIdentity;
    stats_.layoutVersion = source->layoutVersion;
    stats_.sourceVersion = source->sourceVersion;
    if (source->worldIdentity == 0u || source->publicationGeneration == 0u) {
        InvalidateResidency();
        return true;
    }
    if (!source->HasInstances()) {
        if (FrameSlot* slot = ActiveSlot()) {
            slot->worldIdentity = source->worldIdentity;
            slot->layoutVersion = source->layoutVersion;
            slot->sourceVersion = source->sourceVersion;
            slot->materialBindingVersion = materialBindingVersion;
            slot->resident = false;
            slot->residentInstanceCount = 0u;
        }
        stats_.sceneResident = true;
        return true;
    }
    stats_.requestedInstanceCount = source->instances.size();
    if (!EnsureCapacity(source->instances.size())) {
        stats_.overflowInstanceCount = source->instances.size() > kMassGpuSceneMaximumCapacity
                                           ? source->instances.size() - kMassGpuSceneMaximumCapacity
                                           : source->instances.size();
        return false;
    }
    ResetStatsForFrame();
    stats_.publicationFrameIndex = source->publicationFrameIndex;
    stats_.publicationGeneration = source->publicationGeneration;
    stats_.worldIdentity = source->worldIdentity;
    stats_.layoutVersion = source->layoutVersion;
    stats_.sourceVersion = source->sourceVersion;
    stats_.requestedInstanceCount = source->instances.size();
    FrameSlot* slot = ActiveSlot();
    if (slot == nullptr || !slot->uploads.IsInitialized() || slot->defaultBuffer == nullptr || commandList == nullptr) {
        return false;
    }
    slot->uploads.ResetForFrame(GFX::GetCurrentContext().currentFrameRetireFenceValue);

    const bool sameLayout = slot->resident && slot->worldIdentity == source->worldIdentity &&
                            slot->layoutVersion == source->layoutVersion &&
                            slot->residentInstanceCount == source->instances.size();
    const bool bindingChanged = slot->materialBindingVersion != materialBindingVersion;
    if (sameLayout && slot->sourceVersion == source->sourceVersion && !bindingChanged) {
        stats_.reusedResidentFrame = true;
        stats_.residentInstanceCount = source->instances.size();
        stats_.sceneResident = true;
        return true;
    }

    bool patchable = sameLayout && !bindingChanged;
    if (patchable) {
        patchable = source->CollectDirtyRangesSince(slot->sourceVersion, uploadRanges_);
    }
    if (!patchable) {
        uploadRanges_.clear();
        if (!source->instances.empty()) {
            uploadRanges_.push_back({0u, static_cast<uint32_t>(source->instances.size())});
        }
        stats_.uploadedFullScene = true;
    } else {
        stats_.patchedDirtyRanges = !uploadRanges_.empty();
    }

    size_t uploadInstanceCount = 0u;
    for (const RUNTIME::MassRenderDirtyRange& range : uploadRanges_) {
        if (range.instanceCount == 0u || range.firstInstance >= source->instances.size() ||
            range.instanceCount > source->instances.size() - range.firstInstance) {
            return false;
        }
        uploadInstanceCount += range.instanceCount;
    }
    DX::UploadAllocation upload{};
    if (uploadInstanceCount != 0u) {
        upload = slot->uploads.Allocate(uploadInstanceCount * sizeof(RUNTIME::MassRenderProxyGpuRecord));
        if (!upload) {
            slot->resident = false;
            return false;
        }
    }
    size_t packedInstanceOffset = 0u;
    for (const RUNTIME::MassRenderDirtyRange& range : uploadRanges_) {
        auto* records = static_cast<RUNTIME::MassRenderProxyGpuRecord*>(upload.cpuAddress) + packedInstanceOffset;
        std::memcpy(records, source->instances.data() + range.firstInstance,
                    static_cast<size_t>(range.instanceCount) * sizeof(RUNTIME::MassRenderProxyGpuRecord));
        for (uint32_t offset = 0u; offset < range.instanceCount; ++offset) {
            RUNTIME::MassRenderProxyGpuRecord& record = records[offset];
            ++stats_.materialBindingVisitCount;
            if (record.sourceRecordIndex >= materialBindings.size() ||
                materialBindings[record.sourceRecordIndex] == RUNTIME::kInvalidRenderSurfaceIndex) {
                // slot 0 は GpuMaterialRegistry が所有する neutral material。
                // 欠落は診断へ残しつつ、geometry residency は維持する。
                record.materialDataIndex = 0u;
                ++stats_.materialBindingMissingCount;
            } else {
                record.materialDataIndex = materialBindings[record.sourceRecordIndex];
            }
        }
        packedInstanceOffset += range.instanceCount;
    }

    if (!uploadRanges_.empty()) {
        if (slot->defaultState != D3D12_RESOURCE_STATE_COPY_DEST) {
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(slot->defaultBuffer.Get(), slot->defaultState,
                                                                      D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1u, &barrier);
            slot->defaultState = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        size_t packedByteOffset = 0u;
        for (const RUNTIME::MassRenderDirtyRange& range : uploadRanges_) {
            const UINT64 offsetBytes =
                static_cast<UINT64>(range.firstInstance) * sizeof(RUNTIME::MassRenderProxyGpuRecord);
            const UINT64 copyBytes =
                static_cast<UINT64>(range.instanceCount) * sizeof(RUNTIME::MassRenderProxyGpuRecord);
            commandList->CopyBufferRegion(slot->defaultBuffer.Get(), offsetBytes, upload.resource,
                                          upload.offset + packedByteOffset, copyBytes);
            packedByteOffset += static_cast<size_t>(copyBytes);
            stats_.uploadedInstanceCount += range.instanceCount;
            stats_.uploadedBytes += static_cast<size_t>(copyBytes);
            ++stats_.uploadRangeCount;
        }
        const D3D12_RESOURCE_STATES shaderState =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(slot->defaultBuffer.Get(),
                                                                  D3D12_RESOURCE_STATE_COPY_DEST, shaderState);
        commandList->ResourceBarrier(1u, &barrier);
        slot->defaultState = shaderState;
    }

    slot->worldIdentity = source->worldIdentity;
    slot->layoutVersion = source->layoutVersion;
    slot->sourceVersion = source->sourceVersion;
    slot->materialBindingVersion = materialBindingVersion;
    slot->residentInstanceCount = source->instances.size();
    slot->resident = true;
    stats_.residentInstanceCount = source->instances.size();
    stats_.sceneResident = true;
    stats_.srv = slot->srvGpu;
    stats_.gpuAddress = slot->defaultBuffer->GetGPUVirtualAddress();
    return true;
}

MassGpuSceneFrameBuffer::FrameSlot* MassGpuSceneFrameBuffer::ActiveSlot() noexcept {
    return activeSlotIndex_ < slots_.size() ? &slots_[activeSlotIndex_] : nullptr;
}

const MassGpuSceneFrameBuffer::FrameSlot* MassGpuSceneFrameBuffer::ActiveSlot() const noexcept {
    return activeSlotIndex_ < slots_.size() ? &slots_[activeSlotIndex_] : nullptr;
}

D3D12_GPU_VIRTUAL_ADDRESS
MassGpuSceneFrameBuffer::GetGpuVirtualAddress() const noexcept {
    const FrameSlot* slot = ActiveSlot();
    return slot != nullptr && slot->defaultBuffer != nullptr ? slot->defaultBuffer->GetGPUVirtualAddress() : 0u;
}

D3D12_GPU_DESCRIPTOR_HANDLE
MassGpuSceneFrameBuffer::GetSrv() const noexcept {
    const FrameSlot* slot = ActiveSlot();
    return slot != nullptr ? slot->srvGpu : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

const MassGpuSceneFrameBufferStats& MassGpuSceneFrameBuffer::GetStats() const noexcept { return stats_; }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
