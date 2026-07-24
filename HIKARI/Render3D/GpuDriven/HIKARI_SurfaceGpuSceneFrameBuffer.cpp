#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"

#include <algorithm>
#include <cstring>

#include <d3dx12.h>

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
    }

    bool SurfaceGpuSceneFrameBuffer::Initialize(
        ID3D12Device* device,
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu,
        D3D12_GPU_DESCRIPTOR_HANDLE srvGpu,
        UINT descriptorSize,
        size_t capacity) {

        if (device == nullptr ||
            srvCpu.ptr == 0 ||
            srvGpu.ptr == 0 ||
            descriptorSize == 0 ||
            capacity == 0) {
            return false;
        }

        std::array<
            D3D12_CPU_DESCRIPTOR_HANDLE,
            GFX::kFrameResourceCount> srvCpuFrames{};
        std::array<
            D3D12_GPU_DESCRIPTOR_HANDLE,
            GFX::kFrameResourceCount> srvGpuFrames{};
        for (uint32_t slotIndex = 0;
            slotIndex < GFX::kFrameResourceCount;
            ++slotIndex) {

            srvCpuFrames[slotIndex] = srvCpu;
            srvCpuFrames[slotIndex].ptr +=
                static_cast<SIZE_T>(descriptorSize) * slotIndex;
            srvGpuFrames[slotIndex] = srvGpu;
            srvGpuFrames[slotIndex].ptr +=
                static_cast<UINT64>(descriptorSize) * slotIndex;
        }
        return Initialize(device, srvCpuFrames, srvGpuFrames, capacity);
    }

    bool SurfaceGpuSceneFrameBuffer::Initialize(
        ID3D12Device* device,
        const std::array<
            D3D12_CPU_DESCRIPTOR_HANDLE,
            GFX::kFrameResourceCount>& srvCpu,
        const std::array<
            D3D12_GPU_DESCRIPTOR_HANDLE,
            GFX::kFrameResourceCount>& srvGpu,
        size_t capacity) {

        if (device == nullptr || capacity == 0) {
            return false;
        }
        for (uint32_t slotIndex = 0;
            slotIndex < GFX::kFrameResourceCount;
            ++slotIndex) {

            if (srvCpu[slotIndex].ptr == 0 || srvGpu[slotIndex].ptr == 0) {
                return false;
            }
        }

        for (FrameSlot& slot : slots_) {
            slot = {};
        }
        activeSlotIndex_ = 0;
        capacity_ = 0;
        stats_ = {};

        for (size_t attemptCapacity = capacity;
            attemptCapacity > 0;
            attemptCapacity = attemptCapacity > 1 ? attemptCapacity / 2 : 0) {

            const UINT64 bufferBytes =
                static_cast<UINT64>(sizeof(RUNTIME::SurfaceGpuSceneInstance)) *
                static_cast<UINT64>(attemptCapacity);
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bufferBytes);

            bool allSlotsReady = true;
            for (uint32_t slotIndex = 0; slotIndex < GFX::kFrameResourceCount; ++slotIndex) {
                FrameSlot& slot = slots_[slotIndex];
                slot = {};
                slot.srvCpu = srvCpu[slotIndex];
                slot.srvGpu = srvGpu[slotIndex];

                auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
                if (FAILED(device->CreateCommittedResource(
                    &uploadHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(slot.uploadBuffer.GetAddressOf())))) {
                    allSlotsReady = false;
                    break;
                }

                if (FAILED(slot.uploadBuffer->Map(
                    0,
                    nullptr,
                    reinterpret_cast<void**>(&slot.mapped)))) {
                    allSlotsReady = false;
                    break;
                }

                auto defaultHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
                if (FAILED(device->CreateCommittedResource(
                    &defaultHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    D3D12_RESOURCE_STATE_COMMON,
                    nullptr,
                    IID_PPV_ARGS(slot.defaultBuffer.GetAddressOf())))) {
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

        const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc =
            BuildSurfaceGpuSceneSrvDesc(capacity_);
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

    void SurfaceGpuSceneFrameBuffer::MarkDirtyRange(
        FrameSlot& slot,
        size_t firstInstance,
        size_t endInstance) {

        if (firstInstance >= endInstance) {
            return;
        }

        if (!slot.dirty) {
            slot.dirtyFirstInstance = firstInstance;
            slot.dirtyEndInstance = endInstance;
        } else {
            slot.dirtyFirstInstance = (std::min)(slot.dirtyFirstInstance, firstInstance);
            slot.dirtyEndInstance = (std::max)(slot.dirtyEndInstance, endInstance);
        }
        slot.dirty = true;
    }

    void SurfaceGpuSceneFrameBuffer::BeginFrame(uint32_t frameIndex) {
        activeSlotIndex_ = frameIndex % GFX::kFrameResourceCount;
        FrameSlot* slot = ActiveSlot();

        stats_ = {};
        stats_.capacity = capacity_;
        stats_.initialized =
            capacity_ != 0 &&
            slot != nullptr &&
            slot->mapped != nullptr &&
            slot->defaultBuffer != nullptr;
        stats_.srv = slot != nullptr ? slot->srvGpu : D3D12_GPU_DESCRIPTOR_HANDLE{};

        if (slot != nullptr) {
            slot->cursor = 0;
            slot->dirty = false;
            slot->dirtyFirstInstance = 0;
            slot->dirtyEndInstance = 0;
        }
    }

    void SurfaceGpuSceneFrameBuffer::ResetFrame() {
        FrameSlot* slot = ActiveSlot();
        if (slot != nullptr) {
            slot->cursor = 0;
            slot->resident = false;
            slot->residentInstanceCount = 0;
            slot->layoutVersion = 0;
            slot->sourceVersion = 0;
            slot->materialBindingVersion = 0;
            slot->dirty = false;
            slot->dirtyFirstInstance = 0;
            slot->dirtyEndInstance = 0;
        }

        const size_t capacity = stats_.capacity;
        const bool initialized = stats_.initialized;
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = stats_.srv;
        stats_ = {};
        stats_.capacity = capacity;
        stats_.initialized = initialized;
        stats_.srv = srv;
    }

    bool SurfaceGpuSceneFrameBuffer::CanReuseFrame(
        size_t residentInstanceCount,
        uint64_t layoutVersion,
        uint64_t sourceVersion) const {

        const FrameSlot* slot = ActiveSlot();
        return
            slot != nullptr &&
            slot->resident &&
            slot->residentInstanceCount == residentInstanceCount &&
            slot->layoutVersion == layoutVersion &&
            slot->sourceVersion == sourceVersion &&
            residentInstanceCount <= capacity_;
    }

    bool SurfaceGpuSceneFrameBuffer::CanPatchFrame(
        size_t residentInstanceCount,
        uint64_t layoutVersion,
        uint64_t baseSourceVersion) const {

        const FrameSlot* slot = ActiveSlot();
        return
            slot != nullptr &&
            slot->resident &&
            slot->residentInstanceCount == residentInstanceCount &&
            slot->layoutVersion == layoutVersion &&
            slot->sourceVersion == baseSourceVersion &&
            residentInstanceCount <= capacity_;
    }

    void SurfaceGpuSceneFrameBuffer::ReuseFrame(size_t residentInstanceCount) {
        const FrameSlot* slot = ActiveSlot();
        const size_t residentCount =
            (std::min)(
                residentInstanceCount,
                (std::min)(
                    slot != nullptr ? slot->residentInstanceCount : 0u,
                    capacity_));

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

    void SurfaceGpuSceneFrameBuffer::Upload(
        const RUNTIME::SurfaceGpuSceneInstance* instances,
        size_t count) {

        FrameSlot* slot = ActiveSlot();
        stats_.requestedInstanceCount += count;
        ++stats_.uploadCallCount;
        if (instances == nullptr || count == 0) {
            return;
        }
        if (slot == nullptr || slot->mapped == nullptr || capacity_ == 0) {
            stats_.overflowInstanceCount += count;
            return;
        }

        const size_t remainingCapacity =
            capacity_ > slot->cursor ? capacity_ - slot->cursor : 0;
        const size_t uploadCount = (std::min)(count, remainingCapacity);
        if (uploadCount > 0) {
            const size_t firstUploadedInstance = slot->cursor;
            std::memcpy(
                slot->mapped + slot->cursor,
                instances,
                sizeof(RUNTIME::SurfaceGpuSceneInstance) * uploadCount);
            slot->cursor += uploadCount;
            MarkDirtyRange(*slot, firstUploadedInstance, slot->cursor);
            stats_.uploadedInstanceCount += uploadCount;
        }
        if (uploadCount < count) {
            stats_.overflowInstanceCount += count - uploadCount;
        }
    }

    void SurfaceGpuSceneFrameBuffer::Upload(
        const std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances) {

        Upload(instances.data(), instances.size());
    }

    bool SurfaceGpuSceneFrameBuffer::UpdateRange(
        size_t firstInstance,
        const RUNTIME::SurfaceGpuSceneInstance* instances,
        size_t count) {

        FrameSlot* slot = ActiveSlot();
        stats_.requestedInstanceCount += count;
        ++stats_.uploadCallCount;
        if (instances == nullptr || count == 0) {
            return true;
        }
        if (slot == nullptr ||
            slot->mapped == nullptr ||
            firstInstance >= capacity_ ||
            count > capacity_ - firstInstance) {
            stats_.overflowInstanceCount += count;
            return false;
        }

        std::memcpy(
            slot->mapped + firstInstance,
            instances,
            sizeof(RUNTIME::SurfaceGpuSceneInstance) * count);
        stats_.uploadedInstanceCount =
            (std::max)(stats_.uploadedInstanceCount, firstInstance + count);
        slot->cursor = (std::max)(slot->cursor, firstInstance + count);
        MarkDirtyRange(*slot, firstInstance, firstInstance + count);
        return true;
    }

    bool SurfaceGpuSceneFrameBuffer::PatchMaterialDataIndex(
        size_t instanceIndex,
        uint32_t materialDataIndex) {

        FrameSlot* slot = ActiveSlot();
        if (slot == nullptr ||
            slot->mapped == nullptr ||
            instanceIndex >= stats_.uploadedInstanceCount ||
            instanceIndex >= capacity_) {
            return false;
        }

        ++stats_.materialPatchCount;
        if (slot->mapped[instanceIndex].materialDataIndex == materialDataIndex) {
            ++stats_.materialPatchUnchangedCount;
            return true;
        }

        slot->mapped[instanceIndex].materialDataIndex = materialDataIndex;
        MarkDirtyRange(*slot, instanceIndex, instanceIndex + 1);
        ++stats_.materialPatchChangedCount;
        return true;
    }

    bool SurfaceGpuSceneFrameBuffer::PatchMaterialDataIndexChecked(
        size_t instanceIndex,
        uint32_t materialDataIndex,
        uint32_t expectedSourceRecordIndex,
        uint32_t expectedSourceSurfaceInstanceIndex) {

        FrameSlot* slot = ActiveSlot();
        if (slot == nullptr ||
            slot->mapped == nullptr ||
            instanceIndex >= stats_.uploadedInstanceCount ||
            instanceIndex >= capacity_) {
            return false;
        }

        const RUNTIME::SurfaceGpuSceneInstance& instance = slot->mapped[instanceIndex];
        if (expectedSourceRecordIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
            instance.sourceRecordIndex != expectedSourceRecordIndex) {
            return false;
        }
        if (expectedSourceSurfaceInstanceIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
            instance.sourceSurfaceInstanceIndex != expectedSourceSurfaceInstanceIndex) {
            return false;
        }

        return PatchMaterialDataIndex(instanceIndex, materialDataIndex);
    }

    void SurfaceGpuSceneFrameBuffer::ApplyMaterialBindings(
        std::span<const uint32_t> materialSlotBySourceRecord,
        uint64_t bindingVersion) {

        FrameSlot* slot = ActiveSlot();
        if (slot == nullptr || slot->mapped == nullptr) {
            return;
        }

        const size_t instanceCount =
            (std::min)(
                stats_.uploadedInstanceCount,
                (std::min)(slot->cursor, capacity_));
        if (instanceCount == 0u) {
            slot->materialBindingVersion = bindingVersion;
            stats_.materialBindingVersion = bindingVersion;
            return;
        }

        const bool bindingVersionChanged =
            slot->materialBindingVersion != bindingVersion;
        const size_t firstInstance =
            bindingVersionChanged
                ? 0u
                : (slot->dirty ? slot->dirtyFirstInstance : instanceCount);
        const size_t endInstance =
            bindingVersionChanged
                ? instanceCount
                : (slot->dirty ? slot->dirtyEndInstance : instanceCount);
        if (firstInstance >= endInstance) {
            stats_.materialBindingVersion = slot->materialBindingVersion;
            return;
        }

        size_t firstChanged = instanceCount;
        size_t endChanged = 0u;
        for (size_t instanceIndex = firstInstance;
            instanceIndex < endInstance;
            ++instanceIndex) {

            RUNTIME::SurfaceGpuSceneInstance& instance =
                slot->mapped[instanceIndex];
            uint32_t materialSlot = 0u;
            if (instance.sourceRecordIndex < materialSlotBySourceRecord.size()) {
                const uint32_t resolved =
                    materialSlotBySourceRecord[instance.sourceRecordIndex];
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
                continue;
            }

            instance.materialDataIndex = materialSlot;
            ++stats_.materialPatchChangedCount;
            firstChanged = (std::min)(firstChanged, instanceIndex);
            endChanged = (std::max)(endChanged, instanceIndex + 1u);
        }

        if (firstChanged < endChanged) {
            MarkDirtyRange(*slot, firstChanged, endChanged);
        }
        slot->materialBindingVersion = bindingVersion;
        stats_.materialBindingVersion = bindingVersion;
    }

    bool SurfaceGpuSceneFrameBuffer::HasMaterialDataIndex(size_t instanceIndex) const {
        const FrameSlot* slot = ActiveSlot();
        if (slot == nullptr ||
            slot->mapped == nullptr ||
            instanceIndex >= stats_.uploadedInstanceCount ||
            instanceIndex >= capacity_) {
            return false;
        }

        return
            slot->mapped[instanceIndex].materialDataIndex !=
            RUNTIME::kInvalidRenderSurfaceIndex;
    }

    void SurfaceGpuSceneFrameBuffer::MarkResident(
        uint64_t layoutVersion,
        uint64_t sourceVersion,
        size_t instanceCount) {

        FrameSlot* slot = ActiveSlot();
        if (slot == nullptr || stats_.overflowInstanceCount != 0u) {
            return;
        }

        slot->resident = true;
        slot->residentInstanceCount = instanceCount;
        slot->layoutVersion = layoutVersion;
        slot->sourceVersion = sourceVersion;
    }

    void SurfaceGpuSceneFrameBuffer::CommitFrame(ID3D12GraphicsCommandList* commandList) {
        FrameSlot* slot = ActiveSlot();
        if (slot == nullptr ||
            commandList == nullptr ||
            slot->uploadBuffer == nullptr ||
            slot->defaultBuffer == nullptr ||
            !slot->dirty ||
            stats_.uploadedInstanceCount == 0u) {
            return;
        }

        const size_t dirtyFirst =
            slot->dirtyEndInstance > slot->dirtyFirstInstance
                ? slot->dirtyFirstInstance
                : 0u;
        const size_t dirtyEnd =
            slot->dirtyEndInstance > slot->dirtyFirstInstance
                ? slot->dirtyEndInstance
                : stats_.uploadedInstanceCount;
        const size_t clampedDirtyFirst =
            (std::min)(dirtyFirst, stats_.uploadedInstanceCount);
        const size_t clampedDirtyEnd =
            (std::min)(dirtyEnd, stats_.uploadedInstanceCount);
        if (clampedDirtyFirst >= clampedDirtyEnd) {
            slot->dirty = false;
            slot->dirtyFirstInstance = 0;
            slot->dirtyEndInstance = 0;
            return;
        }

        const UINT64 copyOffset =
            static_cast<UINT64>(sizeof(RUNTIME::SurfaceGpuSceneInstance)) *
            static_cast<UINT64>(clampedDirtyFirst);
        const UINT64 copyBytes =
            static_cast<UINT64>(sizeof(RUNTIME::SurfaceGpuSceneInstance)) *
            static_cast<UINT64>(clampedDirtyEnd - clampedDirtyFirst);
        if (copyBytes == 0u) {
            return;
        }

        if (slot->defaultState != D3D12_RESOURCE_STATE_COPY_DEST) {
            const auto toCopyDest = CD3DX12_RESOURCE_BARRIER::Transition(
                slot->defaultBuffer.Get(),
                slot->defaultState,
                D3D12_RESOURCE_STATE_COPY_DEST);
            commandList->ResourceBarrier(1, &toCopyDest);
            slot->defaultState = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        commandList->CopyBufferRegion(
            slot->defaultBuffer.Get(),
            copyOffset,
            slot->uploadBuffer.Get(),
            copyOffset,
            copyBytes);
        stats_.committedInstanceCount += clampedDirtyEnd - clampedDirtyFirst;
        stats_.committedBytes += static_cast<size_t>(copyBytes);

        const D3D12_RESOURCE_STATES shaderState =
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        const auto toShader = CD3DX12_RESOURCE_BARRIER::Transition(
            slot->defaultBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            shaderState);
        commandList->ResourceBarrier(1, &toShader);
        slot->defaultState = shaderState;
        slot->dirty = false;
        slot->dirtyFirstInstance = 0;
        slot->dirtyEndInstance = 0;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE SurfaceGpuSceneFrameBuffer::GetSrv() const {
        return stats_.srv;
    }

    D3D12_GPU_VIRTUAL_ADDRESS SurfaceGpuSceneFrameBuffer::GetGpuVirtualAddress() const {
        const FrameSlot* slot = ActiveSlot();
        return
            slot != nullptr && slot->defaultBuffer != nullptr
                ? slot->defaultBuffer->GetGPUVirtualAddress()
                : 0;
    }

    const SurfaceGpuSceneFrameBufferStats& SurfaceGpuSceneFrameBuffer::GetStats() const {
        return stats_;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
