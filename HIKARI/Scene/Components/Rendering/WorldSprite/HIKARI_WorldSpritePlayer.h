#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Sprite/HIKARI_SpriteAsset.h"

namespace HIKARI {

struct WorldSpriteAnimationState {
    double timeMs = 0.0;
    double durationMs = 0.0;
    float speed = 1.0f;
    uint32_t frameIndex = 0u;
    bool bound = false;
    bool hasClip = false;
    bool loop = true;
    bool paused = false;
    bool complete = false;
};

class WorldSpritePlayer final {
public:
    bool BindAsset(std::shared_ptr<const SPRITE::SpriteAssetData> asset, std::string_view clip = {});
    bool Play(std::string_view clip, bool restart = true);
    bool SetPaused(bool paused) noexcept;
    bool SetSpeed(float speed) noexcept;
    bool Restart() noexcept;
    bool Seek(double timeMs) noexcept;
    bool ConfigureGrid(uint32_t frameCount, float framesPerSecond) noexcept;
    void ClearSource() noexcept;
    bool Update(float dt) noexcept;

    const SPRITE::SpriteAssetData* GetAsset() const noexcept { return asset_.get(); }
    const SPRITE::SpriteClip* GetClip() const noexcept;
    const SPRITE::SpriteClipFrame* GetClipFrame() const noexcept;
    const SPRITE::SpriteFrame* GetSpriteFrame() const noexcept;
    const SPRITE::SpritePage* GetSpritePage() const noexcept;
    uint32_t GetFrameIndex() const noexcept;
    uint32_t GetGridFrameIndex() const noexcept;
    const std::string& GetClipName() const noexcept { return clipName_; }
    double GetTimeMs() const noexcept { return timeMs_; }
    double GetDurationMs() const noexcept;
    float GetSpeed() const noexcept { return speed_; }
    bool IsPaused() const noexcept { return paused_; }
    bool IsComplete() const noexcept { return complete_; }
    bool IsLooping() const noexcept;
    bool HasSpriteAsset() const noexcept { return asset_ != nullptr; }
    bool HasGridAnimation() const noexcept { return gridFrameCount_ > 1u && gridFrameDurationMs_ > 0.0; }

private:
    static bool ResolveAssetClip(const SPRITE::SpriteAssetData& asset, std::string_view clipName,
                                 uint32_t& outClipIndex, double& outDurationMs) noexcept;
    bool SelectAssetClip(std::string_view clip, bool restart);
    uint32_t ResolveAssetFrameOrdinal(double timeMs) const noexcept;
    uint32_t CurrentFrameIndex() const noexcept;
    void RefreshFrameIndex() noexcept;

    std::shared_ptr<const SPRITE::SpriteAssetData> asset_{};
    std::string clipName_{};
    double timeMs_ = 0.0;
    double durationMs_ = 0.0;
    double gridFrameDurationMs_ = 0.0;
    float speed_ = 1.0f;
    uint32_t assetClipIndex_ = 0u;
    uint32_t assetFrameIndex_ = 0u;
    uint32_t assetFrameOrdinal_ = 0u;
    uint32_t gridFrameCount_ = 1u;
    uint32_t gridFrameIndex_ = 0u;
    bool paused_ = false;
    bool complete_ = false;
};

} // namespace HIKARI
