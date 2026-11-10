#include "HIKARI/Project/WorldView.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <array>
#include <cstddef>
#include <limits>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_WorldInstanceStore.h"

namespace HIKARI {
namespace {

static_assert(kWorldMassSpawnBatchMaximumCapacity == kGameProjectWorldMaximumMassSpawnBatchCount);
static_assert(kWorldMassMaximumPodBytes == kGameProjectWorldMaximumMassPodBytes);
static_assert(static_cast<uint8_t>(WorldCommandKind::SpawnMass) ==
              static_cast<uint8_t>(GameProjectWorldCommandKind::SpawnMass));
static_assert(static_cast<uint8_t>(WorldCommandKind::DestroyMass) ==
              static_cast<uint8_t>(GameProjectWorldCommandKind::DestroyMass));
static_assert(static_cast<uint8_t>(WorldCommandKind::ParallelMassSystem) ==
              static_cast<uint8_t>(GameProjectWorldCommandKind::ParallelMassSystem));

} // namespace

bool GameProjectSdkAccess::VisitMassObjects(World& world, ComponentTypeId typeId, void* context,
                                            MassObjectVisitorFn visitor) noexcept {
    return world.VisitMassObjects(typeId, context, visitor);
}

bool WorldView::AddInstanceSet(const GameProjectWorldInstanceSetDesc& desc) const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    WorldInstanceStore* store = world != nullptr ? world->GetInstanceStore() : nullptr;
    try {
        return store != nullptr && store->AddSet(desc);
    } catch (...) {
        return false;
    }
}

bool WorldView::RemoveInstanceSet(ComponentTypeId setId) const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    WorldInstanceStore* store = world != nullptr ? world->GetInstanceStore() : nullptr;
    try {
        return store != nullptr && store->RemoveSet(setId);
    } catch (...) {
        return false;
    }
}

uint32_t WorldView::GetInstanceSetIds(std::span<ComponentTypeId> outIds) const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    const WorldInstanceStore* store = world != nullptr ? world->GetInstanceStore() : nullptr;
    return store != nullptr ? store->CopySetIds(outIds) : 0u;
}

const GameProjectWorldInstanceSetDesc* WorldView::FindInstanceSet(ComponentTypeId setId) const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    const WorldInstanceStore* store = world != nullptr ? world->GetInstanceStore() : nullptr;
    return store != nullptr ? store->FindSet(setId) : nullptr;
}

bool WorldView::ReplaceInstanceCell(ComponentTypeId setId, int32_t cellX, int32_t cellZ,
                                    std::span<const GameProjectWorldInstance> instances) const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    WorldInstanceStore* store = world != nullptr ? world->GetInstanceStore() : nullptr;
    try {
        return store != nullptr && store->ReplaceCell(setId, WorldCellCoord{cellX, cellZ}, instances);
    } catch (...) {
        return false;
    }
}

std::span<const GameProjectWorldInstance> WorldView::ReadInstanceCell(ComponentTypeId setId, int32_t cellX,
                                                                      int32_t cellZ) const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    const WorldInstanceStore* store = world != nullptr ? world->GetInstanceStore() : nullptr;
    return store != nullptr ? store->ReadCell(setId, WorldCellCoord{cellX, cellZ})
                            : std::span<const GameProjectWorldInstance>{};
}

uint64_t WorldView::GetInstanceCount(ComponentTypeId setId) const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    const WorldInstanceStore* store = world != nullptr ? world->GetInstanceStore() : nullptr;
    return store != nullptr ? store->CountOf(setId) : 0u;
}

uint64_t WorldView::GetMassObjectCount(ComponentTypeId typeId) const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr && world->FindMassSchema(typeId) != nullptr
               ? static_cast<uint64_t>(world->GetMassObjectCount(typeId))
               : 0u;
}

bool WorldView::SpawnMassObjects(const GameProjectWorldMassSpawnBatchDesc& batch,
                                 std::span<GameProjectWorldResult> results) const noexcept {
    GameProjectWorldResult* outResults = results.data();
    const uint32_t resultCount = PROJECT::INTERNAL::SpanCount(results.size());
    const auto fillResults = [&](GameProjectWorldResult result) noexcept {
        if (outResults != nullptr) {
            for (uint32_t index = 0u; index < resultCount; ++index) {
                outResults[index] = result;
            }
        }
    };
    if (batch.objectCount != resultCount) {
        fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest));
        return false;
    }
    if (batch.objectCount == 0u) {
        return true;
    }
    if (batch.version != kGameProjectWorldMassVersion) {
        fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::Unsupported));
        return false;
    }
    if (batch.structSize != kGameProjectWorldMassSpawnBatchDescV1Size || NUMERIC::AnyNonZero(batch.reserved) ||
        batch.objectCount > kGameProjectWorldMaximumMassSpawnBatchCount || !batch.typeId.IsValid() ||
        batch.transforms == nullptr || batch.velocities == nullptr || batch.podData == nullptr ||
        batch.podStride == 0u || outResults == nullptr ||
        batch.podDataByteCount > (std::numeric_limits<size_t>::max)()) {
        fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest));
        return false;
    }

    RuntimeWorldAccessResult access{};
    World* world = GameProjectSdkAccess::GetWorld(*this, &access);
    if (world == nullptr) {
        fillResults(PROJECT::INTERNAL::MakeWorldAccessFailure(access));
        return false;
    }
    const WorldMassSchema* schema = world->FindMassSchema(batch.typeId);
    if (schema == nullptr || batch.podStride < schema->podSize) {
        fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::Unsupported));
        return false;
    }

    std::array<WorldCommandSubmission, kGameProjectWorldMaximumMassSpawnBatchCount> submissions{};
    const WorldMassSpawnBatchRequest request{*schema,
                                             std::span<const GameProjectTransform>(batch.transforms, batch.objectCount),
                                             std::span<const GameProjectFloat3>(batch.velocities, batch.objectCount),
                                             static_cast<const std::byte*>(batch.podData),
                                             batch.podStride,
                                             static_cast<size_t>(batch.podDataByteCount)};
    try {
        const WorldMutationResult admission = world->SubmitMassSpawnBatch(
            request, std::span<WorldCommandSubmission>(submissions.data(), batch.objectCount));
        if (admission != WorldMutationResult::Queued) {
            fillResults(PROJECT::INTERNAL::ToProjectMutationResult(*world, admission));
            return false;
        }
        for (uint32_t index = 0u; index < batch.objectCount; ++index) {
            outResults[index] = PROJECT::INTERNAL::ToProjectCommandSubmission(*world, submissions[index]);
        }
        return true;
    } catch (...) {
        fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError));
        return false;
    }
}

bool WorldView::DestroyMassObjects(std::span<const GameProjectObjectHandle> objectSpan,
                                   std::span<GameProjectWorldResult> results) const noexcept {
    const GameProjectObjectHandle* objects = objectSpan.data();
    const uint32_t objectCount = PROJECT::INTERNAL::SpanCount(objectSpan.size());
    GameProjectWorldResult* outResults = results.data();
    const uint32_t resultCount = PROJECT::INTERNAL::SpanCount(results.size());
    const auto fillResults = [&](GameProjectWorldResult result) noexcept {
        if (outResults != nullptr) {
            for (uint32_t index = 0u; index < resultCount; ++index) {
                outResults[index] = result;
            }
        }
    };
    if (objectCount != resultCount) {
        fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest));
        return false;
    }
    if (objectCount == 0u) {
        return true;
    }
    if (objects == nullptr || outResults == nullptr || objectCount > kGameProjectWorldMaximumMassSpawnBatchCount) {
        fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest));
        return false;
    }
    RuntimeWorldAccessResult access{};
    World* world = GameProjectSdkAccess::GetWorld(*this, &access);
    if (world == nullptr) {
        fillResults(PROJECT::INTERNAL::MakeWorldAccessFailure(access));
        return false;
    }
    std::array<RuntimeObjectHandle, kGameProjectWorldMaximumMassSpawnBatchCount> runtimeObjects{};
    for (uint32_t index = 0u; index < objectCount; ++index) {
        if (!PROJECT::INTERNAL::IsObjectHandleForWorld(objects[index], *world)) {
            fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidObject));
            return false;
        }
        runtimeObjects[index] = PROJECT::INTERNAL::DecodeRuntimeHandle(objects[index]);
    }
    std::array<WorldCommandSubmission, kGameProjectWorldMaximumMassSpawnBatchCount> submissions{};
    try {
        const WorldMutationResult admission =
            world->SubmitMassDestroyBatch(std::span<const RuntimeObjectHandle>(runtimeObjects.data(), objectCount),
                                          std::span<WorldCommandSubmission>(submissions.data(), objectCount));
        if (admission != WorldMutationResult::Queued) {
            fillResults(PROJECT::INTERNAL::ToProjectMutationResult(*world, admission));
            return false;
        }
        for (uint32_t index = 0u; index < objectCount; ++index) {
            outResults[index] = PROJECT::INTERNAL::ToProjectCommandSubmission(*world, submissions[index]);
        }
        return true;
    } catch (...) {
        fillResults(PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError));
        return false;
    }
}

GameProjectWorldQueryResult WorldView::QueryMassObjects(ComponentTypeId typeId, void* context,
                                                        GameProjectWorldMassCallback callback) const noexcept {
    GameProjectWorldQueryResult result{};
    RuntimeWorldAccessResult access = RuntimeWorldAccessResult::Success;
    World* world = GameProjectSdkAccess::GetWorld(*this, &access);
    if (world == nullptr) {
        result.code = access == RuntimeWorldAccessResult::WrongThread ? GameProjectWorldQueryResultCode::WrongThread
                                                                      : GameProjectWorldQueryResultCode::InvalidWorld;
        return result;
    }
    if (GameProjectSdkAccess::IsWorldClosing(*world)) {
        result.code = GameProjectWorldQueryResultCode::WorldClosing;
        return result;
    }
    if (callback == nullptr) {
        result.code = GameProjectWorldQueryResultCode::InvalidRequest;
        return result;
    }
    const WorldMassSchema* schema = world->FindMassSchema(typeId);
    if (!typeId.IsValid() || schema == nullptr) {
        result.code = GameProjectWorldQueryResultCode::InvalidTerm;
        result.diagnosticTermIndex = 0u;
        return result;
    }

    struct VisitContext {
        World* world = nullptr;
        void* clientContext = nullptr;
        GameProjectWorldMassCallback clientCallback = nullptr;
        GameProjectWorldQueryResult* result = nullptr;
        bool internalFailure = false;
    } visit{world, context, callback, &result, false};
    bool completed = false;
    try {
        completed = GameProjectSdkAccess::VisitMassObjects(
            *world, typeId, &visit, [](void* opaque, WorldMassMutableRow& row) noexcept {
                VisitContext& current = *static_cast<VisitContext*>(opaque);
                if (!row.object.IsValid() || row.position == nullptr || row.rotation == nullptr ||
                    row.scale == nullptr || row.velocity == nullptr || row.podData == nullptr) {
                    current.internalFailure = true;
                    return false;
                }
                GameProjectWorldMassRow publicRow{kGameProjectWorldMassRowV1Size,
                                                  kGameProjectWorldMassVersion,
                                                  PROJECT::INTERNAL::EncodeRuntimeHandle(*current.world, row.object),
                                                  row.position,
                                                  row.rotation,
                                                  row.scale,
                                                  row.velocity,
                                                  row.podData,
                                                  row.podSize,
                                                  row.podAlignment,
                                                  {0u, 0u}};
                ++current.result->rowsVisited;
                const GameProjectWorldQueryVisitResult callbackResult =
                    current.clientCallback(current.clientContext, &publicRow);
                current.result->callbackResult = callbackResult;
                switch (callbackResult) {
                case GameProjectWorldQueryVisitResult::Continue: return true;
                case GameProjectWorldQueryVisitResult::Stop:
                case GameProjectWorldQueryVisitResult::Error: return false;
                case GameProjectWorldQueryVisitResult::Count:
                    current.result->callbackResult = GameProjectWorldQueryVisitResult::Error;
                    return false;
                }
                return false;
            });
        (void)world->FlushQueuedCommands(WorldCommandPhase::QueryEnd);
    } catch (...) {
        visit.internalFailure = true;
    }

    if (visit.internalFailure) {
        result.code = GameProjectWorldQueryResultCode::InternalError;
    } else if (result.callbackResult == GameProjectWorldQueryVisitResult::Stop) {
        result.code = GameProjectWorldQueryResultCode::Stopped;
    } else if (result.callbackResult == GameProjectWorldQueryVisitResult::Error) {
        result.code = GameProjectWorldQueryResultCode::CallbackError;
    } else {
        result.code =
            completed ? GameProjectWorldQueryResultCode::Success : GameProjectWorldQueryResultCode::InternalError;
    }
    return result;
}

} // namespace HIKARI
