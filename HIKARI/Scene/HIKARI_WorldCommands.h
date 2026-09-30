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
#include "Scene/HIKARI_WorldMassStore.h"

namespace HIKARI {

class ComponentRegistry;

inline constexpr size_t kWorldCommandObjectNameCapacity = 128u;
inline constexpr size_t kWorldCommandInitialCapacity = 128u;
inline constexpr size_t kWorldCommandMaximumCapacity = 4096u;
inline constexpr size_t kWorldCommandFailureCapacity = 256u;
inline constexpr size_t kWorldSpawnBatchMaximumCapacity = 256u;
inline constexpr size_t kWorldMassSpawnBatchMaximumCapacity = 1024u;
inline constexpr size_t kWorldMassMaximumBatchPayloadBytes = 1024u * 1024u;
inline constexpr size_t kWorldSpawnMaximumComponentsPerObject = 16u;
inline constexpr size_t kWorldSpawnMaximumComponentPayloadBytes = 16u * 1024u;
inline constexpr size_t kWorldSpawnMaximumBatchPayloadBytes = 1024u * 1024u;

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

enum class WorldParentMode : uint8_t { KeepLocal, KeepWorld };

enum class WorldCommandKind : uint8_t {
    Spawn,
    Destroy,
    SetParent,
    SpawnMass,
    DestroyMass,
    ParallelMassSystem,
    PrefabInstantiate,
    PrefabDestroyInstance,
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
    WorldMutationResult result = WorldMutationResult::InternalError;
    RuntimeObjectHandle object{};
    uint32_t token = 0u;
};

struct WorldCommandFailure {
    uint32_t token = 0u;
    WorldCommandKind kind = WorldCommandKind::Spawn;
    WorldMutationResult result = WorldMutationResult::InternalError;
    RuntimeObjectHandle object{};
    RuntimeObjectHandle parent{};
    WorldCommandPhase phase = WorldCommandPhase::Unknown;
};

struct WorldCommandLocalTransform {
    MATH::Vec3 position{0.0f, 0.0f, 0.0f};
    MATH::Quat rotation = MATH::Quat::Identity();
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
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

enum class WorldCommandPayloadKind : uint8_t {
    Spawn,
    SpawnMass,
    DestroyMass,
    PrefabInstantiate,
    PrefabDestroyInstance,
};

struct WorldCommandPayload {
    explicit WorldCommandPayload(WorldCommandPayloadKind value) noexcept : kind(value) {}

    WorldCommandPayloadKind kind;
};

struct WorldSpawnBatchPayload final : WorldCommandPayload {
    WorldSpawnBatchPayload() noexcept : WorldCommandPayload(WorldCommandPayloadKind::Spawn) {}
    std::vector<WorldSpawnObjectPayload> objects{};
    bool allowEngineComponents = false;
};

struct WorldPrefabSpawnObjectPayload {
    uint64_t prefabLocalObjectId = 0u;
    uint64_t sceneObjectId = 0u;
    RuntimeObjectHandle object{};
    RuntimeObjectHandle parent{};
    WorldCommandLocalTransform transform{};
    std::string name{};
};

struct WorldPrefabInstantiatePayload final : WorldCommandPayload {
    WorldPrefabInstantiatePayload() noexcept : WorldCommandPayload(WorldCommandPayloadKind::PrefabInstantiate) {}
    uint64_t requestId = 0u;
    uint64_t instanceId = 0u;
    std::shared_ptr<WorldSpawnBatchPayload> components{};
    std::vector<WorldPrefabSpawnObjectPayload> objects{};
};

struct WorldPrefabDestroyInstancePayload final : WorldCommandPayload {
    WorldPrefabDestroyInstancePayload() noexcept
        : WorldCommandPayload(WorldCommandPayloadKind::PrefabDestroyInstance) {}
    uint64_t instanceId = 0u;
    std::vector<RuntimeObjectHandle> objects{};
};

struct WorldMassSpawnBatchPayload final : WorldCommandPayload {
    WorldMassSpawnBatchPayload() noexcept : WorldCommandPayload(WorldCommandPayloadKind::SpawnMass) {}
    WorldMassSchema schema{};
    std::vector<RuntimeObjectHandle> objects{};
    std::vector<GameProjectTransform> transforms{};
    std::vector<GameProjectFloat3> velocities{};
    std::vector<std::byte> podBytes{};
    size_t podStride = 0u;
};

struct WorldMassDestroyBatchPayload final : WorldCommandPayload {
    WorldMassDestroyBatchPayload() noexcept : WorldCommandPayload(WorldCommandPayloadKind::DestroyMass) {}
    std::vector<RuntimeObjectHandle> objects{};
};

namespace INTERNAL {
WorldMutationResult BuildWorldSpawnBatchPayload(const ComponentRegistry* registry,
                                                std::span<const WorldSpawnRequest> requests,
                                                std::shared_ptr<WorldSpawnBatchPayload>& outPayload,
                                                bool allowEngineComponents = false) noexcept;
WorldMutationResult BuildWorldMassSpawnBatchPayload(const WorldMassSpawnBatchRequest& request,
                                                    std::shared_ptr<WorldMassSpawnBatchPayload>& outPayload) noexcept;
} // namespace INTERNAL

struct PendingWorldCommand {
    uint64_t sequence = 0u;
    uint32_t token = 0u;
    WorldCommandKind kind = WorldCommandKind::Spawn;
    RuntimeObjectHandle object{};
    RuntimeObjectHandle parent{};
    WorldParentMode parentMode = WorldParentMode::KeepLocal;
    WorldCommandLocalTransform transform{};
    std::array<char, kWorldCommandObjectNameCapacity> name{};
    std::shared_ptr<const WorldCommandPayload> payload{};
    uint32_t payloadIndex = 0u;
    uint64_t documentId = 0u;
};

static_assert(sizeof(WorldCommandLocalTransform) == 40u);
static_assert(sizeof(PendingWorldCommand) <= 256u);

} // namespace HIKARI
