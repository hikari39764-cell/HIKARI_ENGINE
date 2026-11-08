#include "HIKARI/Project/UiView.h"
#include "HIKARI/Project/WorldView.h"

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

// ゲームの直接描画（画像・文字・背景ぼかし）。フレームごとに出し直す命令だけで、UI 物体は持たない。
namespace HIKARI {

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

} // namespace HIKARI
