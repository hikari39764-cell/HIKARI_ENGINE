#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace HIKARI::TASKS {

    struct TaskExecutorWork {
        using ExecuteFn = void (*)(void* context) noexcept;

        ExecuteFn execute = nullptr;
        void* context = nullptr;

        bool IsValid() const noexcept { return execute != nullptr; }
    };

    class TaskExecutor final {
    public:
        explicit TaskExecutor(uint32_t workerCount = 0u);
        ~TaskExecutor();

        TaskExecutor(const TaskExecutor&) = delete;
        TaskExecutor& operator=(const TaskExecutor&) = delete;

        bool Submit(std::function<void()> work);

        // RunBatch and WaitIdle are owner-side barriers. Calling either from
        // a callback running on this executor is rejected to avoid self-wait.
        bool RunBatch(std::span<const TaskExecutorWork> work);
        void StopAccepting() noexcept;
        bool WaitIdle();
        uint32_t GetWorkerCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_{};
    };

} // namespace HIKARI::TASKS
