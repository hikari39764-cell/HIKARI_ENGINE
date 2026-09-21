#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <wrl.h>
#include <cstdint>
#include <span>
#include <vector>

namespace HIKARI::GFX {
    // 所有スレッドから DIRECT queue に投入する。利用側の描画も同じ queue で
    // 後から投入し、CPU wait なしで upload -> draw の順序を保証する。
    class UploadSubmissionQueue {
    public:
        struct Limits {
            size_t submissions = 8;
            uint64_t stagingBytes = 64ull * 1024 * 1024;
        };
        struct Stats {
            uint64_t submissions = 0, waits = 0, pendingBytes = 0, peakPendingBytes = 0;
        };
        UploadSubmissionQueue() = default;
        ~UploadSubmissionQueue();
        UploadSubmissionQueue(const UploadSubmissionQueue&) = delete;
        UploadSubmissionQueue& operator=(const UploadSubmissionQueue&) = delete;
        bool Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, Limits limits);
        ID3D12GraphicsCommandList* Begin();
        // staging と destination の両方を保持する。公開処理が失敗しても GPU は安全。
        bool Submit(std::span<ID3D12Resource* const> resources, uint64_t stagingBytes);
        void Cancel();
        void Collect();
        void Reset();
        const Stats& GetStats() const noexcept { return stats_; }

    private:
        struct Slot {
            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commands;
            std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> resources;
            uint64_t fenceValue = 0, bytes = 0;
        };
        bool WaitOldest();
        Microsoft::WRL::ComPtr<ID3D12Device> device_;
        Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
        Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
        HANDLE event_ = nullptr;
        uint64_t nextFence_ = 1;
        size_t recording_ = SIZE_MAX;
        Limits limits_;
        Stats stats_;
        std::vector<Slot> slots_;
    };
}
