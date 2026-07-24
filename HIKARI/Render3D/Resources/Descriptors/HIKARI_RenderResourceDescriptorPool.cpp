#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

#include "Gfx/HIKARI_DescriptorAllocator.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"

namespace HIKARI::RENDER3D {

    namespace {

        struct RenderResourceDescriptorPoolState {
            GFX::Context context{};
            GFX::DescriptorAllocator allocator{};
            UINT descriptorSize = 0;
            bool initialized = false;
            uint32_t allocationCount = 0;
            uint32_t freeCount = 0;
            uint32_t failedAllocationCount = 0;
        };

        RenderResourceDescriptorPoolState& State() {
            static RenderResourceDescriptorPoolState state{};
            return state;
        }

        bool HasValidContext(const GFX::Context& ctx) {
            return ctx.device != nullptr && ctx.srvHeap != nullptr;
        }

        void RefreshContext(RenderResourceDescriptorPoolState& state, const GFX::Context& ctx) {
            state.context = ctx;
            if (!HasValidContext(ctx)) {
                return;
            }

            state.descriptorSize = ctx.device->GetDescriptorHandleIncrementSize(
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        RenderResourceView AllocateDescriptor(RenderResourceDescriptorPoolState& state) {
            RenderResourceView view{};
            if (!state.initialized || !HasValidContext(state.context)) {
                ++state.failedAllocationCount;
                return view;
            }

            const GFX::DescriptorSlot slot = state.allocator.Allocate();
            if (!slot.IsValid()) {
                ++state.failedAllocationCount;
                return view;
            }

            view.descriptorIndex = slot.index;
            view.cpu =
                GFX::DESCRIPTOR::CpuAt(state.context.srvHeap, state.descriptorSize, slot.index);
            view.gpu =
                GFX::DESCRIPTOR::GpuAt(state.context.srvHeap, state.descriptorSize, slot.index);
            ++state.allocationCount;
            return view;
        }

    } // namespace

    void UpdateRenderResourceDescriptorPoolContext(const GFX::Context& ctx) {
        RenderResourceDescriptorPoolState& state = State();
        RefreshContext(state, ctx);

        if (state.initialized || !HasValidContext(ctx)) {
            return;
        }

        // リソース層のSRVは、固定システムSRVの後ろにある動的領域だけを使う。
        state.allocator.Initialize(
            GFX::DESCRIPTOR::kSystemSrvDynamicBegin,
            GFX::DESCRIPTOR::kSystemSrvDynamicCount);
        state.initialized = true;
    }

    void ShutdownRenderResourceDescriptorPool() {
        RenderResourceDescriptorPoolState& state = State();
        state = {};
    }

    RenderResourceView AllocateRenderResourceDescriptor() {
        return AllocateDescriptor(State());
    }

    RenderResourceView AllocateBufferSrvDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT numElements,
        UINT structureByteStride,
        D3D12_BUFFER_SRV_FLAGS flags) {

        RenderResourceDescriptorPoolState& state = State();
        if (!state.initialized ||
            !HasValidContext(state.context) ||
            resource == nullptr ||
            numElements == 0) {
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

    RenderResourceView AllocateTexture2DSrvDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT mostDetailedMip,
        UINT mipLevels) {

        RenderResourceDescriptorPoolState& state = State();
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

    RenderResourceView AllocateTexture2DUavDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT mipSlice) {

        RenderResourceDescriptorPoolState& state = State();
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

    RenderResourceView AllocateTexture3DSrvDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT mostDetailedMip,
        UINT mipLevels) {

        RenderResourceDescriptorPoolState& state = State();
        if (resource == nullptr || mipLevels == 0) {
            ++state.failedAllocationCount;
            return {};
        }
        RenderResourceView view = AllocateDescriptor(state);
        if (!view.IsValid()) return {};

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

    RenderResourceView AllocateTexture3DUavDescriptor(
        ID3D12Resource* resource,
        DXGI_FORMAT format,
        UINT mipSlice,
        UINT firstWSlice,
        UINT wSize) {

        RenderResourceDescriptorPoolState& state = State();
        if (resource == nullptr) {
            ++state.failedAllocationCount;
            return {};
        }
        RenderResourceView view = AllocateDescriptor(state);
        if (!view.IsValid()) return {};

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
        if (!state.initialized || view.descriptorIndex == UINT32_MAX) {
            return false;
        }

        const GFX::DescriptorSlot slot{ view.descriptorIndex };
        if (!state.allocator.Owns(slot) || !state.allocator.IsAllocated(slot)) {
            return false;
        }

        state.allocator.Free(slot);
        ++state.freeCount;
        return true;
    }

    RenderResourceDescriptorPoolStats GetRenderResourceDescriptorPoolStats() {
        const RenderResourceDescriptorPoolState& state = State();

        RenderResourceDescriptorPoolStats stats{};
        stats.initialized = state.initialized && HasValidContext(state.context);
        stats.begin = state.allocator.GetBegin();
        stats.capacity = state.allocator.GetCount();
        stats.used = state.allocator.GetUsedCount();
        stats.free = state.allocator.GetFreeCount();
        stats.allocationCount = state.allocationCount;
        stats.freeCount = state.freeCount;
        stats.failedAllocationCount = state.failedAllocationCount;
        return stats;
    }

} // namespace HIKARI::RENDER3D
