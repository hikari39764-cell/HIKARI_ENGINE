#include "HIKARI/Project/UiView.h"

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

namespace HIKARI {

    UiView GameProjectSdkAccess::MakeUiView(World& world) noexcept {
        try {
            return UiView(&world);
        }
        catch (...) {
            return {};
        }
    }

    bool UiView::SubmitSprite(
        std::string_view projectRelativeAssetPath,
        const GameProjectUiSpriteRequest& request) const noexcept {
        try {
            World* world = static_cast<World*>(world_);
            if (world == nullptr) {
                return false;
            }
            GAME_UI::GameUiPresentationService* service =
                world->Services().Find<
                    GAME_UI::GameUiPresentationService>();
            return service != nullptr && service->SubmitSprite(
                projectRelativeAssetPath,
                request);
        }
        catch (...) {
            return false;
        }
    }

    bool UiView::SubmitText(
        std::string_view projectRelativeFontAssetPath,
        std::string_view utf8Text,
        const GameProjectUiTextRequest& request) const noexcept {
        try {
            World* world = static_cast<World*>(world_);
            if (world == nullptr) {
                return false;
            }
            GAME_UI::GameUiPresentationService* service =
                world->Services().Find<
                    GAME_UI::GameUiPresentationService>();
            return service != nullptr && service->SubmitText(
                projectRelativeFontAssetPath,
                utf8Text,
                request);
        }
        catch (...) {
            return false;
        }
    }

    bool UiView::SubmitBackdrop(
        const GameProjectUiBackdropRequest& request) const noexcept {
        try {
            World* world = static_cast<World*>(world_);
            if (world == nullptr) {
                return false;
            }
            GAME_UI::GameUiPresentationService* service =
                world->Services().Find<
                    GAME_UI::GameUiPresentationService>();
            return service != nullptr && service->SubmitBackdrop(request);
        }
        catch (...) {
            return false;
        }
    }

} // namespace HIKARI
