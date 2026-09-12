#include "HIKARI_Audio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>

#include "HIKARI_IAudioBackend.h"
#include "Backends/HIKARI_AudioBackend_XAudio2.h"

namespace HIKARI::AUDIO {

static std::unique_ptr<IAudioBackend> g_backend{};

namespace {

struct PlayingAudioRecord {
    float sourceVolume = 1.0f;
    AudioBus bus = AudioBus::Se;
};

std::array<float, 3u> g_busVolumes{ 1.0f, 1.0f, 1.0f };
std::unordered_map<int, PlayingAudioRecord> g_playing{};
std::unordered_map<std::string, int> g_loaded{};

size_t BusIndex(AudioBus bus) noexcept {
    switch (bus) {
    case AudioBus::Bgm: return 0u;
    case AudioBus::Voice: return 2u;
    case AudioBus::Se:
    default: return 1u;
    }
}

float SafeVolume(float value) noexcept {
    return std::isfinite(value)
        ? std::clamp(value, 0.0f, 1.0f)
        : 1.0f;
}

class NullAudioBackend final : public IAudioBackend {
public:
    bool Initialize() override { return true; }
    void Shutdown() override {}

    int Load(const char*) override { return -1; }
    int Play(int, bool, float) override { return -1; }
    void Stop(int) override {}
    void Pause(int) override {}
    void Resume(int) override {}
    void SetVolume(int, float) override {}
    bool IsPlaying(int) override { return false; }
};

std::unique_ptr<IAudioBackend> CreateBackend(BackendType type)
{
    switch (type) {
    case BackendType::Null:
        return std::make_unique<NullAudioBackend>();
    case BackendType::XAudio2:
    default:
        return std::make_unique<AudioBackendXAudio2>();
    }
}

} // namespace

bool Initialize(BackendType type) {
    Shutdown();

    g_backend = CreateBackend(type);
    if (g_backend && g_backend->Initialize()) {
        return true;
    }

    // 音声初期化に失敗しても、ゲーム実行自体は継続できるようにする。
    g_backend = std::make_unique<NullAudioBackend>();
    g_backend->Initialize();
    return false;
}

void Shutdown() {
    if (g_backend) {
        g_backend->Shutdown();
        g_backend.reset();
    }
    g_playing.clear();
    g_loaded.clear();
}

void PreloadDirectory(const std::filesystem::path& root) {
    if (g_backend) g_backend->PreloadDirectory(root);
}

int Load(const char* path) {
    if (!g_backend || path == nullptr || path[0] == '\0') {
        return -1;
    }
    const auto found = g_loaded.find(path);
    if (found != g_loaded.end()) {
        return found->second;
    }
    const int handle = g_backend->Load(path);
    if (handle >= 0) {
        g_loaded.emplace(path, handle);
    }
    return handle;
}
int Play(int soundHandle, bool loop, float volume) {
    return PlayOnBus(soundHandle, loop, volume, AudioBus::Se);
}
int PlayOnBus(
    int soundHandle,
    bool loop,
    float volume,
    AudioBus bus) {
    if (!g_backend) {
        return -1;
    }
    const float sourceVolume = SafeVolume(volume);
    const int playHandle = g_backend->Play(
        soundHandle,
        loop,
        sourceVolume * g_busVolumes[BusIndex(bus)]);
    if (playHandle >= 0) {
        g_playing[playHandle] = PlayingAudioRecord{
            sourceVolume,
            bus };
    }
    return playHandle;
}
void Stop(int playHandle) {
    if (g_backend) g_backend->Stop(playHandle);
    g_playing.erase(playHandle);
}
void Pause(int playHandle) { if (g_backend) g_backend->Pause(playHandle); }
void Resume(int playHandle) { if (g_backend) g_backend->Resume(playHandle); }
void SetVolume(int playHandle, float volume) {
    if (!g_backend) return;
    const float sourceVolume = SafeVolume(volume);
    const auto found = g_playing.find(playHandle);
    if (found == g_playing.end()) {
        g_backend->SetVolume(playHandle, sourceVolume);
        return;
    }
    found->second.sourceVolume = sourceVolume;
    g_backend->SetVolume(
        playHandle,
        sourceVolume * g_busVolumes[BusIndex(found->second.bus)]);
}
bool IsPlaying(int playHandle) {
    const bool playing = g_backend && g_backend->IsPlaying(playHandle);
    if (!playing) {
        g_playing.erase(playHandle);
    }
    return playing;
}

void SetBusVolume(AudioBus bus, float volume) {
    const size_t index = BusIndex(bus);
    g_busVolumes[index] = SafeVolume(volume);
    if (!g_backend) {
        return;
    }
    for (auto iterator = g_playing.begin();
         iterator != g_playing.end();) {
        if (!g_backend->IsPlaying(iterator->first)) {
            iterator = g_playing.erase(iterator);
            continue;
        }
        if (iterator->second.bus == bus) {
            g_backend->SetVolume(
                iterator->first,
                iterator->second.sourceVolume * g_busVolumes[index]);
        }
        ++iterator;
    }
}

float GetBusVolume(AudioBus bus) {
    return g_busVolumes[BusIndex(bus)];
}

} // namespace HIKARI::AUDIO
