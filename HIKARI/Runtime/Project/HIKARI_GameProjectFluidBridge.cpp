#include "HIKARI/Project/FluidView.h"
#include "HIKARI/Project/WorldView.h"

#include "Core/HIKARI_TimeService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Vfx/HIKARI_FluidInteractionService.h"

namespace HIKARI {
namespace {
GameProjectWorldResult Resolve(GameProjectWorldHandle identity, GameProjectObjectHandle owner, uint64_t instanceId,
                               FluidInteractionService*& service) noexcept {
    using namespace PROJECT::INTERNAL;
    RuntimeWorldAccessResult access{};
    World* world = World::ResolveRuntimeWorld(identity.value, &access);
    if (world == nullptr)
        return MakeWorldAccessFailure(access);
    if (GameProjectSdkAccess::IsWorldClosing(*world))
        return MakeWorldResult(GameProjectWorldResultCode::WorldClosing, owner);
    if (!IsObjectHandleForWorld(owner, *world) || world->FindObject(DecodeRuntimeHandle(owner)) == nullptr)
        return MakeWorldResult(GameProjectWorldResultCode::InvalidObject, owner);
    if (instanceId == 0u)
        return MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, owner);
    service = world->Services().Find<FluidInteractionService>();
    return MakeWorldResult(service ? GameProjectWorldResultCode::Success : GameProjectWorldResultCode::Unsupported,
                           owner);
}

// 公開の形を描画側の形へ移す。版・大きさ・予約の値を確かめ、範囲は service が確かめる。
bool Convert(const GameProjectFluidInteraction& source, RENDER3D::FLUID::FluidInteraction& out) noexcept {
    if (source.structSize != sizeof(source) || source.apiVersion != kGameProjectFluidApiVersion ||
        source.reserved != 0u ||
        static_cast<uint32_t>(source.kind) > static_cast<uint32_t>(GameProjectFluidInteractionKind::Emit) ||
        static_cast<uint32_t>(source.shape) > static_cast<uint32_t>(GameProjectFluidShape::Box))
        return false;
    out.kind = static_cast<RENDER3D::FLUID::FluidInteractionKind>(source.kind);
    out.shape = static_cast<RENDER3D::FLUID::FluidInteractionShape>(source.shape);
    out.worldPosition = {source.position.x, source.position.y, source.position.z};
    out.worldRotation = {source.rotation.x, source.rotation.y, source.rotation.z, source.rotation.w};
    out.extents = {source.extents.x, source.extents.y, source.extents.z};
    out.direction = {source.direction.x, source.direction.y, source.direction.z};
    out.strength = source.strength;
    out.absorption = source.absorption;
    return true;
}
} // namespace

FluidView WorldView::GetFluid() const noexcept { return FluidView(world_); }

GameProjectWorldResult FluidView::Submit(GameProjectObjectHandle owner, uint64_t instanceId,
                                         const GameProjectFluidInteraction& interaction) const noexcept {
    FluidInteractionService* service = nullptr;
    auto result = Resolve(world_, owner, instanceId, service);
    if (!result.Succeeded())
        return result;
    RENDER3D::FLUID::FluidInteraction converted{};
    result.code = Convert(interaction, converted)
                      ? service->Submit(FluidInteractionSource::Project, owner.value, instanceId, converted,
                                        TIME::GetFrameContext().frameIndex)
                      : GameProjectWorldResultCode::InvalidRequest;
    return result;
}

GameProjectWorldResult FluidView::Remove(GameProjectObjectHandle owner, uint64_t instanceId) const noexcept {
    FluidInteractionService* service = nullptr;
    auto result = Resolve(world_, owner, instanceId, service);
    if (result.Succeeded())
        service->Remove(FluidInteractionSource::Project, owner.value, instanceId);
    return result;
}

GameProjectWorldResult FluidView::GetRemovedSmoke(GameProjectObjectHandle owner, uint64_t instanceId,
                                                  double& outRemoved) const noexcept {
    outRemoved = 0.0;
    FluidInteractionService* service = nullptr;
    auto result = Resolve(world_, owner, instanceId, service);
    if (result.Succeeded())
        (void)service->GetRemovedSmoke(owner.value, instanceId, outRemoved);
    return result;
}

} // namespace HIKARI
