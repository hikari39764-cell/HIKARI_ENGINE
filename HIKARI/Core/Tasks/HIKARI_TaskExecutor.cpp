#include "Core/Tasks/HIKARI_TaskExecutor.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace HIKARI::TASKS {
namespace {

thread_local const void* gTaskExecutorWorker = nullptr;

uint32_t ResolveWorkerCount(uint32_t requested) noexcept {
    if (requested != 0u) {
        return (std::clamp)(requested, 1u, 8u);
    }
    const uint32_t hardware = std::thread::hardware_concurrency();
    if (hardware <= 2u) {
        return 1u;
    }
    return (std::clamp)(hardware - 1u, 1u, 4u);
}

} // namespace

struct TaskExecutor::Impl {
    explicit Impl(uint32_t requestedWorkerCount) : workerCount(ResolveWorkerCount(requestedWorkerCount)) {
        workers.reserve(workerCount);
        try {
            for (uint32_t index = 0u; index < workerCount; ++index) {
                workers.emplace_back([this]() { WorkerMain(); });
            }
        } catch (...) {
            {
                std::lock_guard lock(mutex);
                stopping = true;
            }
            wake.notify_all();
            for (std::thread& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
            throw;
        }
    }

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void ExecuteBatch() noexcept {
        for (;;) {
            const size_t index = batchNext.fetch_add(1u, std::memory_order_relaxed);
            if (index >= batchCount) {
                return;
            }
            const TaskExecutorWork& item = batchWork[index];
            item.execute(item.context);
        }
    }

    void WorkerMain() {
        gTaskExecutorWorker = this;
        uint64_t observedBatchGeneration = 0u;
        for (;;) {
            std::function<void()> async{};
            bool runBatch = false;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&]() {
                    return stopping || !asyncWork.empty() ||
                           (batchRunning && batchGeneration != observedBatchGeneration);
                });
                if (batchRunning && batchGeneration != observedBatchGeneration) {
                    observedBatchGeneration = batchGeneration;
                    runBatch = true;
                } else if (!asyncWork.empty()) {
                    async = std::move(asyncWork.front());
                    asyncWork.pop_front();
                    ++activeAsyncCount;
                } else if (stopping) {
                    gTaskExecutorWorker = nullptr;
                    return;
                }
            }

            if (runBatch) {
                ExecuteBatch();
                std::lock_guard lock(mutex);
                if (batchWorkersRemaining != 0u) {
                    --batchWorkersRemaining;
                }
                if (batchWorkersRemaining == 0u) {
                    idle.notify_all();
                }
                continue;
            }

            try {
                if (async) {
                    async();
                }
            } catch (...) {
                // Domain facades own task outcome reporting. The executor
                // only guarantees that one bad task cannot kill a worker.
            }
            {
                std::lock_guard lock(mutex);
                if (activeAsyncCount != 0u) {
                    --activeAsyncCount;
                }
                if (asyncWork.empty() && activeAsyncCount == 0u && !batchRunning) {
                    idle.notify_all();
                }
            }
        }
    }

    uint32_t workerCount = 0u;
    std::vector<std::thread> workers{};
    std::mutex mutex{};
    std::condition_variable wake{};
    std::condition_variable idle{};
    std::deque<std::function<void()>> asyncWork{};
    size_t activeAsyncCount = 0u;
    const TaskExecutorWork* batchWork = nullptr;
    size_t batchCount = 0u;
    std::atomic_size_t batchNext{0u};
    uint64_t batchGeneration = 0u;
    size_t batchWorkersRemaining = 0u;
    bool batchRunning = false;
    bool stopping = false;
};

TaskExecutor::TaskExecutor(uint32_t workerCount) : impl_(std::make_unique<Impl>(workerCount)) {}

TaskExecutor::~TaskExecutor() = default;

bool TaskExecutor::Submit(std::function<void()> work) {
    if (!impl_ || !work) {
        return false;
    }
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopping) {
            return false;
        }
        impl_->asyncWork.push_back(std::move(work));
    }
    impl_->wake.notify_one();
    return true;
}

bool TaskExecutor::RunBatch(std::span<const TaskExecutorWork> work) {
    if (!impl_ || gTaskExecutorWorker == impl_.get()) {
        return false;
    }
    for (const TaskExecutorWork& item : work) {
        if (!item.IsValid()) {
            return false;
        }
    }
    if (work.empty()) {
        return true;
    }
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopping) {
            return false;
        }
    }
    if (impl_->workers.empty() || work.size() == 1u) {
        for (const TaskExecutorWork& item : work) {
            item.execute(item.context);
        }
        return true;
    }

    {
        std::unique_lock lock(impl_->mutex);
        impl_->idle.wait(lock, [&]() { return !impl_->batchRunning; });
        if (impl_->stopping) {
            return false;
        }
        impl_->batchWork = work.data();
        impl_->batchCount = work.size();
        impl_->batchNext.store(0u, std::memory_order_relaxed);
        impl_->batchWorkersRemaining = impl_->workers.size();
        impl_->batchRunning = true;
        ++impl_->batchGeneration;
        if (impl_->batchGeneration == 0u) {
            ++impl_->batchGeneration;
        }
    }
    impl_->wake.notify_all();
    impl_->ExecuteBatch();
    {
        std::unique_lock lock(impl_->mutex);
        impl_->idle.wait(lock, [&]() { return impl_->batchWorkersRemaining == 0u; });
        impl_->batchRunning = false;
        impl_->batchWork = nullptr;
        impl_->batchCount = 0u;
        impl_->idle.notify_all();
    }
    return true;
}

void TaskExecutor::StopAccepting() noexcept {
    if (!impl_) {
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->wake.notify_all();
}

bool TaskExecutor::WaitIdle() {
    if (!impl_) {
        return true;
    }
    if (gTaskExecutorWorker == impl_.get()) {
        return false;
    }
    std::unique_lock lock(impl_->mutex);
    impl_->idle.wait(
        lock, [&]() { return impl_->asyncWork.empty() && impl_->activeAsyncCount == 0u && !impl_->batchRunning; });
    return true;
}

uint32_t TaskExecutor::GetWorkerCount() const noexcept { return impl_ ? impl_->workerCount : 0u; }

} // namespace HIKARI::TASKS
