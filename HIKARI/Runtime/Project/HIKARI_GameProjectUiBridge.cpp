#include "HIKARI/Project/UiView.h"
#include "HIKARI/Project/WorldView.h"

#include "Core/HIKARI_TimeService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include <cmath>

namespace HIKARI {
namespace {
template <class Component>
Component* ResolveUiComponent(void* opaqueWorld, GameProjectObjectHandle handle) {
    auto* world = static_cast<World*>(opaqueWorld);
    if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world) ||
        !PROJECT::INTERNAL::IsObjectHandleForWorld(handle, *world))
        return nullptr;
    auto* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(handle));
    return object ? object->GetComponent<Component>() : nullptr;
}
} // namespace
bool UiView::SetText(GameProjectObjectHandle object, std::string_view text) const noexcept {
    try {
        auto* component = ResolveUiComponent<UiTextComponent>(world_, object);
        return component && component->SetText(text);
    } catch (...) {
        return false;
    }
}
bool UiView::SetCanvasEnabled(GameProjectObjectHandle object, bool enabled) const noexcept {
    try {
        auto* component = ResolveUiComponent<UiCanvasComponent>(world_, object);
        if (!component)
            return false;
        component->SetEnabled(enabled);
        return true;
    } catch (...) {
        return false;
    }
}
bool UiView::SetButtonInteractable(GameProjectObjectHandle object, bool interactable) const noexcept {
    try {
        auto* component = ResolveUiComponent<UiButtonComponent>(world_, object);
        if (!component)
            return false;
        component->SetInteractable(interactable);
        return true;
    } catch (...) {
        return false;
    }
}

bool UiView::SetGroupVisible(GameProjectObjectHandle object, bool visible) const noexcept {
    try {
        auto* component = ResolveUiComponent<UiGroupComponent>(world_, object);
        if (!component)
            return false;
        auto& world = *static_cast<World*>(world_);
        auto* player = world.Services().Find<UiAnimationPlayer>();
        auto* target = world.FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(object));
        if (player && target)
            return player->SetVisible(world, *target, visible);
        component->SetVisible(visible);
        return true;
    } catch (...) {
        return false;
    }
}
bool UiView::SetGroupOpacity(GameProjectObjectHandle object, float opacity) const noexcept {
    if (!std::isfinite(opacity))
        return false;
    try {
        auto* component = ResolveUiComponent<UiGroupComponent>(world_, object);
        if (!component)
            return false;
        component->SetOpacity(opacity);
        return true;
    } catch (...) {
        return false;
    }
}
bool UiView::SetProgressValue(GameProjectObjectHandle object, float value) const noexcept {
    if (!std::isfinite(value))
        return false;
    try {
        auto* component = ResolveUiComponent<UiProgressComponent>(world_, object);
        if (!component)
            return false;
        component->SetValue(value);
        return true;
    } catch (...) {
        return false;
    }
}

UiView GameProjectSdkAccess::MakeUiView(World& world) noexcept {
    try {
        return UiView(&world);
    } catch (...) {
        return {};
    }
}

UiView WorldView::GetUi() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeUiView(*world) : UiView{};
}

bool UiView::SubmitSprite(std::string_view projectRelativeAssetPath,
                          const GameProjectUiSpriteRequest& request) const noexcept {
    try {
        World* world = static_cast<World*>(world_);
        if (world == nullptr || !world->IsOwnerThread()) {
            return false;
        }
        GAME_UI::GameUiPresentationService* service = world->Services().Find<GAME_UI::GameUiPresentationService>();
        return service != nullptr && service->SubmitSprite(projectRelativeAssetPath, request);
    } catch (...) {
        return false;
    }
}

bool UiView::SubmitText(std::string_view projectRelativeFontAssetPath, std::string_view utf8Text,
                        const GameProjectUiTextRequest& request) const noexcept {
    try {
        World* world = static_cast<World*>(world_);
        if (world == nullptr || !world->IsOwnerThread()) {
            return false;
        }
        GAME_UI::GameUiPresentationService* service = world->Services().Find<GAME_UI::GameUiPresentationService>();
        return service != nullptr && service->SubmitText(projectRelativeFontAssetPath, utf8Text, request);
    } catch (...) {
        return false;
    }
}

bool UiView::SubmitBackdrop(const GameProjectUiBackdropRequest& request) const noexcept {
    try {
        World* world = static_cast<World*>(world_);
        if (world == nullptr || !world->IsOwnerThread()) {
            return false;
        }
        GAME_UI::GameUiPresentationService* service = world->Services().Find<GAME_UI::GameUiPresentationService>();
        return service != nullptr && service->SubmitBackdrop(request);
    } catch (...) {
        return false;
    }
}

bool UiView::WasActivated(GameProjectObjectHandle object) const noexcept {
    try {
        World* world = static_cast<World*>(world_);
        if (world == nullptr || !world->IsOwnerThread() || !PROJECT::INTERNAL::IsObjectHandleForWorld(object, *world)) {
            return false;
        }
        GAME_UI::GameUiPresentationService* service = world->Services().Find<GAME_UI::GameUiPresentationService>();
        return service != nullptr && service->WasButtonActivated(PROJECT::INTERNAL::DecodeRuntimeHandle(object),
                                                                 TIME::GetFrameContext().frameIndex);
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI
