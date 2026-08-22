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

} // namespace HIKARI
