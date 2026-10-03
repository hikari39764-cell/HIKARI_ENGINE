#include "HIKARI/Project/SpriteAnimationView.h"

#include <cmath>
#include <memory>
#include <string>

#include "Assets/Sprite/HIKARI_SpriteAssetStore.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

namespace {
struct ResolvedSpriteAnimationTarget {
    World* world = nullptr;
    WorldSpriteComponent* component = nullptr;
};
} // namespace

struct GameProjectSpriteAnimationBridgeAccess {
    static GameProjectWorldResult MakeResult(const SpriteAnimationView& view,
                                             GameProjectWorldResultCode code) noexcept {
        return PROJECT::INTERNAL::MakeWorldResult(code, view.object_);
    }

    static GameProjectWorldResult Resolve(const SpriteAnimationView& view,
                                          ResolvedSpriteAnimationTarget& outTarget) noexcept {
        outTarget = {};
        try {
            RuntimeWorldAccessResult access{};
            World* world = World::ResolveRuntimeWorld(view.object_.world.value, &access);
            if (world == nullptr) {
                return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
            }
            if (!world->IsOwnerThread()) {
                return MakeResult(view, GameProjectWorldResultCode::WrongThread);
            }
            if (!PROJECT::INTERNAL::IsObjectHandleForWorld(view.object_, *world)) {
                return MakeResult(view, GameProjectWorldResultCode::InvalidObject);
            }
            if (GameProjectSdkAccess::IsWorldClosing(*world)) {
                return MakeResult(view, GameProjectWorldResultCode::WorldClosing);
            }

            GameObject* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(view.object_));
            if (object == nullptr) {
                return MakeResult(view, GameProjectWorldResultCode::InvalidObject);
            }
            WorldSpriteComponent* component = object->GetComponent<WorldSpriteComponent>();
            if (component == nullptr || component->IsCanvasSourceOnly()) {
                return MakeResult(view, GameProjectWorldResultCode::Unsupported);
            }
            outTarget = {world, component};
            return MakeResult(view, GameProjectWorldResultCode::Success);
        } catch (...) {
            outTarget = {};
            return MakeResult(view, GameProjectWorldResultCode::InternalError);
        }
    }
};

SpriteAnimationView ObjectView::GetSpriteAnimation() const noexcept { return SpriteAnimationView(handle_); }

bool SpriteAnimationView::IsValid() const noexcept {
    ResolvedSpriteAnimationTarget target{};
    return GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target).Succeeded();
}

GameProjectWorldResult SpriteAnimationView::Bind(std::string_view spriteAssetGuid) const noexcept {
    ResolvedSpriteAnimationTarget target{};
    GameProjectWorldResult result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    if (spriteAssetGuid.empty() || spriteAssetGuid.size() > kGameProjectSpriteAnimationAssetGuidMaximumBytes ||
        spriteAssetGuid.find('\0') != std::string_view::npos) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }

    try {
        SPRITE::SpriteAssetStore* store = target.world->Services().Find<SPRITE::SpriteAssetStore>();
        if (store == nullptr) {
            return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::Unsupported);
        }
        const AssetGuid guid{std::string(spriteAssetGuid)};
        if (!guid.IsValid()) {
            return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this,
                                                                      GameProjectWorldResultCode::InvalidRequest);
        }
        std::string error{};
        const std::shared_ptr<const SPRITE::SpriteAssetData> asset = store->Load(guid, error);
        if (asset == nullptr) {
            return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidAsset);
        }
        if (!target.component->BindSpriteAsset(guid, asset)) {
            return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidAsset);
        }
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::Success);
    } catch (...) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteAnimationView::Play(std::string_view clipId, bool restart) const noexcept {
    ResolvedSpriteAnimationTarget target{};
    GameProjectWorldResult result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    if (clipId.empty() || clipId.find('\0') != std::string_view::npos) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    if (!target.component->GetSpriteAssetGuid().IsValid()) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    try {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(
            *this, target.component->PlayAnimation(clipId, restart) ? GameProjectWorldResultCode::Success
                                                                    : GameProjectWorldResultCode::InvalidName);
    } catch (...) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteAnimationView::Pause() const noexcept {
    ResolvedSpriteAnimationTarget target{};
    GameProjectWorldResult result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(
            *this, target.component->SetPaused(true) ? GameProjectWorldResultCode::Success
                                                     : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteAnimationView::Resume() const noexcept {
    ResolvedSpriteAnimationTarget target{};
    GameProjectWorldResult result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(
            *this, target.component->SetPaused(false) ? GameProjectWorldResultCode::Success
                                                      : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteAnimationView::Restart() const noexcept {
    ResolvedSpriteAnimationTarget target{};
    GameProjectWorldResult result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(
            *this, target.component->RestartAnimation() ? GameProjectWorldResultCode::Success
                                                        : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteAnimationView::SetSpeed(float speed) const noexcept {
    ResolvedSpriteAnimationTarget target{};
    GameProjectWorldResult result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    if (!MATH::IsFinite(speed) || speed <= 0.0f) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    try {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(
            *this, target.component->SetPlaybackSpeed(speed) ? GameProjectWorldResultCode::Success
                                                             : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteAnimationView::Seek(float elapsedSeconds) const noexcept {
    ResolvedSpriteAnimationTarget target{};
    GameProjectWorldResult result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    if (!MATH::IsFinite(elapsedSeconds) || elapsedSeconds < 0.0f) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    try {
        const double elapsedMilliseconds = static_cast<double>(elapsedSeconds) * 1000.0;
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(
            *this, target.component->SeekAnimation(elapsedMilliseconds) ? GameProjectWorldResultCode::Success
                                                                        : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteAnimationView::TryGetClipInfo(std::string_view clipId,
                                                           GameProjectSpriteAnimationClipInfo& outInfo) const noexcept {
    if (outInfo.structSize != sizeof(outInfo) || outInfo.version != kGameProjectSpriteAnimationClipInfoVersion ||
        clipId.empty() || clipId.find('\0') != std::string_view::npos) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    ResolvedSpriteAnimationTarget target{};
    auto result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded())
        return result;
    if (!target.component->HasBoundSpriteAsset()) {
        result.code = GameProjectWorldResultCode::InvalidAsset;
        return result;
    }
    const auto* clip = target.component->FindAnimationClip(clipId);
    if (clip == nullptr || clip->frames.empty()) {
        result.code = GameProjectWorldResultCode::InvalidName;
        return result;
    }
    GameProjectSpriteAnimationClipInfo info{};
    info.frameCount = static_cast<uint32_t>(clip->frames.size());
    info.durationSeconds = static_cast<float>(clip->frames.back().endTimeMs / 1000.0);
    info.loop = clip->loop ? 1u : 0u;
    if (!MATH::IsFinite(info.durationSeconds) || info.durationSeconds <= 0.0f) {
        result.code = GameProjectWorldResultCode::InvalidAsset;
        return result;
    }
    outInfo = info;
    return result;
}

GameProjectWorldResult SpriteAnimationView::TryGetState(GameProjectSpriteAnimationState& outState) const noexcept {
    if (outState.structSize != kGameProjectSpriteAnimationStateV1Size ||
        outState.version != kGameProjectSpriteAnimationStateVersion) {
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    outState = {};
    ResolvedSpriteAnimationTarget target{};
    GameProjectWorldResult result = GameProjectSpriteAnimationBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        const WorldSpriteAnimationState source = target.component->GetAnimationState();
        const double elapsedSeconds = source.timeMs / 1000.0;
        const double durationSeconds = source.durationMs / 1000.0;
        if (!std::isfinite(elapsedSeconds) || !std::isfinite(durationSeconds) || !MATH::IsFinite(source.speed) ||
            elapsedSeconds < 0.0 || durationSeconds < 0.0 || source.speed <= 0.0f) {
            return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
        }
        const float elapsedSecondsValue = static_cast<float>(elapsedSeconds);
        const float durationSecondsValue = static_cast<float>(durationSeconds);
        if (!MATH::IsFinite(elapsedSecondsValue) || !MATH::IsFinite(durationSecondsValue)) {
            return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
        }

        GameProjectSpriteAnimationState state{};
        state.frameIndex = source.frameIndex;
        state.elapsedSeconds = elapsedSecondsValue;
        state.durationSeconds = durationSecondsValue;
        state.speed = source.speed;
        state.flags = source.loop ? kGameProjectSpriteAnimationStateKnownFlags : 0u;
        if (!source.bound) {
            state.playbackState = GameProjectSpriteAnimationPlaybackState::Unbound;
        } else if (!source.hasClip) {
            state.playbackState = GameProjectSpriteAnimationPlaybackState::Stopped;
        } else if (source.complete) {
            state.playbackState = GameProjectSpriteAnimationPlaybackState::Finished;
        } else if (source.paused) {
            state.playbackState = GameProjectSpriteAnimationPlaybackState::Paused;
        } else {
            state.playbackState = GameProjectSpriteAnimationPlaybackState::Playing;
        }
        outState = state;
        return result;
    } catch (...) {
        outState = {};
        return GameProjectSpriteAnimationBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

} // namespace HIKARI
