#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>

#include <wrl/client.h>

struct IUnknown;

namespace HIKARI::GFX {

    struct Context;

    class GpuDeferredReleaseQueue {
    public:
        void Enqueue(
            uint64_t retireFenceValue,
            std::function<void()> releaseCallback,
            std::string debugName = {});

        void Collect(uint64_t completedFenceValue);
        void FlushAll();
        void Reset();

        size_t GetPendingCount() const;

    private:
        struct PendingRelease {
            uint64_t retireFenceValue = 0;
            std::function<void()> releaseCallback;
            std::string debugName;
        };

        std::deque<PendingRelease> pending_;
    };

    void RetireD3D12ObjectForFrame(
        IUnknown* object,
        const Context& context,
        std::string debugName = {});

    void RetireD3D12ObjectForCurrentFrame(
        IUnknown* object,
        std::string debugName = {});

    template <typename T>
    void RetireD3D12ObjectForFrame(
        Microsoft::WRL::ComPtr<T>& object,
        const Context& context,
        std::string debugName = {}) {

        RetireD3D12ObjectForFrame(
            object.Detach(),
            context,
            std::move(debugName));
    }

    template <typename T>
    void RetireD3D12ObjectForCurrentFrame(
        Microsoft::WRL::ComPtr<T>& object,
        std::string debugName = {}) {

        RetireD3D12ObjectForCurrentFrame(
            object.Detach(),
            std::move(debugName));
    }

} // namespace HIKARI::GFX
