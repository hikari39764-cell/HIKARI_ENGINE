#include "Assets/Tasks/HIKARI_AssetTaskService.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace HIKARI {

    namespace {
        using Clock = std::chrono::steady_clock;

        uint32_t ResolveWorkerCount(uint32_t requested) {
            if (requested > 0u) {
                return (std::clamp)(requested, 1u, 8u);
            }
            const uint32_t hardware = std::thread::hardware_concurrency();
            if (hardware <= 2u) {
                return 1u;
            }
            return (std::clamp)(hardware - 1u, 1u, 4u);
        }
    }

    struct AssetTaskService::Impl {
        struct SharedState {
            mutable std::mutex mutex{};
            AssetTaskSnapshot snapshot{};
            Clock::time_point submittedAt = Clock::now();
            Clock::time_point startedAt{};
            Clock::time_point finishedAt{};
            std::atomic_bool cancellationRequested{ false };
        };

        struct QueuedTask {
            std::shared_ptr<SharedState> state{};
            AssetTaskRequest request{};
        };

        struct FinishedTask {
            std::shared_ptr<SharedState> state{};
            AssetTaskRequest request{};
            AssetTaskOutcome workOutcome{};
        };

        explicit Impl(uint32_t requestedWorkerCount)
            : workerCount(ResolveWorkerCount(requestedWorkerCount)) {

            workers.reserve(workerCount);
            for (uint32_t index = 0u; index < workerCount; ++index) {
                workers.emplace_back([this]() {
                    WorkerMain();
                });
            }
        }

        ~Impl() {
            {
                std::lock_guard lock(queueMutex);
                stopping = true;
                for (const QueuedTask& task : queuedTasks) {
                    if (task.state) {
                        task.state->cancellationRequested.store(
                            true,
                            std::memory_order_relaxed);
                    }
                }
                for (const std::shared_ptr<SharedState>& state : states) {
                    if (state) {
                        state->cancellationRequested.store(
                            true,
                            std::memory_order_relaxed);
                    }
                }
            }
            queueCondition.notify_all();
            for (std::thread& worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
        }

        void WorkerMain() {
            for (;;) {
                QueuedTask task{};
                {
                    std::unique_lock lock(queueMutex);
                    queueCondition.wait(lock, [this]() {
                        return stopping || !queuedTasks.empty();
                    });
                    if (stopping && queuedTasks.empty()) {
                        return;
                    }
                    task = std::move(queuedTasks.front());
                    queuedTasks.pop_front();
                }

                const std::shared_ptr<SharedState>& state = task.state;
                if (!state) {
                    continue;
                }

                AssetTaskOutcome outcome{};
                if (state->cancellationRequested.load(
                        std::memory_order_relaxed)) {
                    outcome = AssetTaskOutcome::Canceled(
                        "canceled before the task started");
                } else {
                    {
                        std::lock_guard lock(state->mutex);
                        state->startedAt = Clock::now();
                        state->snapshot.state = AssetTaskState::Running;
                        state->snapshot.progress.stage = "Starting";
                    }

                    std::weak_ptr<SharedState> weakState = state;
                    AssetTaskContext context{
                        [weakState]() {
                            const std::shared_ptr<SharedState> locked =
                                weakState.lock();
                            return !locked ||
                                locked->cancellationRequested.load(
                                    std::memory_order_relaxed);
                        },
                        [weakState](AssetTaskProgress progress) {
                            const std::shared_ptr<SharedState> locked =
                                weakState.lock();
                            if (!locked) {
                                return;
                            }
                            progress.normalized = (std::clamp)(
                                progress.normalized,
                                0.0f,
                                1.0f);
                            std::lock_guard stateLock(locked->mutex);
                            locked->snapshot.progress =
                                std::move(progress);
                        }
                    };

                    try {
                        if (task.request.work) {
                            outcome = task.request.work(context);
                        } else {
                            outcome = AssetTaskOutcome::Failed(
                                "task has no work callback");
                        }
                    } catch (const std::exception& error) {
                        outcome = AssetTaskOutcome::Failed(
                            std::string("task exception: ") + error.what());
                    } catch (...) {
                        outcome = AssetTaskOutcome::Failed(
                            "task exception: unknown");
                    }
                }

                {
                    std::lock_guard lock(state->mutex);
                    state->snapshot.state = AssetTaskState::Finalizing;
                    state->snapshot.progress.stage = "Finalizing";
                    state->snapshot.progress.determinate = false;
                }
                {
                    std::lock_guard lock(completionMutex);
                    finishedTasks.push_back(FinishedTask{
                        state,
                        std::move(task.request),
                        std::move(outcome)
                    });
                }
            }
        }

        uint32_t workerCount = 1u;
        std::atomic<AssetTaskId> nextTaskId{ 1u };
        mutable std::mutex queueMutex{};
        std::condition_variable queueCondition{};
        std::deque<QueuedTask> queuedTasks{};
        std::vector<std::thread> workers{};
        bool stopping = false;

        mutable std::mutex stateListMutex{};
        std::vector<std::shared_ptr<SharedState>> states{};

        mutable std::mutex completionMutex{};
        std::deque<FinishedTask> finishedTasks{};
    };

    AssetTaskOutcome AssetTaskOutcome::Succeeded(std::string message) {
        return AssetTaskOutcome{ true, false, std::move(message) };
    }

    AssetTaskOutcome AssetTaskOutcome::Failed(std::string message) {
        return AssetTaskOutcome{ false, false, std::move(message) };
    }

    AssetTaskOutcome AssetTaskOutcome::Canceled(std::string message) {
        return AssetTaskOutcome{ false, true, std::move(message) };
    }

    AssetTaskContext::AssetTaskContext(
        CancellationQuery cancellationQuery,
        ProgressReporter progressReporter)
        : cancellationQuery_(std::move(cancellationQuery)),
          progressReporter_(std::move(progressReporter)) {
    }

    bool AssetTaskContext::IsCancellationRequested() const {
        return cancellationQuery_ && cancellationQuery_();
    }

    void AssetTaskContext::Report(AssetTaskProgress progress) const {
        if (progressReporter_) {
            progressReporter_(std::move(progress));
        }
    }

    void AssetTaskContext::ReportStage(
        std::string stage,
        float normalized,
        bool determinate,
        std::string currentItem) const {

        AssetTaskProgress progress{};
        progress.stage = std::move(stage);
        progress.currentItem = std::move(currentItem);
        progress.normalized = normalized;
        progress.determinate = determinate;
        Report(std::move(progress));
    }

    AssetTaskService::AssetTaskService(uint32_t workerCount)
        : impl_(std::make_unique<Impl>(workerCount)) {
    }

    AssetTaskService::~AssetTaskService() = default;

    AssetTaskId AssetTaskService::Submit(AssetTaskRequest request) {
        if (!impl_ || !request.work) {
            return 0u;
        }

        const AssetTaskId id = impl_->nextTaskId.fetch_add(
            1u,
            std::memory_order_relaxed);
        auto state = std::make_shared<Impl::SharedState>();
        state->snapshot.id = id;
        state->snapshot.state = AssetTaskState::Queued;
        state->snapshot.category = request.category;
        state->snapshot.label = request.label;
        state->snapshot.progress.stage = "Queued";
        state->snapshot.progress.currentItem = request.initialItem;
        state->snapshot.cancelable = request.cancelable;
        state->submittedAt = Clock::now();

        {
            std::lock_guard lock(impl_->stateListMutex);
            impl_->states.push_back(state);
            if (impl_->states.size() > 256u) {
                std::erase_if(
                    impl_->states,
                    [](const std::shared_ptr<Impl::SharedState>& candidate) {
                        if (!candidate) {
                            return true;
                        }
                        std::lock_guard stateLock(candidate->mutex);
                        return IsTerminal(candidate->snapshot.state);
                    });
            }
        }
        {
            std::lock_guard lock(impl_->queueMutex);
            impl_->queuedTasks.push_back(Impl::QueuedTask{
                state,
                std::move(request)
            });
        }
        impl_->queueCondition.notify_one();
        return id;
    }

    void AssetTaskService::PumpMainThreadCompletions() {
        if (!impl_) {
            return;
        }

        std::deque<Impl::FinishedTask> completed{};
        {
            std::lock_guard lock(impl_->completionMutex);
            completed.swap(impl_->finishedTasks);
        }

        for (Impl::FinishedTask& task : completed) {
            AssetTaskOutcome finalOutcome = task.workOutcome;
            try {
                if (task.request.finalize) {
                    finalOutcome = task.request.finalize(
                        task.workOutcome);
                }
            } catch (const std::exception& error) {
                finalOutcome = AssetTaskOutcome::Failed(
                    std::string("task finalize exception: ") +
                    error.what());
            } catch (...) {
                finalOutcome = AssetTaskOutcome::Failed(
                    "task finalize exception: unknown");
            }

            if (!task.state) {
                continue;
            }
            std::lock_guard lock(task.state->mutex);
            task.state->finishedAt = Clock::now();
            task.state->snapshot.state = finalOutcome.canceled
                ? AssetTaskState::Canceled
                : (finalOutcome.success
                    ? AssetTaskState::Succeeded
                    : AssetTaskState::Failed);
            task.state->snapshot.resultMessage =
                std::move(finalOutcome.message);
            task.state->snapshot.progress.normalized =
                finalOutcome.success ? 1.0f :
                task.state->snapshot.progress.normalized;
            task.state->snapshot.progress.determinate =
                finalOutcome.success;
            task.state->snapshot.progress.stage = finalOutcome.canceled
                ? "Canceled"
                : (finalOutcome.success ? "Complete" : "Failed");
        }
    }

    bool AssetTaskService::RequestCancel(AssetTaskId taskId) {
        if (!impl_ || taskId == 0u) {
            return false;
        }
        std::lock_guard listLock(impl_->stateListMutex);
        for (const std::shared_ptr<Impl::SharedState>& state :
             impl_->states) {
            if (!state) {
                continue;
            }
            std::lock_guard stateLock(state->mutex);
            if (state->snapshot.id != taskId ||
                IsTerminal(state->snapshot.state) ||
                !state->snapshot.cancelable) {
                continue;
            }
            state->cancellationRequested.store(
                true,
                std::memory_order_relaxed);
            state->snapshot.cancellationRequested = true;
            return true;
        }
        return false;
    }

    std::optional<AssetTaskSnapshot> AssetTaskService::FindSnapshot(
        AssetTaskId taskId) const {

        if (!impl_ || taskId == 0u) {
            return std::nullopt;
        }
        std::lock_guard listLock(impl_->stateListMutex);
        for (const std::shared_ptr<Impl::SharedState>& state :
             impl_->states) {
            if (!state) {
                continue;
            }
            std::lock_guard stateLock(state->mutex);
            if (state->snapshot.id != taskId) {
                continue;
            }
            AssetTaskSnapshot snapshot = state->snapshot;
            const Clock::time_point end = IsTerminal(snapshot.state)
                ? state->finishedAt
                : Clock::now();
            const Clock::time_point begin =
                state->startedAt.time_since_epoch().count() != 0
                    ? state->startedAt
                    : state->submittedAt;
            snapshot.elapsedSeconds =
                std::chrono::duration<double>(end - begin).count();
            return snapshot;
        }
        return std::nullopt;
    }

    std::vector<AssetTaskSnapshot>
        AssetTaskService::CollectSnapshots() const {

        std::vector<AssetTaskSnapshot> snapshots{};
        if (!impl_) {
            return snapshots;
        }
        std::lock_guard listLock(impl_->stateListMutex);
        snapshots.reserve(impl_->states.size());
        for (const std::shared_ptr<Impl::SharedState>& state :
             impl_->states) {
            if (!state) {
                continue;
            }
            std::lock_guard stateLock(state->mutex);
            AssetTaskSnapshot snapshot = state->snapshot;
            const Clock::time_point end = IsTerminal(snapshot.state)
                ? state->finishedAt
                : Clock::now();
            const Clock::time_point begin =
                state->startedAt.time_since_epoch().count() != 0
                    ? state->startedAt
                    : state->submittedAt;
            snapshot.elapsedSeconds =
                std::chrono::duration<double>(end - begin).count();
            snapshots.push_back(std::move(snapshot));
        }
        std::sort(
            snapshots.begin(),
            snapshots.end(),
            [](const AssetTaskSnapshot& lhs,
               const AssetTaskSnapshot& rhs) {
                return lhs.id > rhs.id;
            });
        return snapshots;
    }

    void AssetTaskService::ClearCompleted() {
        if (!impl_) {
            return;
        }
        std::lock_guard listLock(impl_->stateListMutex);
        std::erase_if(
            impl_->states,
            [](const std::shared_ptr<Impl::SharedState>& state) {
                if (!state) {
                    return true;
                }
                std::lock_guard stateLock(state->mutex);
                return IsTerminal(state->snapshot.state);
            });
    }

    bool AssetTaskService::HasActiveTasks() const {
        if (!impl_) {
            return false;
        }
        std::lock_guard listLock(impl_->stateListMutex);
        for (const std::shared_ptr<Impl::SharedState>& state :
             impl_->states) {
            if (!state) {
                continue;
            }
            std::lock_guard stateLock(state->mutex);
            if (!IsTerminal(state->snapshot.state)) {
                return true;
            }
        }
        return false;
    }

    uint32_t AssetTaskService::GetWorkerCount() const noexcept {
        return impl_ ? impl_->workerCount : 0u;
    }

    const char* ToString(AssetTaskState state) noexcept {
        switch (state) {
        case AssetTaskState::Queued: return "Queued";
        case AssetTaskState::Running: return "Running";
        case AssetTaskState::Finalizing: return "Finalizing";
        case AssetTaskState::Succeeded: return "Succeeded";
        case AssetTaskState::Failed: return "Failed";
        case AssetTaskState::Canceled: return "Canceled";
        default: return "Unknown";
        }
    }

    bool IsTerminal(AssetTaskState state) noexcept {
        return state == AssetTaskState::Succeeded ||
            state == AssetTaskState::Failed ||
            state == AssetTaskState::Canceled;
    }

} // namespace HIKARI
