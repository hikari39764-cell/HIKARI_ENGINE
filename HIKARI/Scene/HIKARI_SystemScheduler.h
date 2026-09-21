#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Scene/HIKARI_ISystem.h"
namespace HIKARI {

class World;
struct FrameContext;
enum class WorldCommandPhase : uint8_t;

class SystemScheduler {
public:
    SystemScheduler();
    ~SystemScheduler();

    SystemScheduler(const SystemScheduler&) = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    void Clear();

    bool AddSystem(std::string systemId, int executionOrder, std::unique_ptr<ISystem> system);
    bool HasSystem(std::string_view systemId) const;
    size_t GetSystemCount() const;
    uint64_t GetParallelMassFailureCount() const noexcept;
    uint64_t GetMassSnapshotReuseCount() const noexcept { return massSnapshotReuseCount_; }
    uint64_t GetRenderObserverFailureCount() const noexcept;
    uint64_t GetRenderPassFailureCount() const noexcept;
    std::vector<std::string> GetExecutionOrder() const;

    bool ObserveRenderFrame(
        const GameProjectRenderFrameView& frame) noexcept;
    bool GetRenderPassCount(uint32_t& outCount) const noexcept;
    bool DescribeRenderPass(
        uint32_t index,
        GameProjectRenderPassDesc& outDescriptor) const noexcept;
    bool ExecuteRenderPass(
        const char* stableId,
        const GameProjectRenderPassContext& context,
        bool* outCallbackEntered = nullptr) noexcept;

    void AttachWorld(World& world);
    void DetachWorld(World& world);

    void PreUpdate(World& world, const FrameContext& frame);
    void PreFixedUpdate(World& world, const FrameContext& frame);
    void FixedUpdate(World& world, const FrameContext& frame);
    void PostFixedUpdate(World& world, const FrameContext& frame);
    void Update(World& world, const FrameContext& frame);
    void LateUpdate(World& world, const FrameContext& frame);

    void PreRender(World& world, const FrameContext& frame);
    void Render(World& world, const FrameContext& frame);
    void PostRender(World& world, const FrameContext& frame);

private:
    struct SystemEntry;

    using FrameCallback = void (ISystem::*)(
        World&,
        const FrameContext&);

    void ExecuteFramePhase(
        World& world,
        const FrameContext& frame,
        WorldCommandPhase phase,
        FrameCallback callback,
        bool flushAfterEach);
    bool IsOwnerThread() const noexcept;
    bool ExecuteParallelMassSystem(
        World& world,
        const FrameContext& frame,
        const SystemEntry& entry,
        const IMassWorkerSystem& system);
    void WaitForParallelIdle() noexcept;
    void ReleaseParallelRuntime() noexcept;
    void DiscardParallelSystemScratch(const ISystem* system) noexcept;

    struct ParallelRuntime;

    struct SystemEntry {
        std::string systemId{};
        int executionOrder = 0;
        uint64_t insertionIndex = 0;
        std::unique_ptr<ISystem> system{};
        CPU_PROFILE::DynamicPassHandle frameTiming =
            CPU_PROFILE::kInvalidDynamicPassHandle;
        CPU_PROFILE::DynamicPassHandle fixedTiming =
            CPU_PROFILE::kInvalidDynamicPassHandle;
    };

    std::vector<SystemEntry> systems_{};
    const std::thread::id ownerThread_{};
    uint64_t attachedWorldIdentity_ = 0u;
    uint64_t nextInsertionIndex_ = 0;
    uint64_t parallelMassFailureCount_ = 0u;
    uint64_t massSnapshotReuseCount_ = 0u;
    uint64_t renderObserverFailureCount_ = 0u;
    uint64_t renderPassFailureCount_ = 0u;
    std::unique_ptr<ParallelRuntime> parallelRuntime_{};
};

} // namespace HIKARI
