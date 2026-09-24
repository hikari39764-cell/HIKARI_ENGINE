#include "Core/Text/HIKARI_UtfPath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "Project/HIKARI_ProjectContext.h"
#include "HIKARI/Project/AudioView.h"
#include "HIKARI/Project/WorldView.h"
#include "Audio/HIKARI_Audio.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

namespace HIKARI {
using PROJECT::INTERNAL::CurrentWorld;
namespace {
AUDIO::AudioBus DecodeAudioBus(GameProjectAudioBus bus) noexcept {
    switch (bus) {
    case GameProjectAudioBus::Bgm: return AUDIO::AudioBus::Bgm;
    case GameProjectAudioBus::Voice: return AUDIO::AudioBus::Voice;
    case GameProjectAudioBus::Se:
    default: return AUDIO::AudioBus::Se;
    }
}

int DecodeAudioHandle(uint64_t value) noexcept {
    constexpr uint64_t kMaximumEncoded = static_cast<uint64_t>((std::numeric_limits<int>::max)()) + 1u;
    return value == 0u || value > kMaximumEncoded ? -1 : static_cast<int>(value - 1u);
}

uint64_t EncodeAudioHandle(int value) noexcept { return value < 0 ? 0u : static_cast<uint64_t>(value) + 1u; }

} // namespace
bool AudioView::SetBusVolume(GameProjectAudioBus bus, float linearVolume) const noexcept {
    try {
        if (CurrentWorld(world_) == nullptr || !std::isfinite(linearVolume)) {
            return false;
        }
        AUDIO::SetBusVolume(DecodeAudioBus(bus), linearVolume);
        return true;
    } catch (...) {
        return false;
    }
}

float AudioView::GetBusVolume(GameProjectAudioBus bus) const noexcept {
    try {
        if (CurrentWorld(world_) == nullptr) {
            return 0.0f;
        }
        return AUDIO::GetBusVolume(DecodeAudioBus(bus));
    } catch (...) {
        return 0.0f;
    }
}

GameProjectAudioSoundHandle AudioView::Load(std::string_view projectRelativePath) const noexcept {
    try {
        if (CurrentWorld(world_) == nullptr) {
            return {};
        }
        const auto path = PROJECT_PATHS::ResolveRuntimePayloadFile(GetProjectContext().GetRoot(), projectRelativePath);
        return path.empty()
                   ? GameProjectAudioSoundHandle{}
                   : GameProjectAudioSoundHandle{EncodeAudioHandle(AUDIO::Load(TEXT::PathToUtf8(path).c_str()))};
    } catch (...) {
        return {};
    }
}

GameProjectAudioPlaybackHandle AudioView::Play(GameProjectAudioSoundHandle sound, GameProjectAudioBus bus, uint8_t loop,
                                               float linearVolume) const noexcept {
    GameProjectAudioPlayOptions options;
    options.bus = bus;
    options.loop = loop;
    options.volume = linearVolume;
    return Play(sound, options);
}
GameProjectAudioPlaybackHandle AudioView::Play(GameProjectAudioSoundHandle sound,
                                               const GameProjectAudioPlayOptions& options) const noexcept {
    try {
        auto* world = CurrentWorld(world_);
        if (!world || GameProjectSdkAccess::IsWorldClosing(*world))
            return {};
        return {
            EncodeAudioHandle(AUDIO::PlayCue(DecodeAudioHandle(sound.value), options, world->GetRuntimeIdentity()))};
    } catch (...) {
        return {};
    }
}
bool AudioView::UseCameraListener() const noexcept {
    if (!CurrentWorld(world_))
        return false;
    AUDIO::UseCameraListener();
    return true;
}
bool AudioView::SetListener(const GameProjectAudioListener& listener) const noexcept {
    try {
        return CurrentWorld(world_) && AUDIO::SetListener(listener);
    } catch (...) {
        return false;
    }
}
bool AudioView::SetSource(GameProjectAudioPlaybackHandle playback,
                          const GameProjectAudioSource& source) const noexcept {
    try {
        auto* world = CurrentWorld(world_);
        const int handle = DecodeAudioHandle(playback.value);
        return world && AUDIO::BelongsToOwner(handle, world->GetRuntimeIdentity()) && AUDIO::SetSource(handle, source);
    } catch (...) {
        return false;
    }
}
GameProjectAudioStats AudioView::GetStats() const noexcept {
    try {
        return CurrentWorld(world_) ? AUDIO::GetStats() : GameProjectAudioStats{};
    } catch (...) {
        return {};
    }
}

bool AudioView::Stop(GameProjectAudioPlaybackHandle playback) const noexcept {
    try {
        const int decoded = DecodeAudioHandle(playback.value);
        if (CurrentWorld(world_) == nullptr || decoded < 0 ||
            !AUDIO::BelongsToOwner(decoded, CurrentWorld(world_)->GetRuntimeIdentity())) {
            return false;
        }
        AUDIO::Stop(decoded);
        return true;
    } catch (...) {
        return false;
    }
}

bool AudioView::Pause(GameProjectAudioPlaybackHandle playback) const noexcept {
    try {
        const int decoded = DecodeAudioHandle(playback.value);
        if (CurrentWorld(world_) == nullptr || decoded < 0 ||
            !AUDIO::BelongsToOwner(decoded, CurrentWorld(world_)->GetRuntimeIdentity()) || !AUDIO::IsPlaying(decoded)) {
            return false;
        }
        AUDIO::Pause(decoded);
        return true;
    } catch (...) {
        return false;
    }
}

bool AudioView::Resume(GameProjectAudioPlaybackHandle playback) const noexcept {
    try {
        const int decoded = DecodeAudioHandle(playback.value);
        if (CurrentWorld(world_) == nullptr || decoded < 0 ||
            !AUDIO::BelongsToOwner(decoded, CurrentWorld(world_)->GetRuntimeIdentity()) || !AUDIO::IsPlaying(decoded)) {
            return false;
        }
        AUDIO::Resume(decoded);
        return true;
    } catch (...) {
        return false;
    }
}

bool AudioView::SetVolume(GameProjectAudioPlaybackHandle playback, float linearVolume) const noexcept {
    try {
        const int decoded = DecodeAudioHandle(playback.value);
        if (CurrentWorld(world_) == nullptr || decoded < 0 ||
            !AUDIO::BelongsToOwner(decoded, CurrentWorld(world_)->GetRuntimeIdentity()) ||
            !std::isfinite(linearVolume) || !AUDIO::IsPlaying(decoded)) {
            return false;
        }
        AUDIO::SetVolume(decoded, linearVolume);
        return true;
    } catch (...) {
        return false;
    }
}

bool AudioView::IsPlaying(GameProjectAudioPlaybackHandle playback) const noexcept {
    try {
        const int decoded = DecodeAudioHandle(playback.value);
        return CurrentWorld(world_) != nullptr && decoded >= 0 &&
               AUDIO::BelongsToOwner(decoded, CurrentWorld(world_)->GetRuntimeIdentity()) && AUDIO::IsPlaying(decoded);
    } catch (...) {
        return false;
    }
}

AudioView GameProjectSdkAccess::MakeAudioView(World& world) noexcept { return AudioView(&world); }

AudioView WorldView::GetAudio() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeAudioView(*world) : AudioView{};
}
} // namespace HIKARI
