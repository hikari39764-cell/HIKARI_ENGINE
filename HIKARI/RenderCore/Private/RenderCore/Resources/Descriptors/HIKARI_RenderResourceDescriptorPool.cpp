#include "RenderCore/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

#include <mutex>
#include <string>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorAllocator.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

namespace HIKARI::RENDER3D {

namespace {

struct RenderResourceDescriptorPoolState {
    GFX::Context context{};
    GFX::DescriptorAllocator allocator{};
    GFX::DescriptorAllocator clusterAllocator{};
    UINT descriptorSize = 0;
    bool initialized = false;
    bool contextAvailable = false;
    bool contextCompatible = true;
    uint64_t ownerDeviceGeneration = 0;
    ID3D12Device* ownerDevice = nullptr;
    ID3D12DescriptorHeap* ownerHeap = nullptr;
    uint32_t allocationCount = 0;
    uint32_t freeCount = 0;
    uint32_t clusterAllocationCount = 0;
    uint32_t clusterFreeCount = 0;
    uint32_t failedAllocationCount = 0;
    uint32_t clusterFailedAllocationCount = 0;
    uint32_t rejectedContextUpdateCount = 0;
    std::mutex mutex{};
};

RenderResourceDescriptorPoolState& State() {
    static RenderResourceDescriptorPoolState state{};
    return state;
}

bool HasValidContext(const GFX::Context& ctx) {
    if (ctx.deviceGeneration == 0u || ctx.device == nullptr || ctx.srvHeap == nullptr)
        return false;
    const auto heap = ctx.srvHeap->GetDesc();
    // System/cluster の絶対 index を持つ pool に user-only heap を渡さない。
    return heap.Type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV &&
           (heap.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) != 0 &&
           heap.NumDescriptors >= GFX::DESCRIPTOR::kSrvHeapCapacity;
}

bool RefreshContext(RenderResourceDescriptorPoolState& state, const GFX::Context& ctx) {
    if (!HasValidContext(ctx)) {
        state.contextAvailable = false;
        return false;
    }

    if (state.initialized && (state.ownerDeviceGeneration != ctx.deviceGeneration || state.ownerDevice != ctx.device ||
                              state.ownerHeap != ctx.srvHeap)) {
        state.contextAvailable = false;
        state.contextCompatible = false;
        ++state.rejectedContextUpdateCount;
        if (state.rejectedContextUpdateCount == 1u) {
            HIKARI_LOG_ERROR("Render resource descriptor pool rejected an "
                             "incompatible device or SRV heap context.");
        }
        return false;
    }

    state.context = ctx;
    state.contextAvailable = true;
    state.contextCompatible = true;
    state.ownerDeviceGeneration = ctx.deviceGeneration;
    state.ownerDevice = ctx.device;
    state.ownerHeap = ctx.srvHeap;
    state.descriptorSize = ctx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

RenderResourceView AllocateDescriptor(RenderResourceDescriptorPoolState& state) {
    RenderResourceView view{};
    if (!state.initialized || !state.contextAvailable || !state.contextCompatible || !HasValidContext(state.context)) {
        ++state.failedAllocationCount;
        return view;
    }

    const GFX::DescriptorSlot slot = state.allocator.Allocate();
    if (!slot.IsValid()) {
        ++state.failedAllocationCount;
        return view;
    }

    view.descriptorIndex = slot.index;
    view.cpu = GFX::DESCRIPTOR::CpuAt(state.context.srvHeap, state.descriptorSize, slot.index);
    view.gpu = GFX::DESCRIPTOR::GpuAt(state.context.srvHeap, state.descriptorSize, slot.index);
    ++state.allocationCount;
    return view;
}

std::vector<RenderResourceView> AllocateDescriptorRange(RenderResourceDescriptorPoolState& state,
                                                        GFX::DescriptorAllocator& allocator, uint32_t count,
                                                        uint32_t& allocationCount, uint32_t& failedAllocationCount) {
    std::vector<RenderResourceView> views{};
    if (!state.initialized || !state.contextAvailable || !state.contextCompatible || !HasValidContext(state.context) ||
        count == 0u) {
        ++failedAllocationCount;
        return views;
    }
    views.reserve(count);
    const GFX::DescriptorSlot first = allocator.AllocateRange(count);
    if (!first.IsValid()) {
        ++failedAllocationCount;
        return views;
    }
    for (uint32_t offset = 0u; offset < count; ++offset) {
        RenderResourceView view{};
        view.descriptorIndex = first.index + offset;
        view.cpu = GFX::DESCRIPTOR::CpuAt(state.context.srvHeap, state.descriptorSize, view.descriptorIndex);
        view.gpu = GFX::DESCRIPTOR::GpuAt(state.context.srvHeap, state.descriptorSize, view.descriptorIndex);
        views.push_back(view);
    }
    allocationCount += count;
    return views;
}

std::vector<RenderResourceView> AllocateDynamicDescriptorRange(RenderResourceDescriptorPoolState& state,
                                                               uint32_t count) {
    return AllocateDescriptorRange(state, state.allocator, count, state.allocationCount, state.failedAllocationCount);
}

std::vector<RenderResourceView> AllocateClusterDescriptorRange(RenderResourceDescriptorPoolState& state,
                                                               uint32_t count) {
    return AllocateDescriptorRange(state, state.clusterAllocator, count, state.clusterAllocationCount,
                                   state.clusterFailedAllocationCount);
}

} // namespace

void UpdateRenderResourceDescriptorPoolContext(const GFX::Context& ctx) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (!RefreshContext(state, ctx) || state.initialized) {
        return;
    }

    // リソース層のSRVは、固定システムSRVの後ろにある動的領域だけを使う。
    state.allocator.Initialize(GFX::DESCRIPTOR::kSystemSrvDynamicBegin, GFX::DESCRIPTOR::kSystemSrvDynamicCount);
    state.clusterAllocator.Initialize(GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaBegin,
                                      GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity);
    state.initialized = true;
}

void ShutdownRenderResourceDescriptorPool() {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    state.context = {};
    state.allocator = {};
    state.clusterAllocator = {};
    state.descriptorSize = 0;
    state.initialized = false;
    state.contextAvailable = false;
    state.contextCompatible = true;
    state.ownerDeviceGeneration = 0;
    state.ownerDevice = nullptr;
    state.ownerHeap = nullptr;
    state.allocationCount = 0;
    state.freeCount = 0;
    state.clusterAllocationCount = 0;
    state.clusterFreeCount = 0;
    state.failedAllocationCount = 0;
    state.clusterFailedAllocationCount = 0;
    state.rejectedContextUpdateCount = 0;
}

RenderResourceView AllocateRenderResourceDescriptor() {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    return AllocateDescriptor(state);
}

std::vector<RenderResourceView> AllocateRenderResourceDescriptorRange(uint32_t count) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    return AllocateDynamicDescriptorRange(state, count);
}

std::vector<RenderResourceView> AllocateClusterGeometryDescriptorRange(uint32_t count) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (count == 0u || count > GFX::DESCRIPTOR::kClusterGeometryDescriptorArenaCapacity ||
        count % GFX::DESCRIPTOR::kClusterGeometryDescriptorCountPerResource != 0u) {
        ++state.clusterFailedAllocationCount;
        return {};
    }
    return AllocateClusterDescriptorRange(state, count);
}

RenderResourceView AllocateBufferSrvDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, UINT numElements,
                                               UINT structureByteStride, D3D12_BUFFER_SRV_FLAGS flags) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (!state.initialized || !state.contextAvailable || !state.contextCompatible || !HasValidContext(state.context) ||
        resource == nullptr || numElements == 0) {
        ++state.failedAllocationCount;
        return {};
    }

    RenderResourceView view = AllocateDescriptor(state);
    if (!view.IsValid()) {
        return {};
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = numElements;
    srvDesc.Buffer.StructureByteStride = structureByteStride;
    srvDesc.Buffer.Flags = flags;

    state.context.device->CreateShaderResourceView(resource, &srvDesc, view.cpu);
    return view;
}

RenderResourceView AllocateBufferUavDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, UINT numElements,
                                               UINT structureByteStride, ID3D12Resource* counterResource,
                                               UINT64 counterOffsetBytes, D3D12_BUFFER_UAV_FLAGS flags) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (!state.initialized || !state.contextAvailable || !state.contextCompatible || !HasValidContext(state.context) ||
        resource == nullptr || numElements == 0u) {
        ++state.failedAllocationCount;
        return {};
    }

    RenderResourceView view = AllocateDescriptor(state);
    if (!view.IsValid()) {
        return {};
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0u;
    uavDesc.Buffer.NumElements = numElements;
    uavDesc.Buffer.StructureByteStride = structureByteStride;
    uavDesc.Buffer.CounterOffsetInBytes = counterOffsetBytes;
    uavDesc.Buffer.Flags = flags;
    state.context.device->CreateUnorderedAccessView(resource, counterResource, &uavDesc, view.cpu);
    return view;
}

RenderResourceView AllocateTexture2DSrvDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, UINT mostDetailedMip,
                                                  UINT mipLevels) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (resource == nullptr || mipLevels == 0) {
        ++state.failedAllocationCount;
        return {};
    }

    RenderResourceView view = AllocateDescriptor(state);
    if (!view.IsValid()) {
        return {};
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = mostDetailedMip;
    srvDesc.Texture2D.MipLevels = mipLevels;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
    state.context.device->CreateShaderResourceView(resource, &srvDesc, view.cpu);
    return view;
}

RenderResourceView AllocateTexture2DUavDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, UINT mipSlice) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (resource == nullptr) {
        ++state.failedAllocationCount;
        return {};
    }

    RenderResourceView view = AllocateDescriptor(state);
    if (!view.IsValid()) {
        return {};
    }

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = mipSlice;
    uavDesc.Texture2D.PlaneSlice = 0;
    state.context.device->CreateUnorderedAccessView(resource, nullptr, &uavDesc, view.cpu);
    return view;
}

RenderResourceView AllocateTexture3DSrvDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, UINT mostDetailedMip,
                                                  UINT mipLevels) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (resource == nullptr || mipLevels == 0) {
        ++state.failedAllocationCount;
        return {};
    }
    RenderResourceView view = AllocateDescriptor(state);
    if (!view.IsValid())
        return {};

    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    desc.Format = format;
    desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    desc.Texture3D.MostDetailedMip = mostDetailedMip;
    desc.Texture3D.MipLevels = mipLevels;
    desc.Texture3D.ResourceMinLODClamp = 0.0f;
    state.context.device->CreateShaderResourceView(resource, &desc, view.cpu);
    return view;
}

RenderResourceView AllocateTexture3DUavDescriptor(ID3D12Resource* resource, DXGI_FORMAT format, UINT mipSlice,
                                                  UINT firstWSlice, UINT wSize) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (resource == nullptr) {
        ++state.failedAllocationCount;
        return {};
    }
    RenderResourceView view = AllocateDescriptor(state);
    if (!view.IsValid())
        return {};

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = format;
    desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
    desc.Texture3D.MipSlice = mipSlice;
    desc.Texture3D.FirstWSlice = firstWSlice;
    desc.Texture3D.WSize = wSize;
    state.context.device->CreateUnorderedAccessView(resource, nullptr, &desc, view.cpu);
    return view;
}

bool ReleaseRenderResourceDescriptor(RenderResourceView view) {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);
    if (!state.initialized || view.descriptorIndex == UINT32_MAX) {
        return false;
    }

    const GFX::DescriptorSlot slot{view.descriptorIndex};
    if (state.allocator.Owns(slot) && state.allocator.IsAllocated(slot)) {
        state.allocator.Free(slot);
        ++state.freeCount;
        return true;
    }
    if (state.clusterAllocator.Owns(slot) && state.clusterAllocator.IsAllocated(slot)) {
        state.clusterAllocator.Free(slot);
        ++state.clusterFreeCount;
        return true;
    }
    return false;
}

void RetireRenderResourceDescriptor(RenderResourceView& view, const GFX::Context& context,
                                    const char* debugName) noexcept {
    if (!view.IsValid()) {
        return;
    }

    const RenderResourceView retired = view;
    if (context.deferredReleaseQueue != nullptr && context.currentFrameRetireFenceValue != 0u) {
        try {
            context.deferredReleaseQueue->Enqueue(
                context.currentFrameRetireFenceValue, [retired]() { ReleaseRenderResourceDescriptor(retired); },
                debugName != nullptr ? debugName : "Render Resource Descriptor");
        } catch (...) {
            // Queue admission failed before ownership transferred. Keep
            // the descriptor allocator coherent even under allocation
            // pressure; the caller's view is cleared below.
            ReleaseRenderResourceDescriptor(retired);
            view = {};
            return;
        }
        view = {};
        return;
    }
    ReleaseRenderResourceDescriptor(retired);
    view = {};
}

RenderResourceDescriptorPoolStats GetRenderResourceDescriptorPoolStats() {
    RenderResourceDescriptorPoolState& state = State();
    std::scoped_lock lock(state.mutex);

    RenderResourceDescriptorPoolStats stats{};
    stats.initialized =
        state.initialized && state.contextAvailable && state.contextCompatible && HasValidContext(state.context);
    stats.contextAvailable = state.contextAvailable;
    stats.contextCompatible = state.contextCompatible;
    stats.deviceGeneration = state.ownerDeviceGeneration;
    stats.begin = state.allocator.GetBegin();
    stats.capacity = state.allocator.GetCount();
    stats.used = state.allocator.GetUsedCount();
    stats.free = state.allocator.GetFreeCount();
    stats.clusterBegin = state.clusterAllocator.GetBegin();
    stats.clusterCapacity = state.clusterAllocator.GetCount();
    stats.clusterUsed = state.clusterAllocator.GetUsedCount();
    stats.clusterFree = state.clusterAllocator.GetFreeCount();
    stats.allocationCount = state.allocationCount;
    stats.freeCount = state.freeCount;
    stats.clusterAllocationCount = state.clusterAllocationCount;
    stats.clusterFreeCount = state.clusterFreeCount;
    stats.failedAllocationCount = state.failedAllocationCount;
    stats.clusterFailedAllocationCount = state.clusterFailedAllocationCount;
    stats.rejectedContextUpdateCount = state.rejectedContextUpdateCount;
    return stats;
}

} // namespace HIKARI::RENDER3D
