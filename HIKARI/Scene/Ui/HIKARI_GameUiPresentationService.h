#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "HIKARI/Project/UiTypes.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Render3D/Ui/HIKARI_GameUiRenderData.h"

namespace HIKARI {
class AssetDatabase;
} // namespace HIKARI

namespace HIKARI::GAME_UI {

// World-owned owner of game-project HUD submissions. The renderer only
// borrows the immutable frame source produced by Tick.
class GameUiPresentationService final {
public:
    void SetProjectRoot(const std::filesystem::path& projectRoot);
    void SetAssetDatabase(const AssetDatabase* assetDatabase) noexcept;

    bool SubmitSprite(std::string_view assetPathBelowAssets, const GameProjectUiSpriteRequest& request,
                      const Affine2D& transform = {}, const GameUiClipRect& clip = {}) noexcept;
    bool SubmitSpriteAsset(std::string_view assetGuid, const GameProjectUiSpriteRequest& request,
                           const Affine2D& transform = {}, const GameUiClipRect& clip = {}) noexcept;
    bool SubmitText(std::string_view fontAssetPathBelowAssets, std::string_view utf8Text,
                    const GameProjectUiTextRequest& request) noexcept;
    bool SubmitStyledText(std::string_view fontAssetPathBelowAssets, std::string_view utf8Text,
                          const GameProjectUiTextRequest& request, const GameUiTextEffectStyle& style,
                          const Affine2D& transform = {}, const GameUiClipRect& clip = {},
                          const GameUiTextEditing& editing = {}) noexcept;
    bool SubmitTextStyle(const AssetGuid& style, std::string_view utf8Text, const GameProjectUiTextRequest& request,
                         float scale = 1.f) noexcept;
    // 文字样式资产（読めなければ null）。置き場所を字の大きさから決める描き手が SubmitTextStyle の前に読む。
    std::shared_ptr<const TextStyleAsset> FindTextStyle(const AssetGuid& style) const noexcept;
    bool SubmitBackdrop(const GameProjectUiBackdropRequest& request) noexcept;

    void DiscardPending() noexcept;
    bool PendingMatchesFrame() const noexcept;
    void Tick(uint64_t frameIndex) noexcept;
    void Reset() noexcept;

    const GameUiFrameSource& GetFrameSource() const noexcept { return frameSource_; }

private:
    bool ResolveTexturePath(std::string_view relativePath, std::filesystem::path& outPath) noexcept;
    bool ResolveDirectAssetPath(std::string_view relativePath, std::filesystem::path& outPath) noexcept;
    bool ResolveTextureAsset(std::string_view assetGuid, std::filesystem::path& outPath) noexcept;
    bool SubmitResolvedSprite(const std::filesystem::path& resolved, const GameProjectUiSpriteRequest& request,
                              const Affine2D& transform = {}, const GameUiClipRect& clip = {}) noexcept;

    TextAssetStore textAssets_;
    const AssetDatabase* assetDatabase_ = nullptr;
    std::filesystem::path assetsRoot_{};
    std::filesystem::path runtimePayloadAssetsRoot_{};
    std::unordered_map<std::string, std::filesystem::path> resolvedTexturePaths_{};
    std::unordered_map<std::string, std::filesystem::path> resolvedDirectAssetPaths_{};
    std::vector<GameUiSpriteDraw> pending_{};
    std::vector<GameUiTextDraw> pendingTexts_{};
    std::vector<GameUiBackdropDraw> pendingBackdrops_{};
    GameUiFrameSource frameSource_{};
    uint64_t nextSequence_ = 1u;
    uint64_t revision_ = 0u;
};

} // namespace HIKARI::GAME_UI
