#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_GfxContext.h"

#include <utility>
#include <unknwn.h>

namespace HIKARI::SERVICES {
    extern GFX::Context gCtx;
}

namespace HIKARI::GFX {
    namespace {
        void RetireD3D12ObjectAfterFence(
            IUnknown* object,
            GpuDeferredReleaseQueue* queue,
            uint64_t retireFenceValue,
            std::string debugName) {

            if (object == nullptr) {
                return;
            }

            if (queue != nullptr && retireFenceValue != 0) {
                queue->Enqueue(
                    retireFenceValue,
                    [object]() {
                        object->Release();
                    },
                    std::move(debugName));
                return;
            }

            object->Release();
        }
    }
	// GPU による遅延解放のためのキュークラス GpuDeferredReleaseQueue の実装
	// retireFenceValue と releaseCallback を受け取り、保留リストに追加する
    // retireFenceValue は、GPU がこのリリースを安全に実行できるようになるフェンスの値で、releaseCallback は実際のリリース処理を行うコールバック関数である
    void GpuDeferredReleaseQueue::Enqueue(
        uint64_t retireFenceValue,
        std::function<void()> releaseCallback,
        std::string debugName) {

        if (!releaseCallback) {
            return;
        }

        PendingRelease item{};
        item.retireFenceValue = retireFenceValue;
        item.releaseCallback = std::move(releaseCallback);
        item.debugName = std::move(debugName);
        pending_.push_back(std::move(item));
    }
	// completedFenceValue を受け取り、保留リストの先頭から順に retireFenceValue が completedFenceValue 以下のアイテムを処理する
    void GpuDeferredReleaseQueue::Collect(uint64_t completedFenceValue) {
        while (!pending_.empty()) {
            PendingRelease& item = pending_.front();
            if (item.retireFenceValue > completedFenceValue) {
                break;
            }

            if (item.releaseCallback) {
                item.releaseCallback();
            }
            pending_.pop_front();
        }
    }
	// 保留リストのすべてのアイテムを処理する。GPU の完了を待たずにすべてのリリースを強制的に実行するために使用される
    void GpuDeferredReleaseQueue::FlushAll() {
        while (!pending_.empty()) {
            PendingRelease& item = pending_.front();
            if (item.releaseCallback) {
                item.releaseCallback();
            }
            pending_.pop_front();
        }
    }
	// 保留リストをクリアする。通常は、GPU の完了を待たずにすべてのリリースを強制的に実行した後に呼び出される
    void GpuDeferredReleaseQueue::Reset() {
        pending_.clear();
    }
	// 保留リストに現在存在するアイテムの数を返す
    size_t GpuDeferredReleaseQueue::GetPendingCount() const {
        return pending_.size();
    }

    void RetireD3D12ObjectForFrame(
        IUnknown* object,
        const Context& context,
        std::string debugName) {

        RetireD3D12ObjectAfterFence(
            object,
            context.deferredReleaseQueue,
            context.currentFrameRetireFenceValue,
            std::move(debugName));
    }

    void RetireD3D12ObjectForCurrentFrame(
        IUnknown* object,
        std::string debugName) {

        RetireD3D12ObjectForFrame(
            object,
            SERVICES::gCtx,
            std::move(debugName));
    }

} // namespace HIKARI::GFX
