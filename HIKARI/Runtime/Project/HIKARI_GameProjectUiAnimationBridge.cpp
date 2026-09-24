#include "HIKARI/Project/UiView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"

namespace HIKARI {
namespace {
template <class Function>
bool AccessAnimation(void* opaque, GameProjectObjectHandle handle, Function&& fn) {
    auto* world = static_cast<World*>(opaque);
    if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world) ||
        !PROJECT::INTERNAL::IsObjectHandleForWorld(handle, *world))
        return false;
    auto* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(handle));
    auto* player = world->Services().Find<UiAnimationPlayer>();
    return object && player && fn(*world, *object, *player);
}
} // namespace
bool UiView::PlayAnimation(GameProjectObjectHandle handle, std::string_view clip) const noexcept {
    try {
        return AccessAnimation(world_, handle, [&](World& world, GameObject& object, UiAnimationPlayer& player) {
            const auto* animator = object.GetComponent<UiAnimatorComponent>();
            auto* store = world.Services().Find<UiAnimationAssetStore>();
            if (!animator || !animator->IsEnabled() || !store)
                return false;
            std::string error;
            return player.Play(world, object, store->Load(animator->GetAnimation(), error), clip);
        });
    } catch (...) {
        return false;
    }
}
bool UiView::SeekAnimation(GameProjectObjectHandle handle, float seconds) const noexcept {
    try {
        return AccessAnimation(world_, handle, [&](World& world, GameObject& object, UiAnimationPlayer& player) {
            return player.Seek(world, object.GetRuntimeHandle(), seconds);
        });
    } catch (...) {
        return false;
    }
}
bool UiView::PauseAnimation(GameProjectObjectHandle handle, bool paused) const noexcept {
    try {
        return AccessAnimation(world_, handle, [&](World&, GameObject& object, UiAnimationPlayer& player) {
            return player.Pause(object.GetRuntimeHandle(), paused);
        });
    } catch (...) {
        return false;
    }
}
bool UiView::StopAnimation(GameProjectObjectHandle handle) const noexcept {
    try {
        return AccessAnimation(world_, handle, [&](World& world, GameObject& object, UiAnimationPlayer& player) {
            const bool stopped = player.Stop(object.GetRuntimeHandle());
            player.Refresh(world);
            return stopped;
        });
    } catch (...) {
        return false;
    }
}
bool UiView::GetAnimationStatus(GameProjectObjectHandle handle, UiAnimationStatus& status) const noexcept {
    status = {};
    try {
        return AccessAnimation(world_, handle, [&](World&, GameObject& object, UiAnimationPlayer& player) {
            const auto state = player.GetStatus(object.GetRuntimeHandle());
            status = {state.seconds, state.duration, state.playing, state.closing};
            return !state.clip.empty();
        });
    } catch (...) {
        return false;
    }
}
} // namespace HIKARI
