#include "HIKARI/Project/WorldView.h"
#include "HIKARI/Project/TerrainView.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

static_assert(static_cast<uint8_t>(WorldCommandKind::SetParent) ==
              static_cast<uint8_t>(GameProjectWorldCommandKind::SetParent));
static_assert(static_cast<uint8_t>(WorldCommandPhase::SystemPostRender) ==
              static_cast<uint8_t>(GameProjectWorldCommandPhase::SystemPostRender));

ObjectView GameProjectSdkAccess::MakeObjectView(GameObject& object) noexcept {
    World* world = object.GetWorld();
    return world != nullptr ? ObjectView(PROJECT::INTERNAL::EncodeRuntimeHandle(*world, object.GetRuntimeHandle()))
                            : ObjectView{};
}

WorldView GameProjectSdkAccess::MakeWorldView(World& world) noexcept {
    return WorldView(GameProjectWorldHandle{world.GetRuntimeIdentity()});
}

GameObject* GameProjectSdkAccess::GetObject(const ObjectView& view, RuntimeWorldAccessResult* outAccess) noexcept {
    World* world = World::ResolveRuntimeWorld(view.handle_.world.value, outAccess);
    return world != nullptr ? world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(view.handle_)) : nullptr;
}

World* GameProjectSdkAccess::GetWorld(const WorldView& view, RuntimeWorldAccessResult* outAccess) noexcept {
    return World::ResolveRuntimeWorld(view.world_.value, outAccess);
}

bool ObjectView::IsValid() const noexcept { return GameProjectSdkAccess::GetObject(*this) != nullptr; }

std::string_view ObjectView::GetName() const noexcept {
    const GameObject* object = GameProjectSdkAccess::GetObject(*this);
    return object != nullptr ? std::string_view(object->GetName()) : std::string_view{};
}

uint64_t ObjectView::GetSceneObjectId() const noexcept {
    const GameObject* object = GameProjectSdkAccess::GetObject(*this);
    return object != nullptr ? object->GetDocumentId().value : 0u;
}

ObjectView ObjectView::GetParent() const noexcept {
    GameObject* object = GameProjectSdkAccess::GetObject(*this);
    GameObject* parent = object != nullptr ? object->GetParent() : nullptr;
    return parent != nullptr ? GameProjectSdkAccess::MakeObjectView(*parent) : ObjectView{};
}

bool WorldView::IsValid() const noexcept { return GameProjectSdkAccess::GetWorld(*this) != nullptr; }

ObjectView WorldView::FindObject(GameProjectObjectHandle handle) const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    if (world == nullptr || !PROJECT::INTERNAL::IsObjectHandleForWorld(handle, *world)) {
        return {};
    }
    GameObject* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(handle));
    return object != nullptr ? GameProjectSdkAccess::MakeObjectView(*object) : ObjectView{};
}

ObjectView WorldView::FindSceneObject(uint64_t sceneObjectId) const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    if (world == nullptr || sceneObjectId == 0u) {
        return {};
    }
    GameObject* object = world->FindObject(SceneObjectId{sceneObjectId});
    return object != nullptr ? GameProjectSdkAccess::MakeObjectView(*object) : ObjectView{};
}

uint64_t WorldView::GetObjectCount() const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? static_cast<uint64_t>(world->GetTotalObjectCount()) : 0u;
}

bool WorldView::CanMutateStructure() const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr && world->CanMutateStructure();
}

GameProjectWorldResult WorldView::DestroyObject(GameProjectObjectHandle object) const noexcept {
    RuntimeWorldAccessResult access{};
    World* world = GameProjectSdkAccess::GetWorld(*this, &access);
    if (world == nullptr) {
        return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }
    if (!PROJECT::INTERNAL::IsObjectHandleForWorld(object, *world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidObject);
    }
    const RuntimeObjectHandle runtimeHandle = PROJECT::INTERNAL::DecodeRuntimeHandle(object);
    try {
        return PROJECT::INTERNAL::ToProjectCommandSubmission(*world, world->SubmitDestroy(runtimeHandle));
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError, object);
    }
}

GameProjectWorldResult WorldView::SetParent(GameProjectObjectHandle object, GameProjectObjectHandle parent,
                                            GameProjectParentMode mode) const noexcept {
    RuntimeWorldAccessResult access{};
    World* world = GameProjectSdkAccess::GetWorld(*this, &access);
    if (world == nullptr) {
        return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }
    if (!PROJECT::INTERNAL::IsObjectHandleForWorld(object, *world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidObject);
    }
    if (!parent.IsNull() && !PROJECT::INTERNAL::IsObjectHandleForWorld(parent, *world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidParent, object);
    }

    WorldParentMode runtimeMode{};
    switch (mode) {
    case GameProjectParentMode::KeepLocal: runtimeMode = WorldParentMode::KeepLocal; break;
    case GameProjectParentMode::KeepWorld: runtimeMode = WorldParentMode::KeepWorld; break;
    default: return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, object);
    }

    const RuntimeObjectHandle objectHandle = PROJECT::INTERNAL::DecodeRuntimeHandle(object);
    try {
        return PROJECT::INTERNAL::ToProjectCommandSubmission(
            *world, world->SubmitSetParent(objectHandle, PROJECT::INTERNAL::DecodeRuntimeHandle(parent), runtimeMode));
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError, object);
    }
}

uint32_t WorldView::GetCommandFailureCount() const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? world->GetCommandFailureCount() : 0u;
}

bool WorldView::ReadCommandFailure(uint32_t index, GameProjectWorldCommandFailure& outFailure) const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    if (world == nullptr) {
        return false;
    }
    WorldCommandFailure failure{};
    if (!world->ReadCommandFailure(index, failure)) {
        return false;
    }
    outFailure.commandToken = failure.token;
    outFailure.kind = static_cast<GameProjectWorldCommandKind>(failure.kind);
    outFailure.code = PROJECT::INTERNAL::ToProjectMutationResult(*world, failure.result, failure.object).code;
    outFailure.phase = static_cast<GameProjectWorldCommandPhase>(failure.phase);
    outFailure.reserved = 0u;
    outFailure.object = PROJECT::INTERNAL::EncodeRuntimeHandle(*world, failure.object);
    outFailure.parent = PROJECT::INTERNAL::EncodeRuntimeHandle(*world, failure.parent);
    return true;
}

uint32_t WorldView::GetCommandFailureOverflowCount() const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? world->GetCommandFailureOverflowCount() : 0u;
}

TerrainView GameProjectSdkAccess::MakeTerrainView(World& world) noexcept { return TerrainView(&world); }

TerrainView WorldView::GetTerrain() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeTerrainView(*world) : TerrainView{};
}

bool TerrainView::IsTerrainSurfaceAt(GameProjectFloat3 worldPosition, float maximumVerticalDistance) const noexcept {
    try {
        if (world_ == nullptr || !std::isfinite(worldPosition.x) || !std::isfinite(worldPosition.y) ||
            !std::isfinite(worldPosition.z) || !std::isfinite(maximumVerticalDistance) ||
            maximumVerticalDistance < 0.0f || maximumVerticalDistance > 10.0f) {
            return false;
        }
        auto* world = static_cast<World*>(world_);
        TERRAIN::TerrainSurfaceQueryService* query = world->Services().Find<TERRAIN::TerrainSurfaceQueryService>();
        if (query == nullptr) {
            return false;
        }
        float height = 0.0f;
        MATH::Vec3 normal{};
        return query->SampleWorldHeight({worldPosition.x, worldPosition.y, worldPosition.z}, height, normal) &&
               std::abs(worldPosition.y - height) <= maximumVerticalDistance;
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI
