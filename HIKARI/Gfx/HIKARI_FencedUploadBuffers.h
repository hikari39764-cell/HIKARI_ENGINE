#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl/client.h>

#include "Gfx/D3D12/HIKARI_D3D12BufferFactory.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

namespace HIKARI::GFX {

// 中身が変わった時だけ書き直し、変わらない間は同じ buffer を何フレームでも GPU に読ませる upload buffer の束
// （加速構造の instance 記述、光線の hit 記録など）。書き直しは GPU が読み終えた（frame fence が過ぎた）予備を
// 使い回し、まだ読まれ得る buffer には書かない。毎回 committed resource を作り直すと CPU が引っかかるのを避ける。
class FencedUploadBuffers {
public:
    // data を書いた buffer を今の buffer にし、前の今の buffer は予備へ回す。失敗なら false（今の buffer は変えない）。
    bool Write(const Context& graphics, const void* data, uint64_t bytes, const char* name) {
        if (graphics.device == nullptr || graphics.frameFence == nullptr || data == nullptr || bytes == 0)
            return false;
        const uint64_t completed = graphics.frameFence->GetCompletedValue();
        Entry next{};
        const auto spare = std::find_if(spares_.begin(), spares_.end(), [&](const Entry& entry) {
            return entry.fenceValue <= completed && entry.capacity >= bytes;
        });
        if (spare != spares_.end()) {
            next = std::move(*spare);
            spares_.erase(spare);
        } else {
            const auto desc = CD3DX12_RESOURCE_DESC::Buffer(bytes);
            next.allocationBytes = graphics.device->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
            void* created = nullptr;
            if (next.allocationBytes == UINT64_MAX ||
                !D3D12_BUFFER::CreateMappedUploadBuffer(graphics.device, bytes, next.buffer, &created))
                return false;
            next.buffer->Unmap(0, nullptr);
            next.capacity = bytes;
        }
        void* mapped = nullptr;
        const D3D12_RANGE noRead{0, 0};
        if (FAILED(next.buffer->Map(0, &noRead, &mapped))) {
            spares_.push_back(std::move(next));
            return false;
        }
        std::memcpy(mapped, data, static_cast<size_t>(bytes));
        next.buffer->Unmap(0, nullptr);
        Promote(graphics, std::move(next), name);
        return true;
    }
    // 外で作って書いた buffer を今の buffer として預かる（このフレームから読まれる）。
    void Adopt(const Context& graphics, Microsoft::WRL::ComPtr<ID3D12Resource> buffer, uint64_t capacity,
               uint64_t allocationBytes, const char* name) {
        Promote(graphics, Entry{std::move(buffer), capacity, allocationBytes, 0}, name);
    }
    // 今の buffer をこのフレームでも読む（その間は予備にしても書き換えない）。
    void MarkRead(const Context& graphics) noexcept { current_.fenceValue = graphics.currentFrameRetireFenceValue; }
    ID3D12Resource* Current() const noexcept { return current_.buffer.Get(); }
    uint64_t AllocationBytes() const noexcept {
        uint64_t bytes = current_.allocationBytes;
        for (const auto& spare : spares_)
            bytes += spare.allocationBytes;
        return bytes;
    }
    void Reset(const Context& graphics, const char* name) {
        RetireD3D12ObjectForFrame(current_.buffer, graphics, name);
        for (auto& spare : spares_)
            RetireD3D12ObjectForFrame(spare.buffer, graphics, name);
        current_ = {};
        spares_.clear();
    }

private:
    // 予備は少数に保つ（重なって走るフレームの分あれば足りる）。
    static constexpr size_t kMaximumSpares = 4;
    // fenceValue はこの buffer を読む作業を最後に積んだフレームの retire fence。
    struct Entry {
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer{};
        uint64_t capacity = 0;
        uint64_t allocationBytes = 0;
        uint64_t fenceValue = 0;
    };
    // next を今の buffer にし、前の今の buffer を予備へ回す（多すぎる予備は退役する）。
    void Promote(const Context& graphics, Entry next, const char* name) {
        next.fenceValue = graphics.currentFrameRetireFenceValue;
        if (current_.buffer)
            spares_.push_back(std::move(current_));
        current_ = std::move(next);
        while (spares_.size() > kMaximumSpares) {
            RetireD3D12ObjectForFrame(spares_.front().buffer, graphics, name);
            spares_.erase(spares_.begin());
        }
    }

    Entry current_{};
    std::vector<Entry> spares_{};
};

} // namespace HIKARI::GFX
