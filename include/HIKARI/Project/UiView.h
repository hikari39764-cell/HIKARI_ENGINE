#pragma once

#include <string_view>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/UiTypes.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    // A frame-local, non-owning command view. Paths are consumed
    // synchronously and must resolve below the active project's Assets root.
    class UiView {
    public:
        UiView() = default;

        bool IsValid() const noexcept {
            return world_ != nullptr;
        }

        HIKARI_GAME_PROJECT_SDK_API bool SubmitSprite(
            std::string_view projectRelativeAssetPath,
            const GameProjectUiSpriteRequest& request) const noexcept;

        HIKARI_GAME_PROJECT_SDK_API bool SubmitText(
            std::string_view projectRelativeFontAssetPath,
            std::string_view utf8Text,
            const GameProjectUiTextRequest& request) const noexcept;

        HIKARI_GAME_PROJECT_SDK_API bool SubmitBackdrop(
            const GameProjectUiBackdropRequest& request) const noexcept;

    private:
        friend struct GameProjectSdkAccess;

        explicit UiView(void* world) noexcept
            : world_(world) {}

        void* world_ = nullptr;
    };

} // namespace HIKARI
