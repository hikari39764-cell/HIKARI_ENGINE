#include "Render3D/Surface/HIKARI_RenderFrameSurfaceRegistry.h"

#include <array>
#include <mutex>
#include <thread>

#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"

namespace HIKARI::RENDER3D::SURFACE {
namespace {
struct RegistrySlot {
    SceneSurfaceSet surfaces{};
    RenderFrameSurfaceReceipt receipt{};
    bool active = false;
};

struct RegistryState {
    std::array<RegistrySlot, kRenderFrameSurfaceMaximumResidentViewCount> slots{};
    std::thread::id ownerThread{};
    uint64_t nextGeneration = 1u;
    std::mutex mutex{};
};

RegistryState& State() {
    static RegistryState state{};
    return state;
}

bool CheckOwnerThread(RegistryState& state, bool assignIfNeeded) noexcept {
    const std::thread::id current = std::this_thread::get_id();
    if (assignIfNeeded && state.ownerThread == std::thread::id{}) {
        state.ownerThread = current;
    }
    return state.ownerThread != std::thread::id{} && state.ownerThread == current;
}

uint64_t AllocateGeneration(RegistryState& state) noexcept {
    uint64_t generation = state.nextGeneration++;
    if (generation == 0u) {
        generation = state.nextGeneration++;
    }
    return generation;
}

RegistrySlot* FindSlot(RegistryState& state, RenderFrameSurfaceReceipt receipt) noexcept {
    if (!receipt.IsValid()) {
        return nullptr;
    }
    for (RegistrySlot& slot : state.slots) {
        if (slot.active && slot.receipt.Matches(receipt)) {
            return &slot;
        }
    }
    return nullptr;
}

bool CloseSlot(RegistryState& state, RenderFrameSurfaceReceipt receipt) noexcept {
    RegistrySlot* slot = FindSlot(state, receipt);
    if (slot == nullptr) {
        return false;
    }
    slot->surfaces.Reset({}, 0u);
    slot->active = false;
    slot->receipt = {};
    return true;
}
} // namespace

bool BeginRenderFrameSurfaces(RenderFrameToken frame, uint64_t deviceGeneration,
                              RenderFrameSurfaceReceipt& outReceipt) noexcept {
    outReceipt = {};
    if (!frame.IsValid() || deviceGeneration == 0u) {
        return false;
    }
    RegistryState& state = State();
    const std::lock_guard lock(state.mutex);
    if (!CheckOwnerThread(state, true)) {
        return false;
    }

    RegistrySlot* selected = nullptr;
    for (RegistrySlot& slot : state.slots) {
        if (slot.active && slot.receipt.frame.viewId == frame.viewId) {
            if (slot.receipt.frame.Matches(frame) && slot.receipt.deviceGeneration == deviceGeneration) {
                outReceipt = slot.receipt;
                return true;
            }
            return false;
        }
        if (selected == nullptr && !slot.active) {
            selected = &slot;
        }
    }
    if (selected == nullptr) {
        return false;
    }

    const RenderFrameSurfaceReceipt receipt{frame, deviceGeneration, AllocateGeneration(state)};
    selected->surfaces.Reset(frame, deviceGeneration);
    selected->receipt = receipt;
    selected->active = true;
    outReceipt = receipt;
    return true;
}

SceneSurfaceSet* TryGetRenderFrameSurfaces(RenderFrameSurfaceReceipt receipt) noexcept {
    RegistryState& state = State();
    const std::lock_guard lock(state.mutex);
    if (!CheckOwnerThread(state, false)) {
        return nullptr;
    }
    RegistrySlot* slot = FindSlot(state, receipt);
    return slot != nullptr && slot->surfaces.MatchesFrame(receipt.frame) &&
                   slot->surfaces.GetDeviceGeneration() == receipt.deviceGeneration
               ? &slot->surfaces
               : nullptr;
}

bool EndRenderFrameSurfaces(RenderFrameSurfaceReceipt receipt) noexcept {
    RegistryState& state = State();
    const std::lock_guard lock(state.mutex);
    return CheckOwnerThread(state, false) && CloseSlot(state, receipt);
}

bool AbortRenderFrameSurfaces(RenderFrameSurfaceReceipt receipt) noexcept {
    RegistryState& state = State();
    const std::lock_guard lock(state.mutex);
    return CheckOwnerThread(state, false) && CloseSlot(state, receipt);
}

bool ShutdownRenderFrameSurfaceRegistry() noexcept {
    RegistryState& state = State();
    const std::lock_guard lock(state.mutex);
    if (state.ownerThread == std::thread::id{}) {
        return true;
    }
    if (!CheckOwnerThread(state, false)) {
        return false;
    }
    for (RegistrySlot& slot : state.slots) {
        slot.surfaces.Reset({}, 0u);
        slot.active = false;
        slot.receipt = {};
    }
    state.ownerThread = {};
    return true;
}

} // namespace HIKARI::RENDER3D::SURFACE
