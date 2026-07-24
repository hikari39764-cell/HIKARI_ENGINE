#pragma once

#include <cstdint>
#include <d3d12.h>

namespace HIKARI::GFX {

constexpr uint32_t kFrameResourceCount = 3;

class GpuDeferredReleaseQueue;
class ResourceStateTracker;

struct Context {
    ID3D12Device* device{};
    ID3D12GraphicsCommandList* cmdList{};
    ID3D12CommandQueue* queue{};

    ID3D12DescriptorHeap* srvHeap{};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    D3D12_CPU_DESCRIPTOR_HANDLE readOnlyDsv{};

    D3D12_CPU_DESCRIPTOR_HANDLE sceneDepthSrvCpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
    ID3D12Resource* sceneDepthResource{};
    ResourceStateTracker* resourceStates{};
    GpuDeferredReleaseQueue* deferredReleaseQueue{};
    uint64_t currentFrameRetireFenceValue{};

    uint32_t frameIndex{};
    int backBufferWidth{};
    int backBufferHeight{};
};

} // namespace HIKARI::GFX
