#include "HIKARI_Audio.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>

#include "HIKARI_IAudioBackend.h"
#include "Backends/HIKARI_AudioBackend_XAudio2.h"

namespace HIKARI::AUDIO {

static std::unique_ptr<IAudioBackend> g_backend{};

namespace {

struct PlayingAudioRecord {
    int backend = -1;
    int sound = -1;
    uint64_t owner = 0u;
    GameProjectAudioPlayOptions options{};
};

std::array<float, 3u> g_busVolumes{1.0f, 1.0f, 1.0f};
std::unordered_map<int, PlayingAudioRecord> g_playing{};
std::unordered_map<std::string, int> g_loaded{};
std::unordered_map<int, int> g_sounds{};
GameProjectAudioListener g_listener{};
bool g_cameraListener = true;
GameProjectAudioStats g_counters{};
// シャットダウン後も番号を再利用しない。古い SDK handle が別音声へ化けない。
int g_nextSound = 0;
int g_nextPlayback = 0;
constexpr size_t kMaximumVoices = 128u;
constexpr size_t kMaximumSounds = 4096u;

size_t BusIndex(AudioBus bus) noexcept {
    switch (bus) {
    case AudioBus::Bgm: return 0u;
    case AudioBus::Voice: return 2u;
    case AudioBus::Se:
    default: return 1u;
    }
}

float SafeVolume(float value) noexcept { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 1.0f; }

class NullAudioBackend final : public IAudioBackend {
public:
    bool Initialize() override { return true; }
    void Shutdown() override {}

    int Load(const char*) override { return -1; }
    int Play(int, bool, float, bool, float, float) override { return -1; }
    void Stop(int) override {}
    void Pause(int) override {}
    void Resume(int) override {}
    void SetVolume(int, float) override {}
    bool IsPlaying(int) override { return false; }
};

std::unique_ptr<IAudioBackend> CreateBackend(BackendType type) {
    switch (type) {
    case BackendType::Null: return std::make_unique<NullAudioBackend>();
    case BackendType::XAudio2:
    default: return std::make_unique<AudioBackendXAudio2>();
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
    g_sounds.clear();
    g_listener = {};
    g_cameraListener = true;
    g_counters = {};
    g_busVolumes.fill(1.0f);
}

void PreloadDirectory(const std::filesystem::path& root) {
    if (g_backend)
        g_backend->PreloadDirectory(root);
}

int Load(const char* path) {
    if (!g_backend || !path || !path[0])
        return -1;
    const auto found = g_loaded.find(path);
    if (found != g_loaded.end())
        return found->second;
    if (g_sounds.size() >= kMaximumSounds || g_nextSound == (std::numeric_limits<int>::max)())
        return -1;
    const int backend = g_backend->Load(path);
    if (backend < 0)
        return -1;
    const int handle = g_nextSound++;
    g_sounds.emplace(handle, backend);
    g_loaded.emplace(path, handle);
    return handle;
}

namespace {
void CollectFinished() {
    if (!g_backend)
        return;
    std::erase_if(g_playing, [](const auto& record) { return !g_backend->IsPlaying(record.second.backend); });
}
float MixVolume(const GameProjectAudioPlayOptions& options) {
    return options.volume * g_busVolumes[BusIndex(static_cast<AudioBus>(options.bus))];
}
bool Valid(const GameProjectAudioSource& source) {
    const auto& p = source.position;
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(source.minimumDistance) &&
           std::isfinite(source.maximumDistance) && source.minimumDistance > 0.0f &&
           source.maximumDistance > source.minimumDistance;
}
bool SameGroup(const PlayingAudioRecord& record, int sound, uint64_t owner,
               const GameProjectAudioPlayOptions& options) {
    return record.owner == owner && record.options.group == options.group &&
           (options.group != 0u || record.sound == sound);
}
} // namespace

int Play(int soundHandle, bool loop, float volume) { return PlayOnBus(soundHandle, loop, volume, AudioBus::Se); }
int PlayOnBus(int soundHandle, bool loop, float volume, AudioBus bus) {
    GameProjectAudioPlayOptions options;
    options.loop = loop ? 1u : 0u;
    options.volume = SafeVolume(volume);
    options.bus = static_cast<GameProjectAudioBus>(bus);
    return PlayCue(soundHandle, options);
}
int PlayCue(int sound, const GameProjectAudioPlayOptions& options, uint64_t owner) {
    const auto reject = [] {
        ++g_counters.rejectedPlays;
        return -1;
    };
    const auto loaded = g_sounds.find(sound);
    if (!g_backend || loaded == g_sounds.end() || options.structSize != sizeof(options) || options.version != 1u ||
        options.bus > GameProjectAudioBus::Voice || options.loop > 1u || options.spatial > 1u ||
        options.concurrency > GameProjectAudioConcurrency::ReplaceOldest || !std::isfinite(options.volume) ||
        options.volume < 0.0f || options.volume > 1.0f || options.paused > 1u || !std::isfinite(options.startSeconds) ||
        options.startSeconds < 0.0f || !std::isfinite(options.playbackRate) || options.playbackRate < 0.01f ||
        options.playbackRate > 8.0f || options.maximumInstances == 0u || options.maximumInstances > kMaximumVoices ||
        (options.spatial && !Valid(options.source)) || g_nextPlayback == (std::numeric_limits<int>::max)())
        return reject();
    CollectFinished();
    size_t groupCount = 0u;
    int oldest = -1;
    int lowest = -1;
    for (const auto& [handle, record] : g_playing) {
        if (SameGroup(record, sound, owner, options)) {
            ++groupCount;
            if (oldest < 0 || handle < oldest)
                oldest = handle;
        }
        if (lowest < 0 || record.options.priority < g_playing.at(lowest).options.priority ||
            (record.options.priority == g_playing.at(lowest).options.priority && handle < lowest))
            lowest = handle;
    }
    int victim = -1;
    if (groupCount >= options.maximumInstances) {
        if (options.concurrency == GameProjectAudioConcurrency::RejectNew)
            return reject();
        victim = oldest;
    } else if (g_playing.size() >= kMaximumVoices) {
        if (lowest < 0 || options.priority <= g_playing.at(lowest).options.priority)
            return reject();
        victim = lowest;
    }
    // 空間行列の設定と検証が終わるまで voice を開始しない。
    const int backend = g_backend->Play(loaded->second, options.loop != 0u, MixVolume(options), true,
                                        options.startSeconds, options.playbackRate);
    if (backend < 0)
        return reject();
    if (options.spatial && !g_backend->SetSpatial(backend, g_listener, options.source)) {
        g_backend->Stop(backend);
        return reject();
    }
    const int handle = g_nextPlayback++;
    try {
        g_playing.emplace(handle, PlayingAudioRecord{backend, sound, owner, options});
    } catch (...) {
        g_backend->Stop(backend);
        throw;
    }
    if (victim >= 0) {
        Stop(victim);
        ++g_counters.stolenVoices;
    }
    if (!options.paused)
        g_backend->Resume(backend);
    return handle;
}
void Stop(int handle) {
    const auto found = g_playing.find(handle);
    if (found == g_playing.end())
        return;
    if (g_backend)
        g_backend->Stop(found->second.backend);
    g_playing.erase(found);
}
void Pause(int handle) {
    const auto found = g_playing.find(handle);
    if (g_backend && found != g_playing.end() && !found->second.options.paused) {
        g_backend->Pause(found->second.backend);
        found->second.options.paused = 1u;
    }
}
void Resume(int handle) {
    const auto found = g_playing.find(handle);
    if (g_backend && found != g_playing.end() && found->second.options.paused) {
        g_backend->Resume(found->second.backend);
        found->second.options.paused = 0u;
    }
}
void SetVolume(int handle, float volume) {
    const auto found = g_playing.find(handle);
    if (!g_backend || found == g_playing.end())
        return;
    found->second.options.volume = SafeVolume(volume);
    g_backend->SetVolume(found->second.backend, MixVolume(found->second.options));
}
bool IsPlaying(int handle) {
    const auto found = g_playing.find(handle);
    if (found == g_playing.end())
        return false;
    if (g_backend && g_backend->IsPlaying(found->second.backend))
        return true;
    g_playing.erase(found);
    return false;
}
bool TryGetPlaybackSnapshot(int handle, GameProjectAudioPlaybackSnapshot& outSnapshot) {
    const auto found = g_playing.find(handle);
    if (!g_backend || found == g_playing.end() || outSnapshot.structSize != sizeof(outSnapshot) ||
        outSnapshot.version != 1u)
        return false;
    GameProjectAudioPlaybackSnapshot snapshot;
    if (!g_backend->TryGetPlaybackSnapshot(found->second.backend, snapshot))
        return false;
    outSnapshot = snapshot;
    return true;
}
void SetBusVolume(AudioBus bus, float volume) {
    g_busVolumes[BusIndex(bus)] = SafeVolume(volume);
    CollectFinished();
    for (const auto& [handle, record] : g_playing) {
        (void)handle;
        if (record.options.bus == static_cast<GameProjectAudioBus>(bus))
            g_backend->SetVolume(record.backend, MixVolume(record.options));
    }
}
float GetBusVolume(AudioBus bus) { return g_busVolumes[BusIndex(bus)]; }

bool SetListener(const GameProjectAudioListener& listener) {
    const auto finite = [](GameProjectFloat3 p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    };
    const auto dot = [](GameProjectFloat3 a, GameProjectFloat3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
    if (!finite(listener.position) || !finite(listener.forward) || !finite(listener.up) ||
        std::abs(dot(listener.forward, listener.forward) - 1.0f) > 0.001f ||
        std::abs(dot(listener.up, listener.up) - 1.0f) > 0.001f ||
        std::abs(dot(listener.forward, listener.up)) > 0.001f)
        return false;
    g_cameraListener = false;
    g_listener = listener;
    CollectFinished();
    bool success = true;
    for (const auto& [handle, record] : g_playing) {
        (void)handle;
        if (record.options.spatial)
            success = g_backend->SetSpatial(record.backend, listener, record.options.source) && success;
    }
    return success;
}
void UseCameraListener() { g_cameraListener = true; }
void UpdateCameraListener(const GameProjectAudioListener& listener) {
    if (!g_cameraListener)
        return;
    (void)SetListener(listener);
    g_cameraListener = true;
}
bool SetSource(int handle, const GameProjectAudioSource& source) {
    const auto found = g_playing.find(handle);
    if (!g_backend || found == g_playing.end() || !found->second.options.spatial || !Valid(source))
        return false;
    if (!g_backend->SetSpatial(found->second.backend, g_listener, source))
        return false;
    found->second.options.source = source;
    return true;
}
bool SetPlaybackRate(int handle, float rate) {
    const auto found = g_playing.find(handle);
    if (!g_backend || found == g_playing.end() || !std::isfinite(rate) || rate < 0.01f || rate > 8.0f)
        return false;
    if (found->second.options.playbackRate == rate)
        return true;
    if (!g_backend->SetPlaybackRate(found->second.backend, rate))
        return false;
    found->second.options.playbackRate = rate;
    return true;
}
float GetSoundSeconds(int soundHandle) {
    const auto found = g_sounds.find(soundHandle);
    return g_backend && found != g_sounds.end() ? g_backend->GetSoundSeconds(found->second) : 0.0f;
}
void StopOwner(uint64_t owner) {
    for (auto it = g_playing.begin(); it != g_playing.end();) {
        if (it->second.owner != owner) {
            ++it;
            continue;
        }
        if (g_backend)
            g_backend->Stop(it->second.backend);
        it = g_playing.erase(it);
    }
}
bool BelongsToOwner(int handle, uint64_t owner) {
    const auto found = g_playing.find(handle);
    return found != g_playing.end() && found->second.owner == owner;
}
GameProjectAudioStats GetStats() {
    CollectFinished();
    auto result = g_backend ? g_backend->GetStats() : GameProjectAudioStats{};
    result.activeVoices = static_cast<uint32_t>(g_playing.size());
    for (const auto& [handle, record] : g_playing) {
        (void)handle;
        result.spatialVoices += record.options.spatial != 0u;
        result.pausedVoices += record.options.paused != 0u;
    }
    result.rejectedPlays = g_counters.rejectedPlays;
    result.stolenVoices = g_counters.stolenVoices;
    return result;
}
} // namespace HIKARI::AUDIO
