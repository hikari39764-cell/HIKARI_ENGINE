#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Scene/HIKARI_ISystem.h"
namespace HIKARI {

class World;
struct FrameContext;
enum class WorldCommandPhase : uint8_t;

class SystemScheduler {
public:
    void Clear();

    bool AddSystem(std::string systemId, int executionOrder, std::unique_ptr<ISystem> system);
    bool HasSystem(std::string_view systemId) const;
    size_t GetSystemCount() const;
    std::vector<std::string> GetExecutionOrder() const;

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
    using FrameCallback = void (ISystem::*)(
        World&,
        const FrameContext&);

    void ExecuteFramePhase(
        World& world,
        const FrameContext& frame,
        WorldCommandPhase phase,
        FrameCallback callback,
        bool flushAfterEach);

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
    uint64_t nextInsertionIndex_ = 0;
};

} // namespace HIKARI
