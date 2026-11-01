#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "HIKARI_ContractAssert.h"

namespace HIKARI::TESTS::INTERNAL {

// 製品の bin を使わない、実 D3D12 コマンド実行用の WARP fixture。
struct GpuContractDevice final {
    template <class T>
    using ComPtr = Microsoft::WRL::ComPtr<T>;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;

    explicit GpuContractDevice(ID3D12Device* selectedDevice = nullptr) {
        device = selectedDevice;
        if (!device) {
            ComPtr<IDXGIFactory4> factory;
            ComPtr<IDXGIAdapter> warp;
            assert(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))));
            assert(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))));
            assert(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))));
        }
        const D3D12_COMMAND_QUEUE_DESC queueDesc{D3D12_COMMAND_LIST_TYPE_DIRECT};
        assert(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))));
        assert(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
        assert(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                                   IID_PPV_ARGS(&commands))));
        assert(SUCCEEDED(commands->Close()));
        assert(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        assert(event != nullptr);
    }
    ~GpuContractDevice() { CloseHandle(event); }
    GpuContractDevice(const GpuContractDevice&) = delete;
    GpuContractDevice& operator=(const GpuContractDevice&) = delete;
    void ResetCommands() {
        assert(SUCCEEDED(allocator->Reset()));
        assert(SUCCEEDED(commands->Reset(allocator.Get(), nullptr)));
    }
    void SubmitAndWait(uint64_t retireFence) {
        assert(SUCCEEDED(commands->Close()));
        ID3D12CommandList* lists[]{commands.Get()};
        queue->ExecuteCommandLists(1, lists);
        assert(SUCCEEDED(queue->Signal(fence.Get(), retireFence)));
        assert(SUCCEEDED(fence->SetEventOnCompletion(retireFence, event)));
        // WARP では VfxStyleGpuContracts の 37 効果の比較が 10 秒を超える。
        // ctest の TIMEOUT（60 秒）に揃え、止まった時だけ落とす。
        const DWORD waitResult = WaitForSingleObject(event, 60000);
        if (waitResult != WAIT_OBJECT_0)
            std::fprintf(stderr, "GPU wait=%lu completed=%llu target=%llu removed=0x%08lx\n", waitResult,
                         static_cast<unsigned long long>(fence->GetCompletedValue()),
                         static_cast<unsigned long long>(retireFence),
                         static_cast<unsigned long>(device->GetDeviceRemovedReason()));
        assert(waitResult == WAIT_OBJECT_0);
    }
};

} // namespace HIKARI::TESTS::INTERNAL
