#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "HIKARI/Project/UiTypes.h"
#include "Render2D/HIKARI_GameUiRenderData.h"

namespace HIKARI {
    class AssetDatabase;
}

namespace HIKARI::GAME_UI {

    // World-owned owner of game-project HUD submissions. The renderer only
    // borrows the immutable frame source produced by Tick.
    class GameUiPresentationService final {
    public:
        void SetProjectRoot(const std::filesystem::path& projectRoot);
        void SetAssetDatabase(const AssetDatabase* assetDatabase) noexcept;

        bool SubmitSprite(
            std::string_view assetPathBelowAssets,
            const GameProjectUiSpriteRequest& request) noexcept;
        bool SubmitText(
            std::string_view fontAssetPathBelowAssets,
            std::string_view utf8Text,
            const GameProjectUiTextRequest& request) noexcept;
        bool SubmitStyledText(
            std::string_view fontAssetPathBelowAssets,
            std::string_view utf8Text,
            const GameProjectUiTextRequest& request,
            const GameUiTextEffectStyle& style) noexcept;
        bool SubmitBackdrop(
            const GameProjectUiBackdropRequest& request) noexcept;

        void Tick(uint64_t frameIndex) noexcept;
        void Reset() noexcept;

        const GameUiFrameSource& GetFrameSource() const noexcept {
            return frameSource_;
        }

    private:
        bool ResolveTexturePath(
            std::string_view relativePath,
            std::filesystem::path& outPath) noexcept;
        bool ResolveDirectAssetPath(
            std::string_view relativePath,
            std::filesystem::path& outPath) noexcept;

        const AssetDatabase* assetDatabase_ = nullptr;
        std::filesystem::path assetsRoot_{};
        std::filesystem::path runtimePayloadAssetsRoot_{};
        std::unordered_map<std::string, std::filesystem::path>
            resolvedTexturePaths_{};
        std::unordered_map<std::string, std::filesystem::path>
            resolvedDirectAssetPaths_{};
        std::vector<GameUiSpriteDraw> pending_{};
        std::vector<GameUiTextDraw> pendingTexts_{};
        std::vector<GameUiBackdropDraw> pendingBackdrops_{};
        GameUiFrameSource frameSource_{};
        uint64_t nextSequence_ = 1u;
        uint64_t revision_ = 0u;
    };

} // namespace HIKARI::GAME_UI
