#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace HIKARI {

    using AssetTaskId = uint64_t;

    enum class AssetTaskState : uint8_t {
        Queued,
        Running,
        Finalizing,
        Succeeded,
        Failed,
        Canceled,
    };

    struct AssetTaskProgress {
        std::string stage{};
        std::string currentItem{};
        float normalized = 0.0f;
        bool determinate = false;
        uint64_t completedUnits = 0u;
        uint64_t totalUnits = 0u;
        uint64_t processedBytes = 0u;
        uint64_t totalBytes = 0u;
    };

    struct AssetTaskOutcome {
        bool success = false;
        bool canceled = false;
        std::string message{};

        static AssetTaskOutcome Succeeded(std::string message = {});
        static AssetTaskOutcome Failed(std::string message);
        static AssetTaskOutcome Canceled(std::string message = {});
    };

    struct AssetTaskSnapshot {
        AssetTaskId id = 0u;
        AssetTaskState state = AssetTaskState::Queued;
        std::string category{};
        std::string label{};
        AssetTaskProgress progress{};
        std::string resultMessage{};
        bool cancelable = true;
        bool cancellationRequested = false;
        double elapsedSeconds = 0.0;
    };

    class AssetTaskContext {
    public:
        bool IsCancellationRequested() const;
        void Report(AssetTaskProgress progress) const;
        void ReportStage(
            std::string stage,
            float normalized,
            bool determinate = true,
            std::string currentItem = {}) const;

    private:
        friend class AssetTaskService;

        using CancellationQuery = std::function<bool()>;
        using ProgressReporter = std::function<void(AssetTaskProgress)>;

        AssetTaskContext(
            CancellationQuery cancellationQuery,
            ProgressReporter progressReporter);

        CancellationQuery cancellationQuery_{};
        ProgressReporter progressReporter_{};
    };

    struct AssetTaskRequest {
        std::string category{};
        std::string label{};
        std::string initialItem{};
        bool cancelable = true;
        std::function<AssetTaskOutcome(AssetTaskContext&)> work{};
        std::function<AssetTaskOutcome(const AssetTaskOutcome&)> finalize{};
    };

    struct AssetTaskServiceLimits {
        uint32_t maxPendingTasks = 256u;
        uint32_t maxCompletionsPerPump = 8u;
        double maxFinalizeMilliseconds = 2.0;
    };

    class AssetTaskService {
    public:
        explicit AssetTaskService(uint32_t workerCount = 0u,
            AssetTaskServiceLimits limits = {});
        ~AssetTaskService();

        AssetTaskService(const AssetTaskService&) = delete;
        AssetTaskService& operator=(const AssetTaskService&) = delete;

        AssetTaskId Submit(AssetTaskRequest request);
        // 作成スレッドだけで呼ぶ。予算は callback 間で検査し、callback 自体は分断しない。
        void PumpMainThreadCompletions();
        // finalize 開始前まで受け付ける。受理した取消は finalize に Canceled として渡す。
        bool RequestCancel(AssetTaskId taskId);
        std::optional<AssetTaskSnapshot> FindSnapshot(
            AssetTaskId taskId) const;
        std::vector<AssetTaskSnapshot> CollectSnapshots() const;
        void ClearCompleted();
        bool HasActiveTasks() const;
        uint32_t GetWorkerCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_{};
    };

    const char* ToString(AssetTaskState state) noexcept;
    bool IsTerminal(AssetTaskState state) noexcept;

} // namespace HIKARI
