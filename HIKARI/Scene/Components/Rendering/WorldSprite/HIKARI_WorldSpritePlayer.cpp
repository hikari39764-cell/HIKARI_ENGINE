#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpritePlayer.h"

#include <algorithm>
#include <cmath>

#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI {

bool WorldSpritePlayer::BindAsset(std::shared_ptr<const SPRITE::SpriteAssetData> asset, std::string_view clip) {
    if (!asset || asset->pages.empty() || asset->frames.empty() || asset->clips.empty()) {
        return false;
    }

    std::string_view selectedClip = clip;
    if (selectedClip.empty()) {
        selectedClip = asset->defaultClip;
    }
    if (selectedClip.empty()) {
        selectedClip = asset->clips.front().id;
    }

    uint32_t clipIndex = 0u;
    double durationMs = 0.0;
    if (!ResolveAssetClip(*asset, selectedClip, clipIndex, durationMs)) {
        return false;
    }
    const uint32_t firstFrameIndex = asset->clips[clipIndex].frames.front().frameIndex;
    const float speed = speed_;
    asset_ = std::move(asset);
    clipName_ = asset_->clips[clipIndex].id;
    assetClipIndex_ = clipIndex;
    assetFrameIndex_ = firstFrameIndex;
    assetFrameOrdinal_ = 0u;
    gridFrameCount_ = 1u;
    gridFrameDurationMs_ = 0.0;
    durationMs_ = durationMs;
    timeMs_ = 0.0;
    speed_ = speed;
    paused_ = false;
    complete_ = false;
    RefreshFrameIndex();
    return true;
}

bool WorldSpritePlayer::Play(std::string_view clip, bool restart) { return SelectAssetClip(clip, restart); }

bool WorldSpritePlayer::SetPaused(bool paused) noexcept {
    paused_ = paused;
    return true;
}

bool WorldSpritePlayer::SetSpeed(float speed) noexcept {
    if (!MATH::IsFinite(speed) || speed <= 0.0f) {
        return false;
    }
    speed_ = speed;
    return true;
}

bool WorldSpritePlayer::Restart() noexcept {
    if (asset_ == nullptr && !HasGridAnimation()) {
        return false;
    }
    timeMs_ = 0.0;
    complete_ = false;
    paused_ = false;
    RefreshFrameIndex();
    return true;
}

bool WorldSpritePlayer::Seek(double timeMs) noexcept {
    if (!std::isfinite(timeMs) || (asset_ == nullptr && !HasGridAnimation())) {
        return false;
    }
    const double durationMs = GetDurationMs();
    if (!(durationMs > 0.0)) {
        return false;
    }
    const double requestedTimeMs = (std::max)(timeMs, 0.0);
    if (IsLooping()) {
        timeMs_ = std::fmod(requestedTimeMs, durationMs);
        complete_ = false;
    } else {
        timeMs_ = (std::min)(requestedTimeMs, durationMs);
        complete_ = timeMs_ >= durationMs;
    }
    RefreshFrameIndex();
    return true;
}

bool WorldSpritePlayer::ConfigureGrid(uint32_t frameCount, float framesPerSecond) noexcept {
    if (asset_ != nullptr || !MATH::IsFinite(framesPerSecond)) {
        return false;
    }
    frameCount = std::clamp(frameCount, 1u, 4096u);
    framesPerSecond = std::clamp(framesPerSecond, 0.0f, 60.0f);
    const uint32_t nextFrameCount = frameCount > 1u && framesPerSecond > 0.0f ? frameCount : 1u;
    const double nextFrameDurationMs = nextFrameCount > 1u ? 1000.0 / static_cast<double>(framesPerSecond) : 0.0;
    if (gridFrameCount_ == nextFrameCount && gridFrameDurationMs_ == nextFrameDurationMs) {
        return false;
    }
    gridFrameCount_ = nextFrameCount;
    gridFrameDurationMs_ = nextFrameDurationMs;
    durationMs_ = gridFrameDurationMs_ * gridFrameCount_;
    timeMs_ = 0.0;
    complete_ = false;
    RefreshFrameIndex();
    return true;
}

void WorldSpritePlayer::ClearSource() noexcept {
    asset_.reset();
    clipName_.clear();
    timeMs_ = 0.0;
    durationMs_ = 0.0;
    gridFrameDurationMs_ = 0.0;
    speed_ = 1.0f;
    assetClipIndex_ = 0u;
    assetFrameIndex_ = 0u;
    gridFrameCount_ = 1u;
    gridFrameIndex_ = 0u;
    paused_ = false;
    complete_ = false;
}

bool WorldSpritePlayer::Update(float dt) noexcept {
    if ((!asset_ && !HasGridAnimation()) || paused_ || complete_ || !MATH::IsFinite(dt) || dt <= 0.0f ||
        speed_ <= 0.0f) {
        return false;
    }
    const double durationMs = GetDurationMs();
    if (!(durationMs > 0.0)) {
        return false;
    }

    const uint32_t beforeFrame = CurrentFrameIndex();
    const bool beforeComplete = complete_;
    const double requestedTimeMs = timeMs_ + static_cast<double>(dt) * 1000.0 * speed_;
    if (IsLooping()) {
        timeMs_ = std::fmod(requestedTimeMs, durationMs);
        if (timeMs_ < 0.0) {
            timeMs_ += durationMs;
        }
        complete_ = false;
    } else if (requestedTimeMs >= durationMs) {
        timeMs_ = durationMs;
        complete_ = true;
    } else {
        timeMs_ = requestedTimeMs;
    }
    RefreshFrameIndex();
    return beforeFrame != CurrentFrameIndex() || beforeComplete != complete_;
}

const SPRITE::SpriteClip* WorldSpritePlayer::GetClip() const noexcept {
    return asset_ != nullptr && assetClipIndex_ < asset_->clips.size() ? &asset_->clips[assetClipIndex_] : nullptr;
}

const SPRITE::SpriteClipFrame* WorldSpritePlayer::GetClipFrame() const noexcept {
    const SPRITE::SpriteClip* clip = GetClip();
    return clip != nullptr && assetFrameOrdinal_ < clip->frames.size() ? &clip->frames[assetFrameOrdinal_] : nullptr;
}

const SPRITE::SpriteFrame* WorldSpritePlayer::GetSpriteFrame() const noexcept {
    const SPRITE::SpriteClipFrame* frame = GetClipFrame();
    return frame != nullptr && asset_ != nullptr && frame->frameIndex < asset_->frames.size()
               ? &asset_->frames[frame->frameIndex]
               : nullptr;
}

const SPRITE::SpritePage* WorldSpritePlayer::GetSpritePage() const noexcept {
    const SPRITE::SpriteFrame* frame = GetSpriteFrame();
    return frame != nullptr && asset_ != nullptr && frame->pageIndex < asset_->pages.size()
               ? &asset_->pages[frame->pageIndex]
               : nullptr;
}

uint32_t WorldSpritePlayer::GetFrameIndex() const noexcept {
    if (const SPRITE::SpriteClipFrame* frame = GetClipFrame()) {
        return frame->frameIndex;
    }
    return GetGridFrameIndex();
}

uint32_t WorldSpritePlayer::GetGridFrameIndex() const noexcept { return gridFrameIndex_; }

double WorldSpritePlayer::GetDurationMs() const noexcept { return durationMs_; }

bool WorldSpritePlayer::IsLooping() const noexcept {
    const SPRITE::SpriteClip* clip = GetClip();
    return clip != nullptr ? clip->loop : HasGridAnimation();
}

bool WorldSpritePlayer::SelectAssetClip(std::string_view clipName, bool restart) {
    if (asset_ == nullptr || clipName.empty()) {
        return false;
    }
    uint32_t clipIndex = 0u;
    double durationMs = 0.0;
    if (!ResolveAssetClip(*asset_, clipName, clipIndex, durationMs)) {
        return false;
    }
    const bool changedClip = clipIndex != assetClipIndex_;
    clipName_ = asset_->clips[clipIndex].id;
    assetClipIndex_ = clipIndex;
    durationMs_ = durationMs;
    if (changedClip || restart) {
        timeMs_ = 0.0;
        complete_ = false;
    }
    paused_ = false;
    RefreshFrameIndex();
    return true;
}

bool WorldSpritePlayer::ResolveAssetClip(const SPRITE::SpriteAssetData& asset, std::string_view clipName,
                                         uint32_t& outClipIndex, double& outDurationMs) noexcept {
    const auto found = std::find_if(asset.clips.begin(), asset.clips.end(),
                                    [clipName](const SPRITE::SpriteClip& clip) { return clip.id == clipName; });
    if (found == asset.clips.end() || found->frames.empty()) {
        return false;
    }
    double endTimeMs = 0.0;
    for (const SPRITE::SpriteClipFrame& frame : found->frames) {
        if (frame.durationMs == 0u || frame.frameIndex >= asset.frames.size() ||
            asset.frames[frame.frameIndex].pageIndex >= asset.pages.size() ||
            asset.frames[frame.frameIndex].sourceWidth == 0u || asset.frames[frame.frameIndex].sourceHeight == 0u ||
            !std::isfinite(frame.endTimeMs) || frame.endTimeMs <= endTimeMs) {
            return false;
        }
        endTimeMs = frame.endTimeMs;
    }
    if (!(endTimeMs > 0.0) || !std::isfinite(endTimeMs)) {
        return false;
    }
    outClipIndex = static_cast<uint32_t>(found - asset.clips.begin());
    outDurationMs = endTimeMs;
    return true;
}

uint32_t WorldSpritePlayer::ResolveAssetFrameOrdinal(double timeMs) const noexcept {
    const SPRITE::SpriteClip* clip = GetClip();
    if (clip == nullptr || clip->frames.empty()) {
        return 0u;
    }
    const auto frame = std::upper_bound(
        clip->frames.begin(), clip->frames.end(), timeMs,
        [](double value, const SPRITE::SpriteClipFrame& candidate) { return value < candidate.endTimeMs; });
    if (frame == clip->frames.end()) {
        return static_cast<uint32_t>(clip->frames.size() - 1u);
    }
    return static_cast<uint32_t>(frame - clip->frames.begin());
}

uint32_t WorldSpritePlayer::CurrentFrameIndex() const noexcept {
    return asset_ != nullptr ? assetFrameIndex_ : gridFrameIndex_;
}

void WorldSpritePlayer::RefreshFrameIndex() noexcept {
    if (asset_ != nullptr) {
        const SPRITE::SpriteClip* clip = GetClip();
        if (clip == nullptr) {
            assetFrameIndex_ = 0u;
            assetFrameOrdinal_ = 0u;
            return;
        }
        assetFrameOrdinal_ = ResolveAssetFrameOrdinal(timeMs_);
        assetFrameIndex_ = clip->frames[assetFrameOrdinal_].frameIndex;
        return;
    }
    if (!HasGridAnimation()) {
        gridFrameIndex_ = 0u;
        return;
    }
    const double index = std::floor(timeMs_ / gridFrameDurationMs_);
    gridFrameIndex_ = static_cast<uint32_t>(std::clamp(index, 0.0, static_cast<double>(gridFrameCount_ - 1u)));
}

} // namespace HIKARI
