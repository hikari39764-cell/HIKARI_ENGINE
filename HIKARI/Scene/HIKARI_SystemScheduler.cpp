#include "Scene/HIKARI_SystemScheduler.h"

#include <algorithm>
#include <utility>

#include "HIKARI/Project/FrameContext.h"
#include "HIKARI/Project/MassSystem.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

namespace {

bool MatchesMassSystemPhase(GameProjectMassSystemPhase schedulePhase, WorldCommandPhase phase) noexcept {
    switch (schedulePhase) {
    case GameProjectMassSystemPhase::PreUpdate: return phase == WorldCommandPhase::SystemPreUpdate;
    case GameProjectMassSystemPhase::PreFixedUpdate: return phase == WorldCommandPhase::SystemPreFixedUpdate;
    case GameProjectMassSystemPhase::FixedUpdate: return phase == WorldCommandPhase::SystemFixedUpdate;
    case GameProjectMassSystemPhase::PostFixedUpdate: return phase == WorldCommandPhase::SystemPostFixedUpdate;
    case GameProjectMassSystemPhase::Update: return phase == WorldCommandPhase::SystemUpdate;
    case GameProjectMassSystemPhase::LateUpdate: return phase == WorldCommandPhase::SystemLateUpdate;
    default: return false;
    }
}

} // namespace

bool SystemScheduler::IsOwnerThread() const noexcept { return std::this_thread::get_id() == ownerThread_; }

bool SystemScheduler::AddSystem(std::string systemId, int executionOrder, std::unique_ptr<ISystem> system) {
    if (!IsOwnerThread() || systemId.empty() || !system || HasSystem(systemId)) {
        return false;
    }

    const std::string frameTimingName = "System/" + systemId + "/Frame";
    const std::string fixedTimingName = "System/" + systemId + "/Fixed";

    systems_.push_back(SystemEntry{std::move(systemId), executionOrder, nextInsertionIndex_++, std::move(system),
                                   CPU_PROFILE::RegisterDynamicPass(frameTimingName),
                                   CPU_PROFILE::RegisterDynamicPass(fixedTimingName)});
    std::stable_sort(systems_.begin(), systems_.end(), [](const SystemEntry& lhs, const SystemEntry& rhs) {
        if (lhs.executionOrder != rhs.executionOrder) {
            return lhs.executionOrder < rhs.executionOrder;
        }
        return lhs.insertionIndex < rhs.insertionIndex;
    });
    return true;
}

bool SystemScheduler::HasSystem(std::string_view systemId) const {
    if (!IsOwnerThread()) {
        return false;
    }
    return std::any_of(systems_.begin(), systems_.end(),
                       [systemId](const SystemEntry& entry) { return entry.systemId == systemId; });
}

std::vector<std::string> SystemScheduler::GetExecutionOrder() const {
    std::vector<std::string> order;
    if (!IsOwnerThread()) {
        return order;
    }
    order.reserve(systems_.size());
    for (const SystemEntry& entry : systems_) {
        order.push_back(entry.systemId);
    }
    return order;
}

void SystemScheduler::ExecuteFramePhase(World& world, const FrameContext& frame, WorldCommandPhase phase,
                                        FrameCallback callback, bool flushAfterEach) {
    if (!IsOwnerThread() || !world.IsOwnerThread()) {
        return;
    }
    const bool fixedPhase = phase == WorldCommandPhase::SystemPreFixedUpdate ||
                            phase == WorldCommandPhase::SystemFixedUpdate ||
                            phase == WorldCommandPhase::SystemPostFixedUpdate;
    for (const SystemEntry& entry : systems_) {
        CPU_PROFILE::ScopedDynamicCpuTimer timer(fixedPhase ? entry.fixedTiming : entry.frameTiming);
        const auto* massSystem = dynamic_cast<const IMassWorkerSystem*>(entry.system.get());
        if (massSystem != nullptr && massSystem->GetMassSystemSchedule().access.typeId.IsValid() &&
            MatchesMassSystemPhase(massSystem->GetMassSystemSchedule().phase, phase)) {
            bool completed = false;
            try {
                completed = ExecuteParallelMassSystem(world, frame, entry, *massSystem);
            } catch (...) {
                ++parallelMassFailureCount_;
                DiscardParallelSystemScratch(entry.system.get());
                world.RecordParallelMassSystemFailure(phase);
                if (flushAfterEach) {
                    (void)world.FlushQueuedCommands(phase);
                }
                throw;
            }
            if (flushAfterEach) {
                (void)world.FlushQueuedCommands(phase);
            }
            if (!completed) {
                ++parallelMassFailureCount_;
                DiscardParallelSystemScratch(entry.system.get());
                world.RecordParallelMassSystemFailure(phase);
                // Do not let later systems observe a partially failed Mass
                // phase. Earlier systems remain committed at their barriers.
                return;
            }
            continue;
        }
        try {
            const World::StructureReadScope structureRead = world.AcquireStructureReadScope();
            (entry.system.get()->*callback)(world, frame);
        } catch (...) {
            (void)world.FlushQueuedCommands(phase);
            throw;
        }
        if (flushAfterEach) {
            (void)world.FlushQueuedCommands(phase);
        }
    }
}

void SystemScheduler::AttachWorld(World& world) {
    if (!IsOwnerThread() || !world.IsOwnerThread()) {
        return;
    }
    const uint64_t worldIdentity = world.GetRuntimeIdentity();
    if (worldIdentity == 0u || (attachedWorldIdentity_ != 0u && attachedWorldIdentity_ != worldIdentity)) {
        return;
    }
    attachedWorldIdentity_ = worldIdentity;
    for (const SystemEntry& entry : systems_) {
        try {
            const World::StructureReadScope structureRead = world.AcquireStructureReadScope();
            entry.system->OnWorldAttached(world);
        } catch (...) {
            (void)world.FlushQueuedCommands(WorldCommandPhase::SystemAttach);
            throw;
        }
        (void)world.FlushQueuedCommands(WorldCommandPhase::SystemAttach);
    }
}

void SystemScheduler::DetachWorld(World& world) {
    if (!IsOwnerThread() || !world.IsOwnerThread()) {
        return;
    }
    if (attachedWorldIdentity_ != world.GetRuntimeIdentity()) {
        return;
    }
    ReleaseParallelRuntime();
    for (const SystemEntry& entry : systems_) {
        try {
            const World::StructureReadScope structureRead = world.AcquireStructureReadScope();
            entry.system->OnWorldDetached(world);
        } catch (...) {
            (void)world.FlushQueuedCommands(WorldCommandPhase::SystemDetach);
            throw;
        }
        (void)world.FlushQueuedCommands(WorldCommandPhase::SystemDetach);
    }
    attachedWorldIdentity_ = 0u;
}

void SystemScheduler::PreUpdate(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemPreUpdate, &ISystem::PreUpdate, true);
}

void SystemScheduler::PreFixedUpdate(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemPreFixedUpdate, &ISystem::PreFixedUpdate, true);
}

void SystemScheduler::FixedUpdate(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemFixedUpdate, &ISystem::FixedUpdate, true);
}

void SystemScheduler::PostFixedUpdate(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemPostFixedUpdate, &ISystem::PostFixedUpdate, true);
}

void SystemScheduler::Update(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemUpdate, &ISystem::Update, true);
}

void SystemScheduler::LateUpdate(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemLateUpdate, &ISystem::LateUpdate, true);
}

void SystemScheduler::PreRender(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemPreRender, &ISystem::PreRender, true);
}

void SystemScheduler::Render(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemRender, &ISystem::Render, true);
}

void SystemScheduler::PostRender(World& world, const FrameContext& frame) {
    ExecuteFramePhase(world, frame, WorldCommandPhase::SystemPostRender, &ISystem::PostRender, true);
    (void)world.FlushQueuedCommands(WorldCommandPhase::SystemPostRender);
}

} // namespace HIKARI
