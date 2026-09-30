#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Prefab/HIKARI_PrefabAssetStore.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectTransformBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Prefab/HIKARI_PrefabDocument.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"

namespace HIKARI {
namespace {

constexpr size_t kPrefabObjectMaximum = kGameProjectPrefabMaximumObjects;

bool HasZeroReserved(const uint32_t (&values)[3]) noexcept {
    return values[0] == 0u && values[1] == 0u && values[2] == 0u;
}

GameProjectPrefabSubmission RejectPrefabRequest(GameProjectPrefabError error) noexcept {
    return GameProjectPrefabSubmission{{}, error};
}

GameProjectPrefabError ErrorForMutation(WorldMutationResult result) noexcept {
    switch (result) {
    case WorldMutationResult::InvalidParentHandle: return GameProjectPrefabError::InvalidParent;
    case WorldMutationResult::CommandQueueFull: return GameProjectPrefabError::CapacityExceeded;
    case WorldMutationResult::InvalidComponentType: return GameProjectPrefabError::UnsupportedComponent;
    case WorldMutationResult::InvalidComponentData: return GameProjectPrefabError::InvalidAsset;
    case WorldMutationResult::ComponentInitializationFailed: return GameProjectPrefabError::InitializationFailed;
    case WorldMutationResult::CommandCanceled: return GameProjectPrefabError::Cancelled;
    default: return GameProjectPrefabError::InternalError;
    }
}

WorldCommandLocalTransform ToWorldTransform(const Transform3D& transform) noexcept {
    return {transform.position, transform.rotation, transform.scale};
}

bool TryBuildPrefabTransform(const TransformData& source, Transform3D& outTransform) noexcept {
    if (!MATH::IsFinite(source.position) || !MATH::IsFinite(source.rotationEulerDeg) || !MATH::IsFinite(source.scale) ||
        std::abs(source.scale.x) <= 1.0e-8f || std::abs(source.scale.y) <= 1.0e-8f ||
        std::abs(source.scale.z) <= 1.0e-8f) {
        return false;
    }
    constexpr float kDegToRad = std::numbers::pi_v<float> / 180.0f;
    Transform3D transform{};
    transform.position = source.position;
    transform.scale = source.scale;
    transform.rotation =
        MATH::Quat::FromEulerXYZ(source.rotationEulerDeg.x * kDegToRad, source.rotationEulerDeg.y * kDegToRad,
                                 source.rotationEulerDeg.z * kDegToRad);
    if (!MATH::IsFinite(transform.GetLocalMatrix())) {
        return false;
    }
    outTransform = transform;
    return true;
}

} // namespace

GameProjectPrefabSubmission World::RequestPrefabInstantiate(
    std::string_view prefabAssetGuid, const GameProjectPrefabInstantiateOptions& options) noexcept {
    std::vector<RuntimeObjectHandle> reservedHandles{};
    std::vector<uint64_t> allocatedSceneIds{};
    std::shared_ptr<WorldPrefabInstantiatePayload> payload{};
    const auto releaseReservations = [&]() noexcept {
        if (payload != nullptr) {
            ReleasePrefabSpawnReservations(*payload);
            return;
        }
        for (RuntimeObjectHandle handle : reservedHandles) {
            if (IsReservedObjectHandle(handle)) {
                ReleaseReservedObjectHandle(handle);
            }
        }
        const size_t count = (std::min)(allocatedSceneIds.size(), reservedHandles.size());
        for (size_t index = 0u; index < count; ++index) {
            ReleaseDynamicSceneObjectId(allocatedSceneIds[index], reservedHandles[index]);
        }
    };
    try {
        if (!IsOwnerThread() || worldClosing_ || options.structSize != kGameProjectPrefabInstantiateOptionsV1Size ||
            options.version != kGameProjectPrefabInstantiateOptionsVersion || options.overrideRootTransform > 1u ||
            !HasZeroReserved(options.reserved) || !IsValidAssetGuid(prefabAssetGuid)) {
            return RejectPrefabRequest(IsValidAssetGuid(prefabAssetGuid) ? GameProjectPrefabError::InvalidRequest
                                                                         : GameProjectPrefabError::InvalidAsset);
        }
        RuntimeObjectHandle externalParent{};
        if (!options.parent.IsNull()) {
            if (!PROJECT::INTERNAL::IsObjectHandleForWorld(options.parent, *this)) {
                return RejectPrefabRequest(GameProjectPrefabError::InvalidParent);
            }
            externalParent = PROJECT::INTERNAL::DecodeRuntimeHandle(options.parent);
            if (FindObject(externalParent) == nullptr) {
                return RejectPrefabRequest(GameProjectPrefabError::InvalidParent);
            }
        }
        Transform3D rootOverride{};
        if (options.overrideRootTransform != 0u &&
            !PROJECT::INTERNAL::TryToRuntimeTransform(options.rootLocalTransform, rootOverride)) {
            return RejectPrefabRequest(GameProjectPrefabError::InvalidRequest);
        }
        size_t pendingInstanceCount = 0u;
        for (const auto& [id, request] : prefabRequests_) {
            (void)id;
            pendingInstanceCount += request.result.state == GameProjectPrefabRequestState::Pending ? 1u : 0u;
        }
        if (prefabRequests_.size() >= kGameProjectPrefabMaximumOutstandingRequests ||
            prefabInstances_.size() + pendingInstanceCount >= kGameProjectPrefabMaximumInstances ||
            nextPrefabRequestId_ == 0u || nextPrefabInstanceId_ == 0u) {
            return RejectPrefabRequest(GameProjectPrefabError::CapacityExceeded);
        }

        RuntimeAssetContextService* assetContext = Services().Find<RuntimeAssetContextService>();
        if (assetContext == nullptr || assetContext->prefabAssetStore == nullptr ||
            assetContext->assetRegistry == nullptr || assetContext->modelManager == nullptr ||
            !assetContext->preparePrefabRuntimeAssets) {
            HIKARI_LOG_ERROR("[Prefab] runtime asset services are unavailable for GUID=" +
                             std::string(prefabAssetGuid));
            return RejectPrefabRequest(GameProjectPrefabError::Dependency);
        }
        std::string loadError{};
        if (!assetContext->preparePrefabRuntimeAssets(prefabAssetGuid, loadError)) {
            HIKARI_LOG_ERROR(
                "[Prefab] runtime asset admission failed GUID=" + std::string(prefabAssetGuid) +
                " reason=" + (loadError.empty() ? std::string("unspecified admission failure") : loadError));
            return RejectPrefabRequest(GameProjectPrefabError::Dependency);
        }
        PrefabAssetLoadFailure loadFailure = PrefabAssetLoadFailure::None;
        const std::shared_ptr<const PrefabDocument> prefab =
            assetContext->prefabAssetStore->Load(AssetGuid{std::string(prefabAssetGuid)}, loadError, &loadFailure);
        if (!prefab) {
            HIKARI_LOG_ERROR("[Prefab] runtime artifact load failed GUID=" + std::string(prefabAssetGuid) +
                             " reason=" + (loadError.empty() ? std::string("unspecified load failure") : loadError));
            return RejectPrefabRequest(loadFailure == PrefabAssetLoadFailure::InvalidAsset
                                           ? GameProjectPrefabError::InvalidAsset
                                           : GameProjectPrefabError::Dependency);
        }
        if (!ValidatePrefabDocument(*prefab, loadError)) {
            HIKARI_LOG_ERROR("[Prefab] runtime artifact validation failed GUID=" + std::string(prefabAssetGuid) +
                             " reason=" + loadError);
            return RejectPrefabRequest(GameProjectPrefabError::InvalidAsset);
        }

        const size_t objectCount = prefab->descendants.size() + 1u;
        if (objectCount == 0u || objectCount > kPrefabObjectMaximum) {
            return RejectPrefabRequest(GameProjectPrefabError::CapacityExceeded);
        }

        std::vector<const SceneObjectData*> sourceObjects{};
        sourceObjects.reserve(objectCount);
        sourceObjects.push_back(&prefab->rootObject);
        for (const SceneObjectData& object : prefab->descendants) {
            sourceObjects.push_back(&object);
        }
        std::unordered_map<uint64_t, size_t> sourceIndexById{};
        sourceIndexById.reserve(objectCount);
        for (size_t index = 0u; index < sourceObjects.size(); ++index) {
            sourceIndexById.emplace(sourceObjects[index]->id.value, index);
        }
        std::unordered_map<uint64_t, std::vector<size_t>> childrenByParent{};
        childrenByParent.reserve(objectCount);
        for (size_t index = 1u; index < sourceObjects.size(); ++index) {
            const SceneObjectData& object = *sourceObjects[index];
            if (!object.parent.has_value() || !sourceIndexById.contains(object.parent->value)) {
                return RejectPrefabRequest(GameProjectPrefabError::InvalidAsset);
            }
            childrenByParent[object.parent->value].push_back(index);
        }
        std::vector<size_t> order{};
        order.reserve(objectCount);
        order.push_back(0u);
        for (size_t cursor = 0u; cursor < order.size(); ++cursor) {
            const uint64_t parentId = sourceObjects[order[cursor]]->id.value;
            const auto children = childrenByParent.find(parentId);
            if (children != childrenByParent.end()) {
                order.insert(order.end(), children->second.begin(), children->second.end());
            }
        }
        if (order.size() != objectCount) {
            return RejectPrefabRequest(GameProjectPrefabError::InvalidAsset);
        }

        reservedHandles.resize(objectCount);
        if (!ReserveObjectHandleBatch(reservedHandles)) {
            return RejectPrefabRequest(GameProjectPrefabError::CapacityExceeded);
        }
        reservedDynamicSceneObjectIds_.reserve(reservedDynamicSceneObjectIds_.size() + objectCount);

        std::unordered_map<uint64_t, uint64_t> runtimeSceneIds{};
        runtimeSceneIds.reserve(objectCount);
        allocatedSceneIds.reserve(objectCount);
        for (size_t orderedIndex : order) {
            const uint64_t localId = sourceObjects[orderedIndex]->id.value;
            uint64_t dynamicId = 0u;
            if (!ReserveDynamicSceneObjectId(reservedHandles[allocatedSceneIds.size()], dynamicId)) {
                releaseReservations();
                return RejectPrefabRequest(GameProjectPrefabError::CapacityExceeded);
            }
            runtimeSceneIds.emplace(localId, dynamicId);
            allocatedSceneIds.push_back(dynamicId);
        }

        std::vector<std::vector<std::string>> componentJson(objectCount);
        std::vector<std::vector<WorldSpawnComponentRequest>> componentRequests(objectCount);
        std::vector<WorldSpawnRequest> spawnRequests(objectCount);
        std::vector<WorldPrefabSpawnObjectPayload> instanceObjects{};
        instanceObjects.reserve(objectCount);
        for (size_t index = 0u; index < objectCount; ++index) {
            const size_t sourceIndex = order[index];
            const SceneObjectData& source = *sourceObjects[sourceIndex];
            componentJson[index].reserve(source.components.size());
            componentRequests[index].reserve(source.components.size());
            for (const SceneComponentData& component : source.components) {
                if (!component.properties.is_object()) {
                    releaseReservations();
                    return RejectPrefabRequest(GameProjectPrefabError::InvalidAsset);
                }
                nlohmann::json properties = component.properties;
                if (SCENE::SERIALIZATION::RemapSceneObjectReferences(properties, runtimeSceneIds) != 0u) {
                    releaseReservations();
                    return RejectPrefabRequest(GameProjectPrefabError::InvalidAsset);
                }
                componentJson[index].push_back(properties.dump());
                componentRequests[index].push_back(
                    WorldSpawnComponentRequest{MakeComponentTypeId(component.type), componentJson[index].back()});
            }
            Transform3D transform{};
            if (index == 0u && options.overrideRootTransform != 0u) {
                transform = rootOverride;
            } else if (!TryBuildPrefabTransform(source.transform, transform)) {
                releaseReservations();
                return RejectPrefabRequest(GameProjectPrefabError::InvalidAsset);
            }

            RuntimeObjectHandle parent{};
            if (index == 0u) {
                parent = externalParent;
            } else {
                const size_t parentSourceIndex = sourceIndexById.at(source.parent->value);
                const auto parentPosition = std::find(order.begin(), order.end(), parentSourceIndex);
                if (parentPosition == order.end() || static_cast<size_t>(parentPosition - order.begin()) >= index) {
                    releaseReservations();
                    return RejectPrefabRequest(GameProjectPrefabError::InvalidAsset);
                }
                parent = reservedHandles[static_cast<size_t>(parentPosition - order.begin())];
            }
            std::string name = source.name;
            if (name.empty()) {
                name = index == 0u && !prefab->prefabName.empty() ? prefab->prefabName : "PrefabObject";
            }
            if (name.find('\0') != std::string::npos) {
                releaseReservations();
                return RejectPrefabRequest(GameProjectPrefabError::InvalidAsset);
            }
            instanceObjects.push_back(WorldPrefabSpawnObjectPayload{
                source.id.value, runtimeSceneIds.at(source.id.value), reservedHandles[index], parent,
                ToWorldTransform(transform), std::move(name)});
            spawnRequests[index] = WorldSpawnRequest{instanceObjects.back().name, ToWorldTransform(transform), parent,
                                                     componentRequests[index]};
        }

        std::shared_ptr<WorldSpawnBatchPayload> componentPayload{};
        const WorldMutationResult componentAdmission =
            INTERNAL::BuildWorldSpawnBatchPayload(componentRegistry_, spawnRequests, componentPayload, true);
        if (componentAdmission != WorldMutationResult::Success) {
            releaseReservations();
            return RejectPrefabRequest(ErrorForMutation(componentAdmission));
        }

        payload = std::make_shared<WorldPrefabInstantiatePayload>();
        payload->requestId = nextPrefabRequestId_;
        payload->instanceId = nextPrefabInstanceId_;
        payload->components = std::move(componentPayload);
        payload->objects = std::move(instanceObjects);
        prefabRequests_.reserve(prefabRequests_.size() + 1u);
        prefabInstances_.reserve(prefabInstances_.size() + 1u);

        const uint64_t requestId = nextPrefabRequestId_;
        const uint64_t instanceId = nextPrefabInstanceId_;
        nextPrefabRequestId_ = requestId == (std::numeric_limits<uint64_t>::max)() ? 0u : requestId + 1u;
        nextPrefabInstanceId_ = instanceId == (std::numeric_limits<uint64_t>::max)() ? 0u : instanceId + 1u;
        PrefabRequestRecord record{};
        record.result.state = GameProjectPrefabRequestState::Pending;
        record.payload = payload;
        const auto [requestIt, requestInserted] = prefabRequests_.emplace(requestId, std::move(record));
        if (!requestInserted) {
            ReleasePrefabSpawnReservations(*payload);
            return RejectPrefabRequest(GameProjectPrefabError::InternalError);
        }

        PendingWorldCommand command{};
        command.kind = WorldCommandKind::PrefabInstantiate;
        command.object = payload->objects.front().object;
        command.parent = payload->objects.front().parent;
        command.payload = payload;
        command.documentId = payload->objects.front().sceneObjectId;
        const WorldCommandSubmission submission = QueueCommand(std::move(command));
        if (submission.result != WorldMutationResult::Queued) {
            prefabRequests_.erase(requestIt);
            ReleasePrefabSpawnReservations(*payload);
            return RejectPrefabRequest(ErrorForMutation(submission.result));
        }
        for (const WorldPrefabSpawnObjectPayload& object : payload->objects) {
            objectSlots_[object.object.slot].reservationToken = submission.token;
        }
        return GameProjectPrefabSubmission{
            GameProjectPrefabRequestHandle{GameProjectWorldHandle{runtimeIdentity_}, requestId},
            GameProjectPrefabError::None};
    } catch (...) {
        releaseReservations();
        return RejectPrefabRequest(GameProjectPrefabError::InternalError);
    }
}

bool World::ReadPrefabRequestResult(GameProjectPrefabRequestHandle request,
                                    GameProjectPrefabRequestResult& outResult) const noexcept {
    if (!IsOwnerThread() || request.world.value != runtimeIdentity_ || !request.IsValid() ||
        outResult.structSize != kGameProjectPrefabRequestResultV1Size ||
        outResult.version != kGameProjectPrefabRequestResultVersion) {
        return false;
    }
    const auto found = prefabRequests_.find(request.value);
    if (found == prefabRequests_.end()) {
        return false;
    }
    outResult = found->second.result;
    return true;
}

void World::ReleasePrefabSpawnReservations(const WorldPrefabInstantiatePayload& payload) noexcept {
    for (const WorldPrefabSpawnObjectPayload& object : payload.objects) {
        if (IsReservedObjectHandle(object.object)) {
            ReleaseReservedObjectHandle(object.object);
        }
        ReleaseDynamicSceneObjectId(object.sceneObjectId, object.object);
    }
}

void World::ReleasePrefabInstanceObject(uint64_t instanceId, uint64_t prefabLocalObjectId) noexcept {
    if (instanceId == 0u || prefabLocalObjectId == 0u) {
        return;
    }
    const auto instance = prefabInstances_.find(instanceId);
    if (instance == prefabInstances_.end()) {
        return;
    }
    instance->second.objectsByPrefabLocalId.erase(prefabLocalObjectId);
    if (instance->second.objectsByPrefabLocalId.empty()) {
        prefabInstances_.erase(instance);
    }
}

void World::FinishPrefabRequest(uint64_t requestId, GameProjectPrefabRequestState state,
                                GameProjectPrefabError error) noexcept {
    const auto found = prefabRequests_.find(requestId);
    if (found == prefabRequests_.end()) {
        return;
    }
    found->second.result.state = state;
    found->second.result.error = error;
    if (state != GameProjectPrefabRequestState::Pending) {
        found->second.payload.reset();
    }
}

bool World::CancelPrefabRequest(GameProjectPrefabRequestHandle request) noexcept {
    if (!IsOwnerThread() || request.world.value != runtimeIdentity_ || !request.IsValid()) {
        return false;
    }
    const auto found = prefabRequests_.find(request.value);
    if (found == prefabRequests_.end() || found->second.result.state != GameProjectPrefabRequestState::Pending) {
        return false;
    }
    found->second.result.state = GameProjectPrefabRequestState::Cancelled;
    found->second.result.error = GameProjectPrefabError::Cancelled;
    if (found->second.payload != nullptr) {
        ReleasePrefabSpawnReservations(*found->second.payload);
        found->second.payload.reset();
    }
    return true;
}

bool World::ReleasePrefabRequest(GameProjectPrefabRequestHandle request) noexcept {
    if (!IsOwnerThread() || request.world.value != runtimeIdentity_ || !request.IsValid()) {
        return false;
    }
    const auto found = prefabRequests_.find(request.value);
    if (found == prefabRequests_.end() || found->second.result.state == GameProjectPrefabRequestState::Pending) {
        return false;
    }
    prefabRequests_.erase(found);
    return true;
}

WorldMutationResult World::ApplyQueuedPrefabInstantiate(const PendingWorldCommand& command) {
    if (command.payload == nullptr || command.payload->kind != WorldCommandPayloadKind::PrefabInstantiate) {
        return WorldMutationResult::InvalidComponentData;
    }
    const auto* payload = static_cast<const WorldPrefabInstantiatePayload*>(command.payload.get());
    const auto request = prefabRequests_.find(payload->requestId);
    if (request == prefabRequests_.end() || request->second.result.state != GameProjectPrefabRequestState::Pending) {
        ReleasePrefabSpawnReservations(*payload);
        return WorldMutationResult::CommandCanceled;
    }
    if (payload->objects.empty() || payload->objects.size() > kPrefabObjectMaximum || payload->instanceId == 0u ||
        payload->objects.front().object != command.object ||
        (payload->components != nullptr && payload->components->objects.size() != payload->objects.size())) {
        FinishPrefabRequest(payload->requestId, GameProjectPrefabRequestState::Failed,
                            GameProjectPrefabError::InvalidRequest);
        ReleasePrefabSpawnReservations(*payload);
        return WorldMutationResult::InvalidComponentData;
    }
    RuntimeAssetContextService* assetContext = Services().Find<RuntimeAssetContextService>();
    if (assetContext == nullptr || assetContext->assetRegistry == nullptr || assetContext->modelManager == nullptr) {
        FinishPrefabRequest(payload->requestId, GameProjectPrefabRequestState::Failed,
                            GameProjectPrefabError::Dependency);
        ReleasePrefabSpawnReservations(*payload);
        return WorldMutationResult::ComponentInitializationFailed;
    }

    const WorldCommandCheckpoint groupCheckpoint{pendingCommands_.size(), reservationStateJournal_.size()};
    size_t createdCount = 0u;
    WorldMutationResult failure = WorldMutationResult::Success;
    GameProjectPrefabError failureError = GameProjectPrefabError::None;
    try {
        commandApplyActive_ = true;
        for (size_t index = 0u; index < payload->objects.size(); ++index) {
            if (prefabRequests_.find(payload->requestId) == prefabRequests_.end() ||
                prefabRequests_.at(payload->requestId).result.state != GameProjectPrefabRequestState::Pending) {
                failure = WorldMutationResult::CommandCanceled;
                break;
            }
            const WorldPrefabSpawnObjectPayload& object = payload->objects[index];
            PendingWorldCommand spawn{};
            spawn.kind = WorldCommandKind::Spawn;
            spawn.object = object.object;
            spawn.parent = object.parent;
            spawn.transform = object.transform;
            spawn.documentId = object.sceneObjectId;
            failure = ApplyQueuedSpawn(spawn, object.name);
            if (failure != WorldMutationResult::Success) {
                break;
            }
            ++createdCount;
        }

        if (failure == WorldMutationResult::Success && payload->components != nullptr) {
            SceneRuntimeBuilder builder{};
            for (size_t index = 0u; index < payload->objects.size() && failure == WorldMutationResult::Success;
                 ++index) {
                const WorldSpawnObjectPayload& source = payload->components->objects[index];
                for (size_t componentIndex = 0u; componentIndex < source.components.size(); ++componentIndex) {
                    const auto requestIt = prefabRequests_.find(payload->requestId);
                    if (requestIt == prefabRequests_.end() ||
                        requestIt->second.result.state != GameProjectPrefabRequestState::Pending) {
                        failure = WorldMutationResult::CommandCanceled;
                        break;
                    }
                    PendingWorldCommand spawn{};
                    spawn.kind = WorldCommandKind::Spawn;
                    spawn.object = payload->objects[index].object;
                    spawn.parent = payload->objects[index].parent;
                    spawn.transform = payload->objects[index].transform;
                    spawn.documentId = payload->objects[index].sceneObjectId;
                    spawn.payload = payload->components;
                    spawn.payloadIndex = static_cast<uint32_t>(index);
                    failure = ApplyQueuedSpawn(spawn, {}, true, static_cast<uint32_t>(componentIndex), true);
                    if (failure != WorldMutationResult::Success) {
                        break;
                    }
                    GameObject* object = FindObject(payload->objects[index].object);
                    if (object == nullptr || object->GetComponents().empty()) {
                        failure = WorldMutationResult::InternalError;
                        break;
                    }
                    std::string diagnostic{};
                    if (!builder.InitializeRuntimeComponentAssets(*object, *object->GetComponents().back(),
                                                                  *assetContext->assetRegistry,
                                                                  *assetContext->modelManager, diagnostic)) {
                        failure = WorldMutationResult::ComponentInitializationFailed;
                        failureError = GameProjectPrefabError::Dependency;
                        break;
                    }
                }
                if (failure == WorldMutationResult::Success) {
                    GameObject* object = FindObject(payload->objects[index].object);
                    if (object == nullptr) {
                        failure = WorldMutationResult::InternalError;
                        break;
                    }
                    builder.FinalizeRuntimeObjectAssets(*object, *assetContext->assetRegistry);
                }
            }
        }

        if (failure == WorldMutationResult::Success &&
            (prefabRequests_.find(payload->requestId) == prefabRequests_.end() ||
             prefabRequests_.at(payload->requestId).result.state != GameProjectPrefabRequestState::Pending)) {
            failure = WorldMutationResult::CommandCanceled;
        }

        if (failure != WorldMutationResult::Success) {
            RollbackCommandCheckpoint(groupCheckpoint);
            for (size_t index = createdCount; index-- > 0u;) {
                GameObject* object = FindObject(payload->objects[index].object);
                if (object != nullptr) {
                    (void)DestroyObject(object);
                }
            }
            RollbackCommandCheckpoint(groupCheckpoint);
            commandApplyActive_ = false;
            ReleasePrefabSpawnReservations(*payload);
            const auto requestIt = prefabRequests_.find(payload->requestId);
            if (requestIt != prefabRequests_.end() &&
                requestIt->second.result.state == GameProjectPrefabRequestState::Pending) {
                FinishPrefabRequest(payload->requestId, GameProjectPrefabRequestState::Failed,
                                    failureError != GameProjectPrefabError::None
                                        ? failureError
                                        : (failure == WorldMutationResult::CommandCanceled
                                               ? GameProjectPrefabError::Cancelled
                                               : ErrorForMutation(failure)));
            }
            return failure;
        }

        PrefabInstanceRecord instance{};
        instance.root = payload->objects.front().object;
        instance.objects.reserve(payload->objects.size());
        instance.objectsByPrefabLocalId.reserve(payload->objects.size());
        for (const WorldPrefabSpawnObjectPayload& object : payload->objects) {
            instance.objects.push_back(object.object);
            instance.objectsByPrefabLocalId.emplace(object.prefabLocalObjectId, object.object);
        }
        if (!prefabInstances_.emplace(payload->instanceId, std::move(instance)).second) {
            throw std::logic_error("Prefab instance identity was already in use.");
        }
        for (const WorldPrefabSpawnObjectPayload& object : payload->objects) {
            ObjectSlot& slot = objectSlots_[object.object.slot];
            slot.prefabInstanceId = payload->instanceId;
            slot.prefabLocalObjectId = object.prefabLocalObjectId;
        }
        GameProjectPrefabRequestResult& result = prefabRequests_.at(payload->requestId).result;
        result.instance =
            GameProjectPrefabInstanceHandle{GameProjectWorldHandle{runtimeIdentity_}, payload->instanceId};
        result.rootObject = PROJECT::INTERNAL::EncodeRuntimeHandle(*this, payload->objects.front().object);
        result.objectCount = static_cast<uint32_t>(payload->objects.size());
        reservationStateJournal_.resize(groupCheckpoint.reservationJournalCount);
        FinishPrefabRequest(payload->requestId, GameProjectPrefabRequestState::Succeeded, GameProjectPrefabError::None);
    } catch (...) {
        prefabInstances_.erase(payload->instanceId);
        RollbackCommandCheckpoint(groupCheckpoint);
        for (size_t index = createdCount; index-- > 0u;) {
            GameObject* object = FindObject(payload->objects[index].object);
            if (object != nullptr) {
                (void)DestroyObject(object);
            }
        }
        RollbackCommandCheckpoint(groupCheckpoint);
        commandApplyActive_ = false;
        ReleasePrefabSpawnReservations(*payload);
        FinishPrefabRequest(payload->requestId, GameProjectPrefabRequestState::Failed,
                            GameProjectPrefabError::InternalError);
        return WorldMutationResult::InternalError;
    }
    commandApplyActive_ = false;
    return WorldMutationResult::Success;
}

RuntimeObjectHandle World::FindPrefabInstanceObject(GameProjectPrefabInstanceHandle instance,
                                                    uint64_t prefabLocalObjectId) const noexcept {
    if (!IsOwnerThread() || instance.world.value != runtimeIdentity_ || !instance.IsValid() ||
        prefabLocalObjectId == 0u) {
        return {};
    }
    const auto foundInstance = prefabInstances_.find(instance.value);
    if (foundInstance == prefabInstances_.end()) {
        return {};
    }
    const auto foundObject = foundInstance->second.objectsByPrefabLocalId.find(prefabLocalObjectId);
    return foundObject != foundInstance->second.objectsByPrefabLocalId.end() &&
                   FindObject(foundObject->second) != nullptr
               ? foundObject->second
               : RuntimeObjectHandle{};
}

WorldCommandSubmission World::SubmitDestroyPrefabInstance(uint64_t instanceId) {
    if (!IsOwnerThread()) {
        return {WorldMutationResult::WrongThread};
    }
    if (worldClosing_) {
        return {WorldMutationResult::WorldClosing};
    }
    RuntimeObjectHandle root{};
    bool applyImmediately = false;
    bool markedPending = false;
    try {
        auto payload = std::make_shared<WorldPrefabDestroyInstancePayload>();
        {
            const auto found = prefabInstances_.find(instanceId);
            if (found == prefabInstances_.end()) {
                return {WorldMutationResult::InvalidObjectHandle};
            }
            if (found->second.destroyPending) {
                return {WorldMutationResult::Queued, found->second.root};
            }
            root = found->second.root;
            applyImmediately = CanMutateStructure() && pendingCommands_.empty();
            if (!applyImmediately && pendingCommands_.size() >= kWorldCommandMaximumCapacity) {
                return {WorldMutationResult::CommandQueueFull, root, 0u};
            }
            payload->instanceId = instanceId;
            payload->objects = found->second.objects;
            found->second.destroyPending = true;
            markedPending = true;
        }
        PendingWorldCommand command{};
        command.kind = WorldCommandKind::PrefabDestroyInstance;
        command.object = root;
        command.payload = payload;
        if (applyImmediately) {
            return {ApplyQueuedPrefabDestroyInstance(command), root, 0u};
        }
        const WorldCommandSubmission submission = QueueCommand(std::move(command));
        if (submission.result != WorldMutationResult::Queued) {
            const auto current = prefabInstances_.find(instanceId);
            if (current != prefabInstances_.end()) {
                current->second.destroyPending = false;
            }
            markedPending = false;
        }
        return {submission.result, root, submission.token};
    } catch (...) {
        if (markedPending) {
            const auto current = prefabInstances_.find(instanceId);
            if (current != prefabInstances_.end()) {
                current->second.destroyPending = false;
            }
        }
        return {WorldMutationResult::InternalError, root, 0u};
    }
}

WorldMutationResult World::ApplyQueuedPrefabDestroyInstance(const PendingWorldCommand& command) {
    if (command.payload == nullptr || command.payload->kind != WorldCommandPayloadKind::PrefabDestroyInstance) {
        return WorldMutationResult::InvalidComponentData;
    }
    const auto* payload = static_cast<const WorldPrefabDestroyInstancePayload*>(command.payload.get());
    {
        const auto found = prefabInstances_.find(payload->instanceId);
        if (found == prefabInstances_.end() || !found->second.destroyPending || found->second.root != command.object) {
            return WorldMutationResult::InvalidObjectHandle;
        }
    }
    const uint64_t instanceId = payload->instanceId;
    const bool wasApplyingCommand = commandApplyActive_;
    commandApplyActive_ = true;
    WorldMutationResult result = WorldMutationResult::Success;
    try {
        for (size_t index = payload->objects.size(); index-- > 0u;) {
            if (FindObject(payload->objects[index]) != nullptr) {
                result = DestroyObject(payload->objects[index]);
                if (result != WorldMutationResult::Success) {
                    break;
                }
            }
        }
    } catch (...) {
        result = WorldMutationResult::InternalError;
    }
    commandApplyActive_ = wasApplyingCommand;
    const auto current = prefabInstances_.find(instanceId);
    if (current == prefabInstances_.end()) {
        return result == WorldMutationResult::Success ? result : WorldMutationResult::InternalError;
    }
    if (result == WorldMutationResult::Success) {
        prefabInstances_.erase(current);
    } else {
        current->second.destroyPending = false;
    }
    return result;
}

} // namespace HIKARI
