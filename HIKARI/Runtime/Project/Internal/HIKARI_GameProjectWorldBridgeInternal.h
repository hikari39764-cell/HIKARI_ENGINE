#pragma once

#include <cstdint>

#include "HIKARI/Project/WorldTypes.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"

namespace HIKARI::PROJECT::INTERNAL {

// Callback-scoped service Views may only access the active World on its
// owner thread. Persistent object references use generation handles instead.
inline World* CurrentWorld(void* value) noexcept {
    auto* world = static_cast<World*>(value);
    return world != nullptr && RuntimeSceneContext::GetCurrentWorld() == world && world->IsOwnerThread() ? world
                                                                                                         : nullptr;
}

template <class WorldType>
auto* FindObject(WorldType* world, uint64_t value) noexcept {
    const auto handle = RuntimeObjectHandle::FromValue(value);
    return world != nullptr && handle.IsValid() ? world->FindObject(handle) : nullptr;
}

inline RuntimeObjectHandle DecodeRuntimeHandle(GameProjectObjectHandle handle) noexcept {
    return handle.IsValid() ? RuntimeObjectHandle::FromValue(handle.value) : RuntimeObjectHandle{};
}

inline GameProjectObjectHandle EncodeRuntimeHandle(const World& world, RuntimeObjectHandle handle) noexcept {
    return handle.IsValid()
               ? GameProjectObjectHandle{GameProjectWorldHandle{world.GetRuntimeIdentity()}, handle.ToValue()}
               : GameProjectObjectHandle{};
}

inline GameProjectWorldResult MakeWorldResult(GameProjectWorldResultCode code,
                                              GameProjectObjectHandle object = {}) noexcept {
    return GameProjectWorldResult{code, object, 0u};
}

inline GameProjectWorldResult ToProjectMutationResult(const World& world, WorldMutationResult result,
                                                      RuntimeObjectHandle object = {}) noexcept {
    switch (result) {
    case WorldMutationResult::Success:
        return MakeWorldResult(GameProjectWorldResultCode::Success, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::Queued:
        return MakeWorldResult(GameProjectWorldResultCode::Queued, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::WrongThread:
        return MakeWorldResult(GameProjectWorldResultCode::WrongThread, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::MutationDisallowed:
        return MakeWorldResult(GameProjectWorldResultCode::MutationRejected, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::CommandQueueFull:
        return MakeWorldResult(GameProjectWorldResultCode::CommandQueueFull, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::WorldClosing:
        return MakeWorldResult(GameProjectWorldResultCode::WorldClosing, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::CommandCanceled:
        return MakeWorldResult(GameProjectWorldResultCode::CommandCanceled, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::InvalidName:
        return MakeWorldResult(GameProjectWorldResultCode::InvalidName, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::InvalidObjectHandle: return MakeWorldResult(GameProjectWorldResultCode::InvalidObject);
    case WorldMutationResult::InvalidParentHandle:
        return MakeWorldResult(GameProjectWorldResultCode::InvalidParent, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::ParentCycle:
        return MakeWorldResult(GameProjectWorldResultCode::HierarchyCycle, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::InvalidParentMode:
        return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::TransformNotInvertible:
    case WorldMutationResult::TransformDecompositionFailed:
    case WorldMutationResult::HierarchyRejected:
        return MakeWorldResult(GameProjectWorldResultCode::MutationRejected, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::InvalidComponentType:
        return MakeWorldResult(GameProjectWorldResultCode::Unsupported, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::InvalidComponentData:
        return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::ComponentInitializationFailed:
        return MakeWorldResult(GameProjectWorldResultCode::MutationRejected, EncodeRuntimeHandle(world, object));
    case WorldMutationResult::InternalError:
    default: return MakeWorldResult(GameProjectWorldResultCode::InternalError, EncodeRuntimeHandle(world, object));
    }
}

inline GameProjectWorldResult ToProjectCommandSubmission(const World& world,
                                                         const WorldCommandSubmission& submission) noexcept {
    GameProjectWorldResult result = ToProjectMutationResult(world, submission.result, submission.object);
    result.reserved = submission.token;
    return result;
}

inline GameProjectWorldResult MakeWorldAccessFailure(RuntimeWorldAccessResult access) noexcept {
    return MakeWorldResult(access == RuntimeWorldAccessResult::WrongThread ? GameProjectWorldResultCode::WrongThread
                                                                           : GameProjectWorldResultCode::InvalidWorld);
}

// span の要素数を検査用の 32bit 値へ。上限を超える値は各 API の件数上限で拒否される。
inline uint32_t SpanCount(size_t value) noexcept {
    return value > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(value);
}

inline bool IsObjectHandleForWorld(GameProjectObjectHandle handle, const World& world) noexcept {
    return handle.IsValid() && handle.world.value == world.GetRuntimeIdentity() &&
           DecodeRuntimeHandle(handle).IsValid();
}

} // namespace HIKARI::PROJECT::INTERNAL
