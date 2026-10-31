#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <vector>

#include "Audio/HIKARI_Audio.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Tools/Tests/Internal/HIKARI_ContractAssert.h"

namespace {
void WriteSilentWave(const std::filesystem::path& path, uint32_t sampleRate, uint32_t frames) {
    const uint32_t bytes = frames * 2u;
    std::ofstream file(path, std::ios::binary);
    const auto u32 = [&](uint32_t value) { file.write(reinterpret_cast<const char*>(&value), 4); };
    const auto u16 = [&](uint16_t value) { file.write(reinterpret_cast<const char*>(&value), 2); };
    file.write("RIFF", 4);
    u32(36u + bytes);
    file.write("WAVEfmt ", 8);
    u32(16u);
    u16(1u);
    u16(1u);
    u32(sampleRate);
    u32(sampleRate * 2u);
    u16(2u);
    u16(16u);
    file.write("data", 4);
    u32(bytes);
    const std::vector<char> silence(bytes, 0);
    file.write(silence.data(), static_cast<std::streamsize>(silence.size()));
    assert(file.good());
}

void CheckVoice(const std::filesystem::path& path, float startSeconds, bool streaming) {
    using namespace HIKARI;
    using namespace std::chrono_literals;
    const int sound = AUDIO::Load(TEXT::PathToUtf8(path).c_str());
    assert(sound >= 0);
    GameProjectAudioPlayOptions options{};
    options.paused = 1u;
    options.loop = 1u;
    options.volume = 0.f;
    options.startSeconds = startSeconds;
    const int playback = AUDIO::PlayCue(sound, options, 13u);
    assert(playback >= 0 && AUDIO::BelongsToOwner(playback, 13u) && !AUDIO::BelongsToOwner(playback, 14u));
    GameProjectAudioPlaybackSnapshot snapshot{};
    assert(AUDIO::TryGetPlaybackSnapshot(playback, snapshot));
    assert(snapshot.paused == 1u && snapshot.looping == 1u && snapshot.samplesPlayed == 0u);
    assert(std::abs(snapshot.positionSeconds - startSeconds) < 0.0001);
    assert(snapshot.elapsedSeconds == 0.0 && snapshot.completedLoops == 0u && snapshot.sampleRate == 48000u);
    assert((AUDIO::GetStats().streamingVoices > 0u) == streaming);
    auto invalid = snapshot;
    invalid.structSize = 8u;
    assert(!AUDIO::TryGetPlaybackSnapshot(playback, invalid) && invalid.structSize == 8u);
    invalid = snapshot;
    invalid.version = 99u;
    assert(!AUDIO::TryGetPlaybackSnapshot(playback, invalid) && invalid.version == 99u);
    AUDIO::Resume(playback);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    do {
        std::this_thread::sleep_for(10ms);
        assert(AUDIO::TryGetPlaybackSnapshot(playback, snapshot));
    } while (snapshot.completedLoops == 0u && std::chrono::steady_clock::now() < deadline);
    assert(snapshot.completedLoops > 0u && snapshot.paused == 0u && snapshot.samplesPlayed > 0u);
    assert(snapshot.positionSeconds >= 0.0 && snapshot.positionSeconds < snapshot.durationSeconds);
    assert(std::abs(snapshot.elapsedSeconds - double(snapshot.samplesPlayed) / snapshot.sampleRate) < 1e-9);
    assert(std::abs(startSeconds + snapshot.elapsedSeconds -
                    (double(snapshot.completedLoops) * snapshot.durationSeconds + snapshot.positionSeconds)) < 0.0001);
    AUDIO::Pause(playback);
    std::this_thread::sleep_for(50ms); // Allow the audio processing quantum to apply Stop.
    assert(AUDIO::TryGetPlaybackSnapshot(playback, snapshot) && snapshot.paused == 1u);
    const uint64_t pausedSamples = snapshot.samplesPlayed;
    std::this_thread::sleep_for(60ms);
    assert(AUDIO::TryGetPlaybackSnapshot(playback, snapshot) && snapshot.samplesPlayed == pausedSamples);
    assert(AUDIO::SetPlaybackRate(playback, 1.5f));
    AUDIO::Resume(playback);
    std::this_thread::sleep_for(80ms);
    assert(AUDIO::TryGetPlaybackSnapshot(playback, snapshot));
    assert(snapshot.samplesPlayed > pausedSamples && snapshot.playbackRate == 1.5f);
    AUDIO::StopOwner(13u);
    const auto before = snapshot.samplesPlayed;
    assert(!AUDIO::TryGetPlaybackSnapshot(playback, snapshot) && snapshot.samplesPlayed == before);

    options.loop = 0u;
    options.paused = 0u;
    const int finite = AUDIO::PlayCue(sound, options, 13u);
    assert(finite >= 0 && finite != playback);
    const auto finishDeadline = std::chrono::steady_clock::now() + 3s;
    while (AUDIO::IsPlaying(finite) && std::chrono::steady_clock::now() < finishDeadline)
        std::this_thread::sleep_for(10ms);
    assert(!AUDIO::IsPlaying(finite));
    assert(!AUDIO::TryGetPlaybackSnapshot(finite, snapshot) && snapshot.samplesPlayed == before);
    assert(!AUDIO::TryGetPlaybackSnapshot(playback, snapshot));
}
} // namespace

void CheckAudioPlaybackContracts() {
    using namespace HIKARI;
    const auto root = std::filesystem::path(HIKARI_CONTRACT_OUTPUT_ROOT) / "audio-playback-contracts";
    std::filesystem::create_directories(root);
    WriteSilentWave(root / "resident.wav", 48000u, 12000u);
    WriteSilentWave(root / "stream.wav", 48000u, 48u * 48000u); // Above the 4 MiB streaming threshold.
    assert(AUDIO::Initialize());
    CheckVoice(root / "resident.wav", 0.125f, false);
    CheckVoice(root / "stream.wav", 47.875f, true);
    AUDIO::Shutdown();
    assert(AUDIO::Initialize(AUDIO::BackendType::Null));
    GameProjectAudioPlaybackSnapshot snapshot{};
    snapshot.positionSeconds = 37.0;
    assert(!AUDIO::TryGetPlaybackSnapshot(-1, snapshot) && snapshot.positionSeconds == 37.0);
    AUDIO::Shutdown();
    std::cout << "Audio playback: resident/stream seek, loop, pause/resume, rate, end and invalid snapshots passed.\n";
}
