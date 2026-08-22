#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

namespace HIKARI::GAME_UI {
    namespace {
        bool IsFinite(float value) noexcept {
            return std::isfinite(value);
        }

        bool IsFinite(const GameProjectUiColor& color) noexcept {
            return IsFinite(color.r) && IsFinite(color.g) &&
                IsFinite(color.b) && IsFinite(color.a);
        }


        bool IsValidTextEnum(const GameProjectUiTextRequest& request) {
            return static_cast<uint32_t>(request.horizontalAlign) <=
                    static_cast<uint32_t>(
                        GameProjectUiTextAlign::Right) &&
                static_cast<uint32_t>(request.verticalAlign) <=
                    static_cast<uint32_t>(
                        GameProjectUiVerticalAlign::Bottom) &&
                static_cast<uint32_t>(request.wrap) <=
                    static_cast<uint32_t>(
                        GameProjectUiTextWrap::Character);
        }

        bool HasParentTraversal(const std::filesystem::path& path) {
            for (const auto& part : path) {
                if (part == "..") {
                    return true;
                }
            }
            return false;
        }

        bool IsWithin(
            const std::filesystem::path& root,
            const std::filesystem::path& candidate) {
            auto rootIt = root.begin();
            auto candidateIt = candidate.begin();
            while (rootIt != root.end()) {
                if (candidateIt == candidate.end() ||
                    *rootIt != *candidateIt) {
                    return false;
                }
                ++rootIt;
                ++candidateIt;
            }
            return true;
        }
    }

    void GameUiPresentationService::SetProjectRoot(
        const std::filesystem::path& projectRoot) {
        std::error_code error{};
        const std::filesystem::path requested = projectRoot / "Assets";
        assetsRoot_ = std::filesystem::weakly_canonical(requested, error);
        if (error) {
            assetsRoot_ = requested.lexically_normal();
        }
        Reset();
    }

    bool GameUiPresentationService::ResolveAssetPath(
        std::string_view relativePath,
        std::filesystem::path& outPath) noexcept {
        outPath.clear();
        try {
            if (assetsRoot_.empty() || relativePath.empty()) {
                return false;
            }
            const std::string cacheKey{ relativePath };
            if (const auto found = resolvedAssetPaths_.find(cacheKey);
                found != resolvedAssetPaths_.end()) {
                outPath = found->second;
                return true;
            }
            const std::filesystem::path relative{
                cacheKey };
            if (relative.is_absolute() || relative.has_root_path() ||
                HasParentTraversal(relative)) {
                return false;
            }
            std::error_code error{};
            const std::filesystem::path candidate =
                std::filesystem::weakly_canonical(
                    assetsRoot_ / relative,
                    error);
            if (error || !IsWithin(assetsRoot_, candidate) ||
                !std::filesystem::is_regular_file(candidate, error) ||
                error) {
                return false;
            }
            outPath = candidate;
            resolvedAssetPaths_.emplace(cacheKey, candidate);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool GameUiPresentationService::SubmitSprite(
        std::string_view assetPathBelowAssets,
        const GameProjectUiSpriteRequest& request) noexcept {
        try {
            if (request.sourceId == 0u ||
                request.width <= 0.0f || request.height <= 0.0f ||
                request.referenceWidth <= 0.0f ||
                request.referenceHeight <= 0.0f ||
                !IsFinite(request.referenceWidth) ||
                !IsFinite(request.referenceHeight) ||
                !IsFinite(request.x) || !IsFinite(request.y) ||
                !IsFinite(request.width) || !IsFinite(request.height) ||
                !IsFinite(request.pivotX) || !IsFinite(request.pivotY) ||
                !IsFinite(request.rotationRadians) ||
                !IsFinite(request.fillAmount) ||
                !IsFinite(request.effectProgress) ||
                !IsFinite(request.effectIntensity) ||
                !IsFinite(request.timeSeconds) ||
                !IsFinite(request.directionX) ||
                !IsFinite(request.directionY) ||
                !IsFinite(request.tint) ||
                !IsFinite(request.effectColor) ||
                !IsFinite(request.highlightColor) ||
                static_cast<uint32_t>(request.effect) >
                    static_cast<uint32_t>(
                        GameProjectUiEffect::DialoguePortraitFocus)) {
                return false;
            }

            std::filesystem::path resolved{};
            if (!ResolveAssetPath(assetPathBelowAssets, resolved)) {
                return false;
            }

            GameUiSpriteDraw draw{};
            draw.absoluteAssetPath = resolved.string();
            draw.request = request;
            draw.request.fillAmount = std::clamp(
                request.fillAmount,
                0.0f,
                1.0f);
            draw.request.effectProgress = std::clamp(
                request.effectProgress,
                0.0f,
                1.0f);
            draw.request.effectIntensity = std::clamp(
                request.effectIntensity,
                0.0f,
                8.0f);
            draw.submissionSequence = nextSequence_++;
            pending_.push_back(std::move(draw));
            return true;
        }
        catch (...) {
            return false;
        }
    }


    bool GameUiPresentationService::SubmitText(
        std::string_view fontAssetPathBelowAssets,
        std::string_view utf8Text,
        const GameProjectUiTextRequest& request) noexcept {
        return SubmitStyledText(
            fontAssetPathBelowAssets,
            utf8Text,
            request,
            {});
    }

    bool GameUiPresentationService::SubmitStyledText(
        std::string_view fontAssetPathBelowAssets,
        std::string_view utf8Text,
        const GameProjectUiTextRequest& request,
        const GameUiTextEffectStyle& style) noexcept {
        try {
            constexpr size_t kMaximumUtf8Bytes = 64u * 1024u;
            if (request.sourceId == 0u || utf8Text.empty() ||
                utf8Text.size() > kMaximumUtf8Bytes ||
                request.referenceWidth <= 0.0f ||
                request.referenceHeight <= 0.0f ||
                request.maxWidth <= 0.0f || request.maxHeight <= 0.0f ||
                request.pixelHeight <= 0.0f ||
                request.lineHeightMultiplier <= 0.0f ||
                !IsFinite(request.referenceWidth) ||
                !IsFinite(request.referenceHeight) ||
                !IsFinite(request.x) || !IsFinite(request.y) ||
                !IsFinite(request.maxWidth) ||
                !IsFinite(request.maxHeight) ||
                !IsFinite(request.pivotX) ||
                !IsFinite(request.pivotY) ||
                !IsFinite(request.rotationRadians) ||
                !IsFinite(request.pixelHeight) ||
                !IsFinite(request.lineHeightMultiplier) ||
                !IsFinite(request.letterSpacing) ||
                !IsFinite(request.revealAmount) ||
                !IsFinite(request.effectProgress) ||
                !IsFinite(request.effectIntensity) ||
                !IsFinite(request.timeSeconds) ||
                !IsFinite(request.outlineWidth) ||
                !IsFinite(request.shadowOffsetX) ||
                !IsFinite(request.shadowOffsetY) ||
                !IsFinite(request.textColor) ||
                !IsFinite(request.outlineColor) ||
                !IsFinite(request.shadowColor) ||
                !IsFinite(style.glowColor) ||
                !IsFinite(style.glowWidth) ||
                !IsFinite(style.glowIntensity) ||
                !IsValidTextEnum(request)) {
                return false;
            }

            std::filesystem::path resolved{};
            if (!ResolveAssetPath(fontAssetPathBelowAssets, resolved)) {
                return false;
            }

            GameUiTextDraw draw{};
            draw.absoluteFontAssetPath = resolved.string();
            draw.utf8Text.assign(utf8Text.data(), utf8Text.size());
            draw.request = request;
            draw.style = style;
            draw.request.revealAmount = std::clamp(
                request.revealAmount,
                0.0f,
                1.0f);
            draw.request.effectProgress = std::clamp(
                request.effectProgress,
                0.0f,
                1.0f);
            draw.request.effectIntensity = std::clamp(
                request.effectIntensity,
                0.0f,
                8.0f);
            draw.request.outlineWidth = std::clamp(
                request.outlineWidth,
                0.0f,
                12.0f);
            draw.style.glowWidth = std::clamp(
                style.glowWidth,
                0.0f,
                12.0f);
            draw.style.glowIntensity = std::clamp(
                style.glowIntensity,
                0.0f,
                8.0f);
            draw.submissionSequence = nextSequence_++;
            pendingTexts_.push_back(std::move(draw));
            return true;
        }
        catch (...) {
            return false;
        }
    }


    bool GameUiPresentationService::SubmitBackdrop(
        const GameProjectUiBackdropRequest& request) noexcept {
        try {
            if (request.sourceId == 0u ||
                !IsFinite(request.dimAmount) ||
                !IsFinite(request.blurRadiusPixels) ||
                !IsFinite(request.desaturation) ||
                !IsFinite(request.vignette) ||
                !IsFinite(request.progress) ||
                !IsFinite(request.tint)) {
                return false;
            }
            GameUiBackdropDraw draw{};
            draw.request = request;
            draw.request.dimAmount = std::clamp(
                request.dimAmount, 0.0f, 0.95f);
            draw.request.blurRadiusPixels = std::clamp(
                request.blurRadiusPixels, 0.0f, 24.0f);
            draw.request.desaturation = std::clamp(
                request.desaturation, 0.0f, 1.0f);
            draw.request.vignette = std::clamp(
                request.vignette, 0.0f, 1.0f);
            draw.request.progress = std::clamp(
                request.progress, 0.0f, 1.0f);
            draw.submissionSequence = nextSequence_++;
            pendingBackdrops_.push_back(std::move(draw));
            return true;
        }
        catch (...) {
            return false;
        }
    }

    void GameUiPresentationService::Tick(uint64_t frameIndex) noexcept {
        try {
            // Render submission can be visited by more than one view. A
            // secondary view in the same host frame must not replace the
            // already frozen HUD source with an empty pending queue.
            if (frameIndex != 0u && frameSource_.frameIndex == frameIndex) {
                return;
            }
            frameSource_.hasBackdrop = false;
            frameSource_.backdrop = {};
            bool suppressNonModal = false;
            if (!pendingBackdrops_.empty()) {
                const auto winner = std::max_element(
                    pendingBackdrops_.begin(),
                    pendingBackdrops_.end(),
                    [](const GameUiBackdropDraw& lhs,
                       const GameUiBackdropDraw& rhs) {
                        if (lhs.request.priority != rhs.request.priority) {
                            return lhs.request.priority < rhs.request.priority;
                        }
                        if (lhs.request.sourceId != rhs.request.sourceId) {
                            return lhs.request.sourceId > rhs.request.sourceId;
                        }
                        return lhs.submissionSequence <
                            rhs.submissionSequence;
                    });
                frameSource_.backdrop = *winner;
                frameSource_.hasBackdrop = true;
                suppressNonModal =
                    (winner->request.flags &
                        kGameProjectUiSuppressNonModal) != 0u;
            }
            pendingBackdrops_.clear();
            if (suppressNonModal) {
                std::erase_if(
                    pending_,
                    [](const GameUiSpriteDraw& draw) {
                        return (draw.request.flags &
                            kGameProjectUiSubmissionModal) == 0u;
                    });
                std::erase_if(
                    pendingTexts_,
                    [](const GameUiTextDraw& draw) {
                        return (draw.request.flags &
                            kGameProjectUiSubmissionModal) == 0u;
                    });
            }
            std::stable_sort(
                pending_.begin(),
                pending_.end(),
                [](const GameUiSpriteDraw& lhs,
                   const GameUiSpriteDraw& rhs) {
                    if (lhs.request.order != rhs.request.order) {
                        return lhs.request.order < rhs.request.order;
                    }
                    if (lhs.request.sourceId != rhs.request.sourceId) {
                        return lhs.request.sourceId < rhs.request.sourceId;
                    }
                    return lhs.submissionSequence < rhs.submissionSequence;
                });
            std::stable_sort(
                pendingTexts_.begin(),
                pendingTexts_.end(),
                [](const GameUiTextDraw& lhs,
                   const GameUiTextDraw& rhs) {
                    if (lhs.request.order != rhs.request.order) {
                        return lhs.request.order < rhs.request.order;
                    }
                    if (lhs.request.sourceId != rhs.request.sourceId) {
                        return lhs.request.sourceId < rhs.request.sourceId;
                    }
                    return lhs.submissionSequence < rhs.submissionSequence;
                });
            frameSource_.sprites.clear();
            frameSource_.sprites.swap(pending_);
            frameSource_.texts.clear();
            frameSource_.texts.swap(pendingTexts_);
            frameSource_.frameIndex = frameIndex;
            frameSource_.revision = ++revision_;
        }
        catch (...) {
            pending_.clear();
            pendingTexts_.clear();
            pendingBackdrops_.clear();
            frameSource_ = {};
        }
    }

    void GameUiPresentationService::Reset() noexcept {
        pending_.clear();
        pendingTexts_.clear();
        pendingBackdrops_.clear();
        resolvedAssetPaths_.clear();
        frameSource_ = {};
        nextSequence_ = 1u;
        ++revision_;
    }

} // namespace HIKARI::GAME_UI
