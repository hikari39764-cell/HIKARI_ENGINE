#include "HIKARI/Project/WorldView.h"

#include "HIKARI/Project/PrefabView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectServiceBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

PrefabView GameProjectSdkAccess::MakePrefabView(World& world) noexcept {
    return PrefabView(GameProjectWorldHandle{world.GetRuntimeIdentity()});
}

PrefabView WorldView::GetPrefabs() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakePrefabView(*world) : PrefabView{};
}

bool PrefabView::IsValid() const noexcept { return PROJECT::INTERNAL::ResolveServiceWorld(world_) != nullptr; }

GameProjectPrefabSubmission PrefabView::RequestInstantiate(
    std::string_view prefabAssetGuid, const GameProjectPrefabInstantiateOptions& options) const noexcept {
    World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
    if (world == nullptr) {
        return GameProjectPrefabSubmission{{}, GameProjectPrefabError::InvalidRequest};
    }
    return world->RequestPrefabInstantiate(prefabAssetGuid, options);
}

bool PrefabView::ReadRequestResult(GameProjectPrefabRequestHandle request,
                                   GameProjectPrefabRequestResult& outResult) const noexcept {
    World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
    return world != nullptr && world->ReadPrefabRequestResult(request, outResult);
}

bool PrefabView::CancelRequest(GameProjectPrefabRequestHandle request) const noexcept {
    World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
    return world != nullptr && world->CancelPrefabRequest(request);
}

bool PrefabView::ReleaseRequest(GameProjectPrefabRequestHandle request) const noexcept {
    World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
    return world != nullptr && world->ReleasePrefabRequest(request);
}

ObjectView PrefabView::FindInstanceObject(GameProjectPrefabInstanceHandle instance,
                                          uint64_t prefabLocalObjectId) const noexcept {
    World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
    if (world == nullptr) {
        return {};
    }
    const RuntimeObjectHandle object = world->FindPrefabInstanceObject(instance, prefabLocalObjectId);
    GameObject* resolved = object.IsValid() ? world->FindObject(object) : nullptr;
    return resolved != nullptr ? GameProjectSdkAccess::MakeObjectView(*resolved) : ObjectView{};
}

GameProjectWorldResult PrefabView::DestroyInstance(GameProjectPrefabInstanceHandle instance) const noexcept {
    World* world = PROJECT::INTERNAL::ResolveServiceWorld(world_);
    if (world == nullptr) {
        RuntimeWorldAccessResult access{};
        (void)World::ResolveRuntimeWorld(world_.value, &access);
        return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }
    if (!instance.IsValid() || instance.world != world_) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidObject);
    }
    try {
        return PROJECT::INTERNAL::ToProjectCommandSubmission(*world,
                                                             world->SubmitDestroyPrefabInstance(instance.value));
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError);
    }
}

} // namespace HIKARI
