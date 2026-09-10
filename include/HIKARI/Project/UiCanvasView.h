#pragma once

#include <string_view>
#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/UiCanvasTypes.h"

namespace HIKARI {
    class WorldView;

    class UiCanvasView {
    public:
        UiCanvasView() = default;
        HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
        HIKARI_GAME_PROJECT_SDK_API GameProjectUiCanvasHandle Create(const GameProjectUiCanvasDesc&) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool Destroy(GameProjectUiCanvasHandle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool NeedsUpdate(GameProjectUiCanvasHandle, uint64_t revision) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool BeginUpdate(GameProjectUiCanvasHandle, uint64_t revision) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitSprite(GameProjectUiCanvasHandle, std::string_view texturePath,
            const GameProjectUiSpriteRequest&) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitText(GameProjectUiCanvasHandle, std::string_view fontPath,
            std::string_view text, const GameProjectUiTextRequest&) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SetSourceOnly(GameProjectObjectHandle object, bool enabled) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool SubmitObject(GameProjectUiCanvasHandle canvas,
            GameProjectObjectHandle object, const GameProjectUiSpriteRequest& geometry,
            const GameProjectUiTextRequest& typography) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool CommitUpdate(GameProjectUiCanvasHandle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool CancelUpdate(GameProjectUiCanvasHandle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool BindSurface(const GameProjectUiCanvasSurfaceBinding&) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool ClearSurface(GameProjectUiCanvasHandle, GameProjectObjectHandle) const noexcept;
        HIKARI_GAME_PROJECT_SDK_API bool GetSnapshot(GameProjectUiCanvasHandle, GameProjectUiCanvasSnapshot&) const noexcept;
    private:
        friend class WorldView;
        explicit UiCanvasView(GameProjectWorldHandle world) : world_(world) {}
        GameProjectWorldHandle world_{};
    };
}
