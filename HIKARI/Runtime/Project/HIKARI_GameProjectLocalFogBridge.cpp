#include "HIKARI/Project/LocalFogView.h"
#include "HIKARI/Project/WorldView.h"

#include "Core/HIKARI_TimeService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Lighting/HIKARI_LocalFogService.h"

namespace HIKARI {
namespace {
GameProjectWorldResult Resolve(GameProjectWorldHandle identity, GameProjectObjectHandle owner, uint64_t instanceId,
                               LocalFogService*& service) noexcept {
    using namespace PROJECT::INTERNAL;
    RuntimeWorldAccessResult access{};
    World* world = World::ResolveRuntimeWorld(identity.value, &access);
    if (world == nullptr) {
        return MakeWorldAccessFailure(access);
    }
    if (GameProjectSdkAccess::IsWorldClosing(*world)) {
        return MakeWorldResult(GameProjectWorldResultCode::WorldClosing, owner);
    }
    if (!IsObjectHandleForWorld(owner, *world) || world->FindObject(DecodeRuntimeHandle(owner)) == nullptr) {
        return MakeWorldResult(GameProjectWorldResultCode::InvalidObject, owner);
    }
    if (instanceId == 0u) {
        return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, owner);
    }
    service = world->Services().Find<LocalFogService>();
    return MakeWorldResult(service ? GameProjectWorldResultCode::Success : GameProjectWorldResultCode::Unsupported,
                           owner);
}
} // namespace

LocalFogView WorldView::GetLocalFog() const noexcept { return LocalFogView(world_); }

GameProjectWorldResult LocalFogView::Submit(GameProjectObjectHandle owner, uint64_t instanceId,
                                            const GameProjectLocalFogVolume& volume) const noexcept {
    LocalFogService* service = nullptr;
    auto result = Resolve(world_, owner, instanceId, service);
    if (result.Succeeded()) {
        result.code = service->Submit(owner.value, instanceId, volume, TIME::GetFrameContext().frameIndex);
    }
    return result;
}

GameProjectWorldResult LocalFogView::Remove(GameProjectObjectHandle owner, uint64_t instanceId) const noexcept {
    LocalFogService* service = nullptr;
    auto result = Resolve(world_, owner, instanceId, service);
    if (result.Succeeded()) {
        service->Remove(owner.value, instanceId);
    }
    return result;
}

} // namespace HIKARI
