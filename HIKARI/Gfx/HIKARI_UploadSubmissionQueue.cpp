#include "HIKARI_UploadSubmissionQueue.h"
#include "HIKARI_GpuCompletion.h"
#include <algorithm>

namespace HIKARI::GFX {
    UploadSubmissionQueue::~UploadSubmissionQueue() { Reset(); }

    bool UploadSubmissionQueue::Initialize(ID3D12Device* device, ID3D12CommandQueue* queue, Limits limits) {
        if (!device || !queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT ||
            !limits.submissions || !limits.stagingBytes) return false;
        if (device_.Get() == device && queue_.Get() == queue && fence_) return true;
        Reset();
        device_ = device; queue_ = queue; limits_ = limits;
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) {
            Reset(); return false;
        }
        event_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (!event_) { Reset(); return false; }
        return true;
    }

    void UploadSubmissionQueue::Collect() {
        if (!fence_) return;
        const auto completed = fence_->GetCompletedValue();
        for (auto& slot : slots_) if (slot.fenceValue && completed >= slot.fenceValue) {
            stats_.pendingBytes -= slot.bytes;
            slot.resources.clear(); slot.fenceValue = 0; slot.bytes = 0;
        }
    }

    bool UploadSubmissionQueue::WaitOldest() {
        uint64_t oldest = UINT64_MAX;
        for (const auto& slot : slots_) if (slot.fenceValue) oldest = (std::min)(oldest, slot.fenceValue);
        if (oldest == UINT64_MAX) return false;
        if (fence_->GetCompletedValue() < oldest) ++stats_.waits;
        const bool ok = WaitForGpuCompletion(device_.Get(), fence_.Get(), oldest, event_, "Resource upload queue");
        Collect();
        return ok;
    }

    ID3D12GraphicsCommandList* UploadSubmissionQueue::Begin() {
        if (!fence_ || FAILED(device_->GetDeviceRemovedReason())) return nullptr;
        Cancel(); Collect();
        size_t available = 0;
        for (;;) {
            for (available = 0; available < slots_.size(); ++available)
                if (!slots_[available].fenceValue) break;
            if (available < slots_.size() || slots_.size() < limits_.submissions) break;
            if (!WaitOldest()) return nullptr;
        }
        if (available == slots_.size()) {
            Slot slot;
            if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&slot.allocator))) ||
                FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, slot.allocator.Get(), nullptr,
                    IID_PPV_ARGS(&slot.commands))) || FAILED(slot.commands->Close())) return nullptr;
            slots_.push_back(std::move(slot));
        }
        auto& slot = slots_[available];
        if (FAILED(slot.allocator->Reset()) || FAILED(slot.commands->Reset(slot.allocator.Get(), nullptr))) return nullptr;
        recording_ = available;
        return slot.commands.Get();
    }

    bool UploadSubmissionQueue::Submit(std::span<ID3D12Resource* const> resources, uint64_t bytes) {
        if (recording_ == SIZE_MAX) return false;
        auto& slot = slots_[recording_];
        if (FAILED(slot.commands->Close())) { recording_ = SIZE_MAX; return false; }
        recording_ = SIZE_MAX;
        Collect();
        // 予算より大きい単体 upload は、先行分を回収して単独で許可する。
        while (stats_.pendingBytes && (bytes > limits_.stagingBytes ||
                stats_.pendingBytes > limits_.stagingBytes - bytes))
            if (!WaitOldest()) return false;
        slot.resources.clear();
        slot.resources.reserve(resources.size());
        for (auto* resource : resources) slot.resources.emplace_back(resource);
        ID3D12CommandList* lists[]{slot.commands.Get()};
        queue_->ExecuteCommandLists(1, lists);
        slot.fenceValue = nextFence_++; slot.bytes = bytes;
        stats_.pendingBytes += bytes;
        stats_.peakPendingBytes = (std::max)(stats_.peakPendingBytes, stats_.pendingBytes);
        ++stats_.submissions;
        if (FAILED(queue_->Signal(fence_.Get(), slot.fenceValue))) {
            if (FAILED(device_->GetDeviceRemovedReason())) { Collect(); return false; }
            AbortUnsafeGpuSubmission("Resource upload queue signal");
        }
        return true;
    }

    void UploadSubmissionQueue::Cancel() {
        if (recording_ == SIZE_MAX) return;
        slots_[recording_].commands->Close();
        recording_ = SIZE_MAX;
    }

    void UploadSubmissionQueue::Reset() {
        Cancel();
        if (fence_ && nextFence_ > 1)
            WaitForGpuCompletion(device_.Get(), fence_.Get(), nextFence_ - 1, event_, "Resource upload shutdown");
        slots_.clear(); fence_.Reset(); queue_.Reset(); device_.Reset();
        if (event_) { CloseHandle(event_); event_ = nullptr; }
        nextFence_ = 1; stats_ = {};
    }
}
