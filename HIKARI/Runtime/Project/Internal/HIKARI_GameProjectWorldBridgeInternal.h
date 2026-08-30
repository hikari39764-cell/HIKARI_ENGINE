#pragma once

#include "HIKARI/Project/WorldTypes.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PROJECT::INTERNAL {

    inline RuntimeObjectHandle DecodeRuntimeHandle(
        GameProjectObjectHandle handle) noexcept {
        if (!handle.IsValid()) {
            return {};
        }
        const uint32_t encodedSlot = static_cast<uint32_t>(
            handle.value & 0xFFFFFFFFu);
        if (encodedSlot == 0u) {
            return {};
        }
        return RuntimeObjectHandle{
            encodedSlot - 1u,
            static_cast<uint32_t>(handle.value >> 32u)
        };
    }

    inline GameProjectObjectHandle EncodeRuntimeHandle(
        const World& world,
        RuntimeObjectHandle handle) noexcept {
        return handle.IsValid()
            ? GameProjectObjectHandle{
                GameProjectWorldHandle{
                    world.GetRuntimeIdentity()
                },
                handle.ToValue()
            }
            : GameProjectObjectHandle{};
    }

    inline GameProjectWorldResult MakeWorldResult(
        GameProjectWorldResultCode code,
        GameProjectObjectHandle object = {}) noexcept {
        return GameProjectWorldResult{ code, object, 0u };
    }

    inline GameProjectWorldResult ToProjectMutationResult(
        const World& world,
        WorldMutationResult result,
        RuntimeObjectHandle object = {}) noexcept {
        switch (result) {
        case WorldMutationResult::Success:
            return MakeWorldResult(
                GameProjectWorldResultCode::Success,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::Queued:
            return MakeWorldResult(
                GameProjectWorldResultCode::Queued,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::WrongThread:
            return MakeWorldResult(
                GameProjectWorldResultCode::WrongThread,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::MutationDisallowed:
            return MakeWorldResult(
                GameProjectWorldResultCode::MutationRejected,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::CommandQueueFull:
            return MakeWorldResult(
                GameProjectWorldResultCode::CommandQueueFull,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::WorldClosing:
            return MakeWorldResult(
                GameProjectWorldResultCode::WorldClosing,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::CommandCanceled:
            return MakeWorldResult(
                GameProjectWorldResultCode::CommandCanceled,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::InvalidName:
            return MakeWorldResult(
                GameProjectWorldResultCode::InvalidName,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::InvalidObjectHandle:
            return MakeWorldResult(
                GameProjectWorldResultCode::InvalidObject);
        case WorldMutationResult::InvalidParentHandle:
            return MakeWorldResult(
                GameProjectWorldResultCode::InvalidParent,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::ParentCycle:
            return MakeWorldResult(
                GameProjectWorldResultCode::HierarchyCycle,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::InvalidParentMode:
            return MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::TransformNotInvertible:
        case WorldMutationResult::TransformDecompositionFailed:
        case WorldMutationResult::HierarchyRejected:
            return MakeWorldResult(
                GameProjectWorldResultCode::MutationRejected,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::InvalidComponentType:
            return MakeWorldResult(
                GameProjectWorldResultCode::Unsupported,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::InvalidComponentData:
            return MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::ComponentInitializationFailed:
            return MakeWorldResult(
                GameProjectWorldResultCode::MutationRejected,
                EncodeRuntimeHandle(world, object));
        case WorldMutationResult::InternalError:
        default:
            return MakeWorldResult(
                GameProjectWorldResultCode::InternalError,
                EncodeRuntimeHandle(world, object));
        }
    }

    inline GameProjectWorldResult ToProjectCommandSubmission(
        const World& world,
        const WorldCommandSubmission& submission) noexcept {
        GameProjectWorldResult result = ToProjectMutationResult(
            world,
            submission.result,
            submission.object);
        result.reserved = submission.token;
        return result;
    }

    inline GameProjectWorldResult MakeWorldAccessFailure(
        RuntimeWorldAccessResult access) noexcept {
        return MakeWorldResult(
            access == RuntimeWorldAccessResult::WrongThread
                ? GameProjectWorldResultCode::WrongThread
                : GameProjectWorldResultCode::InvalidWorld);
    }

    inline bool IsObjectHandleForWorld(
        GameProjectObjectHandle handle,
        const World& world) noexcept {
        return handle.IsValid() &&
            handle.world.value == world.GetRuntimeIdentity() &&
            DecodeRuntimeHandle(handle).IsValid();
    }

} // namespace HIKARI::PROJECT::INTERNAL
