#include "Gfx/HIKARI_GfxContext.h"

#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

namespace HIKARI::GFX {
    namespace {
        Context gCurrentContext{};
    }

    void PublishCurrentContext(const Context& context) noexcept {
        gCurrentContext = context;
    }

    void ClearCurrentContext() noexcept {
        gCurrentContext = {};
    }

    const Context& GetCurrentContext() noexcept {
        return gCurrentContext;
    }

    size_t GetCurrentDeferredReleaseCount() noexcept {
        return gCurrentContext.deferredReleaseQueue != nullptr
            ? gCurrentContext.deferredReleaseQueue->GetPendingCount()
            : 0;
    }

    bool IsCurrentFenceComplete(uint64_t fenceValue) noexcept {
        return gCurrentContext.frameFence != nullptr &&
            gCurrentContext.frameFence->GetCompletedValue() >= fenceValue;
    }
} // namespace HIKARI::GFX
