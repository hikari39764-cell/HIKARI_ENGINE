#include "HIKARI/Project/UiView.h"
#include "HIKARI/Project/WorldView.h"

#include "Core/HIKARI_TimeService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiTextFieldComponent.h"
#include "Scene/Ui/HIKARI_UiSceneSystem.h"
#include "Scene/Ui/HIKARI_UiBindingSourceComponent.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/Ui/HIKARI_UiScrollViewComponent.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/Ui/HIKARI_UiAnimationPlayer.h"
#include <cmath>
#include <cstring>

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
        if (!component)
            return false;
        if (auto* field = ResolveUiComponent<UiTextFieldComponent>(world_, object); field && field->IsEnabled())
            return field->SetText(*component, text);
        return component->SetText(text);
    } catch (...) {
        return false;
    }
}
bool UiView::CopyTextFieldText(GameProjectObjectHandle object, char* destination, size_t capacity,
                               size_t* requiredBytes) const noexcept {
    if (requiredBytes)
        *requiredBytes = 0u;
    if (destination && capacity)
        destination[0] = '\0';
    if (!requiredBytes || (!destination && capacity))
        return false;
    try {
        auto* field = ResolveUiComponent<UiTextFieldComponent>(world_, object);
        auto* text = ResolveUiComponent<UiTextComponent>(world_, object);
        if (!field || !field->IsEnabled() || !text)
            return false;
        const std::string& value = text->GetText();
        *requiredBytes = value.size();
        if (capacity <= value.size() || !destination)
            return false;
        std::memcpy(destination, value.data(), value.size());
        destination[value.size()] = '\0';
        return true;
    } catch (...) {
        return false;
    }
}

bool UiView::SetBindingSource(GameProjectObjectHandle source, GameProjectObjectHandle target) const noexcept {
    try {
        auto* world = static_cast<World*>(world_);
        auto* component = ResolveUiComponent<UiBindingSourceComponent>(world_, source);
        if (!world || !component)
            return false;
        if (!target.IsValid()) {
            component->SetTargetObjectId({});
            return true;
        }
        if (!world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world) ||
            !PROJECT::INTERNAL::IsObjectHandleForWorld(target, *world))
            return false;
        auto* targetObject = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(target));
        if (!targetObject || !targetObject->GetDocumentId().value)
            return false;
        component->SetTargetObjectId(targetObject->GetDocumentId());
        return true;
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
bool UiView::SetCanvasInputActive(GameProjectObjectHandle object, bool active) const noexcept {
    try {
        auto* component = ResolveUiComponent<UiCanvasComponent>(world_, object);
        if (!component)
            return false;
        component->SetInputActive(active);
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

bool UiView::RequestFocus(GameProjectObjectHandle control) const noexcept {
    try {
        auto* world = static_cast<World*>(world_);
        auto* object = world && world->IsOwnerThread() && !GameProjectSdkAccess::IsWorldClosing(*world) &&
                               PROJECT::INTERNAL::IsObjectHandleForWorld(control, *world)
                           ? world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(control))
                           : nullptr;
        if (!object || (!object->GetComponent<UiButtonComponent>() && !object->GetComponent<UiTextFieldComponent>()))
            return false;
        for (GameObject* ancestor = object; ancestor; ancestor = ancestor->GetParent()) {
            if (auto* canvas = ancestor->GetComponent<UiCanvasComponent>())
                return canvas->IsEnabled() && canvas->IsInputActive() &&
                       canvas->RequestFocus(object->GetRuntimeHandle());
        }
        return false;
    } catch (...) {
        return false;
    }
}

GameProjectObjectHandle UiView::GetFocusedControl() const noexcept {
    try {
        auto* world = static_cast<World*>(world_);
        if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world))
            return {};
        auto* system = world->Services().Find<UiSceneSystem>();
        if (!system)
            return {};
        const RuntimeObjectHandle focused = system->GetFocusedControl();
        if (!focused.IsValid() || !world->FindObject(focused))
            return {};
        return PROJECT::INTERNAL::EncodeRuntimeHandle(*world, focused);
    } catch (...) {
        return {};
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

bool UiView::SetScrollOffset(GameProjectObjectHandle object, float offset) const noexcept {
    if (!std::isfinite(offset))
        return false;
    try {
        auto* world = static_cast<World*>(world_);
        auto* component = ResolveUiComponent<UiScrollViewComponent>(world_, object);
        auto* system = world ? world->Services().Find<UiSceneSystem>() : nullptr;
        auto* target = world ? world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(object)) : nullptr;
        if (!component || !system || !target)
            return false;
        const auto layout = system->ResolveLayout(*world, *target);
        if (!layout.valid)
            return false;
        const float viewport = component->GetAxis() == UiScrollAxis::Vertical ? layout.height : layout.width;
        component->SetScrollOffset(offset, viewport);
        return true;
    } catch (...) {
        return false;
    }
}

bool UiView::GetScrollOffset(GameProjectObjectHandle object, float& offset) const noexcept {
    offset = 0.0f;
    try {
        auto* world = static_cast<World*>(world_);
        auto* component = ResolveUiComponent<UiScrollViewComponent>(world_, object);
        auto* system = world ? world->Services().Find<UiSceneSystem>() : nullptr;
        auto* target = world ? world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(object)) : nullptr;
        if (!component || !system || !target)
            return false;
        const auto layout = system->ResolveLayout(*world, *target);
        if (!layout.valid)
            return false;
        const float viewport = component->GetAxis() == UiScrollAxis::Vertical ? layout.height : layout.width;
        offset = component->GetScrollOffset(viewport);
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
