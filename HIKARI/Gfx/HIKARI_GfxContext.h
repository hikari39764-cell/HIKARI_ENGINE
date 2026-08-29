#pragma once

#include <cstddef>
#include <cstdint>
#include <d3d12.h>

namespace HIKARI::GFX {

constexpr uint32_t kFrameResourceCount = 3;

class GpuDeferredReleaseQueue;
class ResourceStateTracker;

struct Context {
    uint64_t deviceGeneration{};
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
    ID3D12Fence* frameFence{};
    uint64_t currentFrameRetireFenceValue{};

    uint32_t frameIndex{};
    int backBufferWidth{};
    int backBufferHeight{};
};

// 現在の単一 DX12 デバイス状態を Gfx 層が所有する。
// Publish/Clear は Runtime の呈現スレッドだけが実行し、描画側は読み取り専用で参照する。
void PublishCurrentContext(const Context& context) noexcept;
void ClearCurrentContext() noexcept;
const Context& GetCurrentContext() noexcept;
size_t GetCurrentDeferredReleaseCount() noexcept;
bool IsCurrentFenceComplete(uint64_t fenceValue) noexcept;

} // namespace HIKARI::GFX
