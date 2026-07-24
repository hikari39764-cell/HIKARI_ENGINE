#include "Scene/HIKARI_SystemScheduler.h"

#include <algorithm>
#include <utility>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

void SystemScheduler::Clear() {
    systems_.clear();
    nextInsertionIndex_ = 0;
}

bool SystemScheduler::AddSystem(
    std::string systemId,
    int executionOrder,
    std::unique_ptr<ISystem> system) {

    if (systemId.empty() || !system || HasSystem(systemId)) {
        return false;
    }

    systems_.push_back(SystemEntry{
        std::move(systemId),
        executionOrder,
        nextInsertionIndex_++,
        std::move(system)
    });
    std::stable_sort(
        systems_.begin(),
        systems_.end(),
        [](const SystemEntry& lhs, const SystemEntry& rhs) {
            if (lhs.executionOrder != rhs.executionOrder) {
                return lhs.executionOrder < rhs.executionOrder;
            }
            return lhs.insertionIndex < rhs.insertionIndex;
        });
    return true;
}

bool SystemScheduler::HasSystem(std::string_view systemId) const {
    return std::any_of(
        systems_.begin(),
        systems_.end(),
        [systemId](const SystemEntry& entry) {
            return entry.systemId == systemId;
        });
}

size_t SystemScheduler::GetSystemCount() const {
    return systems_.size();
}

std::vector<std::string> SystemScheduler::GetExecutionOrder() const {
    std::vector<std::string> order;
    order.reserve(systems_.size());
    for (const SystemEntry& entry : systems_) {
        order.push_back(entry.systemId);
    }
    return order;
}

void SystemScheduler::AttachWorld(World& world) {
    for (const SystemEntry& entry : systems_) {
        entry.system->OnWorldAttached(world);
    }
}

void SystemScheduler::DetachWorld(World& world) {
    for (const SystemEntry& entry : systems_) {
        entry.system->OnWorldDetached(world);
    }
}

void SystemScheduler::PreUpdate(World& world, const FrameContext& frame) {
    for (const SystemEntry& entry : systems_) {
        entry.system->PreUpdate(world, frame);
    }
}

void SystemScheduler::PreFixedUpdate(
    World& world,
    const FrameContext& frame) {

    for (const SystemEntry& entry : systems_) {
        entry.system->PreFixedUpdate(world, frame);
    }
}

void SystemScheduler::FixedUpdate(
    World& world,
    const FrameContext& frame) {

    for (const SystemEntry& entry : systems_) {
        entry.system->FixedUpdate(world, frame);
    }
}

void SystemScheduler::PostFixedUpdate(
    World& world,
    const FrameContext& frame) {

    for (const SystemEntry& entry : systems_) {
        entry.system->PostFixedUpdate(world, frame);
    }
}

void SystemScheduler::Update(World& world, const FrameContext& frame) {
    for (const SystemEntry& entry : systems_) {
        entry.system->Update(world, frame);
    }
}

void SystemScheduler::LateUpdate(World& world, const FrameContext& frame) {
    for (const SystemEntry& entry : systems_) {
        entry.system->LateUpdate(world, frame);
    }
}

void SystemScheduler::PreRender(World& world, const FrameContext& frame) {
    for (const SystemEntry& entry : systems_) {
        entry.system->PreRender(world, frame);
    }
}

void SystemScheduler::Render(World& world, const FrameContext& frame) {
    for (const SystemEntry& entry : systems_) {
        entry.system->Render(world, frame);
    }
}

void SystemScheduler::PostRender(World& world, const FrameContext& frame) {
    for (const SystemEntry& entry : systems_) {
        entry.system->PostRender(world, frame);
    }
}

} // namespace HIKARI
