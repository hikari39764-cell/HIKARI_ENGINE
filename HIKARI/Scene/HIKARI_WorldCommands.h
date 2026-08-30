#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "HIKARI/Runtime/ComponentTypeId.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI {

    class ComponentRegistry;

    inline constexpr size_t kWorldCommandObjectNameCapacity = 128u;
    inline constexpr size_t kWorldCommandInitialCapacity = 128u;
    inline constexpr size_t kWorldCommandMaximumCapacity = 4096u;
    inline constexpr size_t kWorldCommandFailureCapacity = 256u;
    inline constexpr size_t kWorldSpawnBatchMaximumCapacity = 256u;
    inline constexpr size_t
        kWorldSpawnMaximumComponentsPerObject = 16u;
    inline constexpr size_t
        kWorldSpawnMaximumComponentPayloadBytes = 16u * 1024u;
    inline constexpr size_t
        kWorldSpawnMaximumBatchPayloadBytes = 1024u * 1024u;

    enum class WorldMutationResult : uint8_t {
        Success,
        Queued,
        WrongThread,
        MutationDisallowed,
        CommandQueueFull,
        WorldClosing,
        CommandCanceled,
        InvalidName,
        InvalidObjectHandle,
        InvalidParentHandle,
        ParentCycle,
        InvalidParentMode,
        TransformNotInvertible,
        TransformDecompositionFailed,
        HierarchyRejected,
        InvalidComponentType,
        InvalidComponentData,
        ComponentInitializationFailed,
        InternalError
    };

    enum class WorldParentMode : uint8_t {
        KeepLocal,
        KeepWorld
    };

    enum class WorldCommandKind : uint8_t {
        Spawn,
        Destroy,
        SetParent
    };

    enum class WorldCommandPhase : uint8_t {
        Unknown,
        BeginFrame,
        ComponentUpdateBegin,
        ComponentUpdateEnd,
        QueryEnd,
        WorldClear,
        SystemAttach,
        SystemDetach,
        SystemPreUpdate,
        SystemPreFixedUpdate,
        SystemFixedUpdate,
        SystemPostFixedUpdate,
        SystemUpdate,
        SystemLateUpdate,
        SystemPreRender,
        SystemRender,
        SystemPostRender
    };

    struct WorldCommandSubmission {
        WorldMutationResult result =
            WorldMutationResult::InternalError;
        RuntimeObjectHandle object{};
        uint32_t token = 0u;
    };

    struct WorldCommandFailure {
        uint32_t token = 0u;
        WorldCommandKind kind = WorldCommandKind::Spawn;
        WorldMutationResult result =
            WorldMutationResult::InternalError;
        RuntimeObjectHandle object{};
        RuntimeObjectHandle parent{};
        WorldCommandPhase phase = WorldCommandPhase::Unknown;
    };

    struct WorldCommandLocalTransform {
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Quat rotation = MATH::Quat::Identity();
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
    };

    struct WorldSpawnComponentRequest {
        ComponentTypeId typeId{};
        std::string_view initialValuesJson{};
    };

    struct WorldSpawnRequest {
        std::string_view name{};
        WorldCommandLocalTransform transform{};
        RuntimeObjectHandle parent{};
        std::span<const WorldSpawnComponentRequest> components{};
    };

    struct WorldSpawnComponentPayload {
        ComponentTypeId typeId{};
        std::string initialValuesJson{};
    };

    struct WorldSpawnObjectPayload {
        std::vector<WorldSpawnComponentPayload> components{};
    };

    struct WorldSpawnBatchPayload {
        std::vector<WorldSpawnObjectPayload> objects{};
    };

    namespace INTERNAL {
        WorldMutationResult BuildWorldSpawnBatchPayload(
            const ComponentRegistry* registry,
            std::span<const WorldSpawnRequest> requests,
            std::shared_ptr<WorldSpawnBatchPayload>& outPayload) noexcept;
    }

    struct PendingWorldCommand {
        uint64_t sequence = 0u;
        uint32_t token = 0u;
        WorldCommandKind kind = WorldCommandKind::Spawn;
        RuntimeObjectHandle object{};
        RuntimeObjectHandle parent{};
        WorldParentMode parentMode = WorldParentMode::KeepLocal;
        WorldCommandLocalTransform transform{};
        std::array<char, kWorldCommandObjectNameCapacity> name{};
        std::shared_ptr<const WorldSpawnBatchPayload> spawnPayload{};
        uint32_t spawnPayloadIndex = 0u;
    };

    static_assert(sizeof(WorldCommandLocalTransform) == 40u);
    static_assert(sizeof(PendingWorldCommand) <= 256u);

} // namespace HIKARI
