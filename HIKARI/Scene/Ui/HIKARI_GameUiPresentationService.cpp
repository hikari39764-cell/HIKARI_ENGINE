#include "Scene/Ui/HIKARI_GameUiPresentationService.h"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

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
        error.clear();
        const std::filesystem::path runtimeRequested =
            projectRoot / "RuntimePayload" / "Assets";
        runtimePayloadAssetsRoot_ = std::filesystem::weakly_canonical(
            runtimeRequested, error);
        if (error) {
            runtimePayloadAssetsRoot_ = runtimeRequested.lexically_normal();
        }
        Reset();
    }

    void GameUiPresentationService::SetAssetDatabase(
        const AssetDatabase* assetDatabase) noexcept {
        assetDatabase_ = assetDatabase;
        resolvedTexturePaths_.clear();
    }

    bool GameUiPresentationService::ResolveTexturePath(
        std::string_view relativePath,
        std::filesystem::path& outPath) noexcept {
        outPath.clear();
        try {
            if (relativePath.empty()) {
                return false;
            }
            const std::string cacheKey{ relativePath };
            if (const auto found = resolvedTexturePaths_.find(cacheKey);
                found != resolvedTexturePaths_.end()) {
                outPath = found->second;
                return true;
            }
            if (relativePath.starts_with("generated://")) {
                outPath = std::filesystem::path(cacheKey);
                resolvedTexturePaths_.emplace(cacheKey, outPath);
                return true;
            }
            const std::filesystem::path relative =
                TEXT::PathFromUtf8(cacheKey);
            if (relative.is_absolute() || relative.has_root_path() ||
                HasParentTraversal(relative)) {
                return false;
            }
            if (assetDatabase_ != nullptr) {
                const AssetRecord* record = assetDatabase_->FindByPath(
                    std::filesystem::path("Assets") / relative);
                if (record != nullptr && record->type == AssetType::Texture) {
                    const AssetArtifactDesc* artifact =
                        ASSETS::SEMANTICS::FindCompatibleAssetArtifact(
                            *record,
                            ASSETS::SEMANTICS::
                                AssetArtifactKind::MainTexture);
                    if (artifact != nullptr && !artifact->path.empty()) {
                        const std::filesystem::path root =
                            assetDatabase_->GetProjectRoot().
                                lexically_normal();
                        std::error_code error{};
                        const std::filesystem::path candidate =
                            std::filesystem::weakly_canonical(
                                root / artifact->path,
                                error);
                        if (!error && IsWithin(root, candidate) &&
                            std::filesystem::is_regular_file(
                                candidate,
                                error) && !error) {
                            outPath = candidate;
                            resolvedTexturePaths_.emplace(
                                cacheKey,
                                candidate);
                            return true;
                        }
                    }
                }
                if (assetDatabase_->IsCookedContent()) {
                    // Shipping consumes HTEX only. Never fall back to a copied
                    // PNG/JPG when the cooked registry is authoritative.
                    return false;
                }
            }
            return ResolveDirectAssetPath(relativePath, outPath);
        }
        catch (...) {
            return false;
        }
    }

    bool GameUiPresentationService::ResolveDirectAssetPath(
        std::string_view relativePath,
        std::filesystem::path& outPath) noexcept {
        outPath.clear();
        try {
            if (assetsRoot_.empty() || relativePath.empty()) {
                return false;
            }
            const std::string cacheKey{ relativePath };
            if (const auto found = resolvedDirectAssetPaths_.find(cacheKey);
                found != resolvedDirectAssetPaths_.end()) {
                outPath = found->second;
                return true;
            }
            const std::filesystem::path relative =
                TEXT::PathFromUtf8(cacheKey);
            if (relative.is_absolute() || relative.has_root_path() ||
                HasParentTraversal(relative)) {
                return false;
            }
            const std::filesystem::path* roots[]{
                &assetsRoot_, &runtimePayloadAssetsRoot_ };
            for (const std::filesystem::path* root : roots) {
                if (root->empty()) {
                    continue;
                }
                std::error_code error{};
                const std::filesystem::path candidate =
                    std::filesystem::weakly_canonical(
                        *root / relative,
                        error);
                if (error || !IsWithin(*root, candidate)) {
                    continue;
                }
                error.clear();
                if (!std::filesystem::is_regular_file(candidate, error) ||
                    error) {
                    continue;
                }
                outPath = candidate;
                resolvedDirectAssetPaths_.emplace(cacheKey, candidate);
                return true;
            }
            return false;
        }
        catch (...) {
            return false;
        }
    }

    bool GameUiPresentationService::SubmitSprite(
        std::string_view assetPathBelowAssets,
        const GameProjectUiSpriteRequest& request) noexcept {
        try {
            std::filesystem::path resolved{};
            return ResolveTexturePath(assetPathBelowAssets, resolved) &&
                SubmitResolvedSprite(resolved, request);
        }
        catch (...) {
            return false;
        }
    }

    bool GameUiPresentationService::SubmitSpriteAsset(
        std::string_view assetGuid,
        const GameProjectUiSpriteRequest& request) noexcept {
        try {
            std::filesystem::path resolved{};
            return ResolveTextureAsset(assetGuid, resolved) &&
                SubmitResolvedSprite(resolved, request);
        }
        catch (...) {
            return false;
        }
    }

    bool GameUiPresentationService::SubmitResolvedSprite(
        const std::filesystem::path& resolved,
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
                static_cast<uint32_t>(request.effect) >=
                    static_cast<uint32_t>(
                        GameProjectUiEffect::Count) ||
                resolved.empty()) {
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

    bool GameUiPresentationService::ResolveTextureAsset(
        std::string_view assetGuid,
        std::filesystem::path& outPath) noexcept {
        outPath.clear();
        try {
            if (assetDatabase_ == nullptr || assetGuid.empty()) {
                return false;
            }
            const AssetRecord* record = assetDatabase_->FindByGuid(
                AssetGuid{ std::string(assetGuid) });
            if (record == nullptr || record->type != AssetType::Texture) {
                return false;
            }
            const std::filesystem::path assetsPrefix{ "Assets" };
            const std::filesystem::path relative =
                record->sourcePath.lexically_relative(assetsPrefix);
            if (relative.empty() || relative == "." ||
                HasParentTraversal(relative)) {
                return false;
            }
            return ResolveTexturePath(relative.generic_string(), outPath);
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
            if (!ResolveDirectAssetPath(fontAssetPathBelowAssets, resolved)) {
                return false;
            }

            GameUiTextDraw draw{};
            draw.absoluteFontAssetPath = TEXT::PathToUtf8(resolved);
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

    void GameUiPresentationService::BeginInteractionFrame(
        uint64_t frameIndex) noexcept {
        if (frameIndex == 0u || interactionFrameIndex_ == frameIndex) {
            return;
        }
        interactionFrameIndex_ = frameIndex;
        activatedButtons_.clear();
    }

    bool GameUiPresentationService::PublishButtonActivation(
        RuntimeObjectHandle object,
        uint64_t frameIndex) noexcept {
        if (!object.IsValid() || frameIndex == 0u ||
            interactionFrameIndex_ != frameIndex) {
            return false;
        }
        const uint64_t value = object.ToValue();
        if (std::find(
                activatedButtons_.begin(),
                activatedButtons_.end(),
                value) == activatedButtons_.end()) {
            activatedButtons_.push_back(value);
        }
        return true;
    }

    bool GameUiPresentationService::WasButtonActivated(
        RuntimeObjectHandle object,
        uint64_t frameIndex) const noexcept {
        return object.IsValid() && frameIndex != 0u &&
            interactionFrameIndex_ == frameIndex &&
            std::find(
                activatedButtons_.begin(),
                activatedButtons_.end(),
                object.ToValue()) != activatedButtons_.end();
    }

    bool GameUiPresentationService::PendingMatchesFrame() const noexcept {
        if(!frameSource_.revision || !pendingBackdrops_.empty() || frameSource_.hasBackdrop ||
            pending_.size()!=frameSource_.sprites.size() || pendingTexts_.size()!=frameSource_.texts.size())return false;
        uint64_t pendingBase=~uint64_t(0),frameBase=~uint64_t(0);
        for(const auto& s:pending_)pendingBase=(std::min)(pendingBase,s.submissionSequence);
        for(const auto& s:pendingTexts_)pendingBase=(std::min)(pendingBase,s.submissionSequence);
        for(const auto& s:frameSource_.sprites)frameBase=(std::min)(frameBase,s.submissionSequence);
        for(const auto& s:frameSource_.texts)frameBase=(std::min)(frameBase,s.submissionSequence);
        for(size_t i=0;i<pending_.size();++i) {
            const auto& a=pending_[i];const auto& b=frameSource_.sprites[i];
            if(a.absoluteAssetPath!=b.absoluteAssetPath || a.request!=b.request || a.submissionSequence-pendingBase!=b.submissionSequence-frameBase)return false;
        }
        for(size_t i=0;i<pendingTexts_.size();++i) {
            const auto& a=pendingTexts_[i];const auto& b=frameSource_.texts[i];
            if(a.absoluteFontAssetPath!=b.absoluteFontAssetPath || a.utf8Text!=b.utf8Text || a.request!=b.request || a.style!=b.style || a.submissionSequence-pendingBase!=b.submissionSequence-frameBase)return false;
        }
        return true;
    }

    void GameUiPresentationService::DiscardPending() noexcept {
        pending_.clear();
        pendingTexts_.clear();
        pendingBackdrops_.clear();
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
        resolvedTexturePaths_.clear();
        resolvedDirectAssetPaths_.clear();
        frameSource_ = {};
        activatedButtons_.clear();
        interactionFrameIndex_ = 0u;
        nextSequence_ = 1u;
        ++revision_;
    }

} // namespace HIKARI::GAME_UI
