#include "HIKARI_AudioBackend_XAudio2.h"

#include "Core/HIKARI_Logger.h"
#include "Core/Tasks/HIKARI_TaskExecutor.h"
#include "Core/Text/HIKARI_UtfPath.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <xaudio2.h>
#include <x3daudio.h>

namespace HIKARI::AUDIO {

namespace {

    // Windows N では省略可能な Media Foundation を起動時依存にしない。
    // 関数と COM オブジェクトが存続する間、モジュールを保持する。
    struct MediaFoundationApi {
        HMODULE platform = nullptr;
        HMODULE reader = nullptr;
        decltype(&MFStartup) startup = nullptr;
        decltype(&MFShutdown) shutdown = nullptr;
        decltype(&MFCreateSourceReaderFromURL) createReader = nullptr;
        decltype(&MFCreateMediaType) createType = nullptr;
        decltype(&MFCreateWaveFormatExFromMFMediaType) createWaveFormat = nullptr;

        MediaFoundationApi() {
            platform = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            reader = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!platform || !reader) return;
            startup = reinterpret_cast<decltype(startup)>(GetProcAddress(platform, "MFStartup"));
            shutdown = reinterpret_cast<decltype(shutdown)>(GetProcAddress(platform, "MFShutdown"));
            createReader = reinterpret_cast<decltype(createReader)>(GetProcAddress(reader, "MFCreateSourceReaderFromURL"));
            createType = reinterpret_cast<decltype(createType)>(GetProcAddress(platform, "MFCreateMediaType"));
            createWaveFormat = reinterpret_cast<decltype(createWaveFormat)>(GetProcAddress(platform, "MFCreateWaveFormatExFromMFMediaType"));
        }
        ~MediaFoundationApi() {
            if (reader) FreeLibrary(reader);
            if (platform) FreeLibrary(platform);
        }
        bool Ready() const {
            return startup && shutdown && createReader && createType && createWaveFormat;
        }
    };

    const MediaFoundationApi& GetMediaFoundationApi() {
        static const MediaFoundationApi api;
        return api;
    }

    constexpr size_t kMaximumDecodedSoundBytes = 128u * 1024u * 1024u;
    constexpr size_t kMaximumPreloadBytes = 128u * 1024u * 1024u;
    constexpr size_t kMaximumResidentSoundBytes = 256u * 1024u * 1024u;
    constexpr size_t kMaximumLoadedSounds = 4096u;

    std::wstring AudioCacheKey(const std::filesystem::path& source) {
        std::error_code error;
        auto absolute = std::filesystem::absolute(source, error);
        return error ? std::wstring{} : absolute.lexically_normal().make_preferred().wstring();
    }

    void ReleaseXAudio2(IXAudio2* engine)
    {
        if (engine) {
            engine->Release();
        }
    }

    using TEXT::PathFromUtf8;

} // namespace

struct AudioBackendXAudio2::SpatialState {
    X3DAUDIO_HANDLE handle{};
    uint32_t channels = 0u;
};

struct AudioBackendXAudio2::DecodeCache {
    struct Job {
        SoundData sound;
        bool requested{}, done{}, succeeded{};
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::unordered_map<std::wstring, std::shared_ptr<Job>> jobs;
    std::unordered_set<std::wstring> requested;
    std::atomic<bool> cancelled{false};
    size_t bytes{};
    // 最後に宣言して最初に破棄する。キャッシュと MF の破棄前に worker を join する。
    TASKS::TaskExecutor worker{1u};
};

AudioBackendXAudio2::AudioBackendXAudio2() = default;
AudioBackendXAudio2::~AudioBackendXAudio2() { Shutdown(); }

void AudioBackendXAudio2::PreloadDirectory(const std::filesystem::path& root) {
    if (!engine_ || root.empty() || decodeCache_) return;
    try {
        decodeCache_ = std::make_unique<DecodeCache>();
        auto* cache = decodeCache_.get();
        const bool submitted = cache->worker.Submit([this, cache, root] {
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            struct ComScope { bool active; ~ComScope() { if (active) CoUninitialize(); } } comScope{SUCCEEDED(com)};
            if (FAILED(com)) return;
            try {
                std::error_code error;
                std::filesystem::recursive_directory_iterator it(root,
                    std::filesystem::directory_options::skip_permission_denied, error), end;
                std::vector<std::filesystem::path> paths;
                for (; !error && it != end && !cache->cancelled.load(); it.increment(error)) {
                    if (!it->is_regular_file(error)) { error.clear(); continue; }
                    auto ext = it->path().extension().wstring();
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
                    if (ext == L".wav" || ext == L".mp3" || ext == L".ogg" || ext == L".flac") {
                        paths.push_back(it->path());
                        if (paths.size() >= 256u) break;
                    }
                }
                std::sort(paths.begin(), paths.end());
                for (const auto& source : paths) {
                    if (cache->cancelled.load()) break;
                    const auto key = AudioCacheKey(source);
                    if (key.empty()) continue;
                    auto job = std::make_shared<DecodeCache::Job>();
                    {
                        std::lock_guard lock(cache->mutex);
                        if (cache->bytes >= kMaximumPreloadBytes) break;
                        if (cache->requested.contains(key) || cache->jobs.contains(key)) continue;
                        cache->jobs.emplace(key, job);
                    }
                    SoundData decoded;
                    bool succeeded = false;
                    try {
                        const auto path = TEXT::PathToUtf8(source);
                        succeeded = DecodeSound(path.c_str(), decoded, &cache->cancelled);
                    } catch (...) {
                        HIKARI_LOG_WARN("[Audio][Preload] decode failed; normal Load remains available.");
                    }
                    {
                        std::lock_guard lock(cache->mutex);
                        const auto bytes = decoded.audioBytes.size() + decoded.formatBytes.size();
                        if (succeeded && !cache->cancelled.load() &&
                            (job->requested || bytes <= kMaximumPreloadBytes - (std::min)(cache->bytes, kMaximumPreloadBytes))) {
                            job->sound = std::move(decoded);
                            job->succeeded = true;
                            cache->bytes += bytes;
                        }
                        job->done = true;
                    }
                    cache->changed.notify_all();
                }
                HIKARI_LOG_INFO("[Audio][Preload] background warmup finished.");
            } catch (...) {
                HIKARI_LOG_WARN("[Audio][Preload] warmup stopped; normal Load remains available.");
            }
        });
        if (!submitted) decodeCache_.reset();
    } catch (...) {
        decodeCache_.reset();
        HIKARI_LOG_WARN("[Audio][Preload] worker unavailable; using normal Load.");
    }
}

bool AudioBackendXAudio2::Initialize()
{
    const HRESULT comResult = CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED);
    comInitialized_ = SUCCEEDED(comResult);
    const auto& media = GetMediaFoundationApi();
    const HRESULT mediaResult = media.Ready()
        ? media.startup(MF_VERSION, MFSTARTUP_LITE) : E_NOINTERFACE;
    mediaFoundationStarted_ = SUCCEEDED(mediaResult);
    if (!mediaFoundationStarted_) {
        HIKARI_LOG_WARN(
            "[Audio][XAudio2] Media Foundation is unavailable; "
            "compressed audio loading is disabled.");
    }
    IXAudio2* rawEngine = nullptr;
    const HRESULT createResult = XAudio2Create(&rawEngine, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(createResult) || !rawEngine) {
        HIKARI_LOG_ERROR("[Audio][XAudio2] XAudio2Create failed.");
        Shutdown();
        return false;
    }

    engine_ = std::unique_ptr<IXAudio2, void(*)(IXAudio2*)>(rawEngine, ReleaseXAudio2);

    const HRESULT masteringResult = engine_->CreateMasteringVoice(&masteringVoice_);
    if (FAILED(masteringResult) || !masteringVoice_) {
        HIKARI_LOG_ERROR("[Audio][XAudio2] CreateMasteringVoice failed.");
        Shutdown();
        return false;
    }

    DWORD mask = 0u;
    XAUDIO2_VOICE_DETAILS details{};
    masteringVoice_->GetVoiceDetails(&details);
    auto spatial = std::make_unique<SpatialState>();
    if (SUCCEEDED(masteringVoice_->GetChannelMask(&mask)) && details.InputChannels <= 8u &&
        SUCCEEDED(X3DAudioInitialize(mask, X3DAUDIO_SPEED_OF_SOUND, spatial->handle))) {
        spatial->channels = details.InputChannels;
        spatial_ = std::move(spatial);
    }
    HIKARI_LOG_INFO("[Audio][XAudio2] initialized.");
    return true;
}

void AudioBackendXAudio2::Shutdown()
{
    if (decodeCache_) {
        decodeCache_->cancelled.store(true);
        decodeCache_->changed.notify_all();
        decodeCache_.reset();
    }
    for (size_t i = 0; i < voices_.size(); ++i) {
        DestroyVoiceSlot(static_cast<int>(i));
    }
    voices_.clear();
    sounds_.clear();
    residentSoundBytes_ = 0;

    if (masteringVoice_) {
        masteringVoice_->DestroyVoice();
        masteringVoice_ = nullptr;
    }

    spatial_.reset();
    engine_.reset();
    if (mediaFoundationStarted_) {
        (void)GetMediaFoundationApi().shutdown();
        mediaFoundationStarted_ = false;
    }
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

bool AudioBackendXAudio2::DecodeSound(const char* path, SoundData& sound,
    const std::atomic<bool>* cancel) const {
    const auto source = PathFromUtf8(path);
    if (source.empty() || (cancel && cancel->load())) return false;
    auto extension = source.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension == L".wav" ? LoadWaveFile(path, sound, cancel)
        : mediaFoundationStarted_ && LoadMediaFile(path, sound, cancel);
}

int AudioBackendXAudio2::Load(const char* path) {
    if (!path || !path[0] || !engine_) return -1;
    if (sounds_.size() >= kMaximumLoadedSounds) {
        HIKARI_LOG_WARN("[Audio] loaded sound count budget reached (4096); new sound rejected.");
        return -1;
    }
    try {
        SoundData sound;
        bool loaded = false;
        if (decodeCache_) {
            const auto key = AudioCacheKey(PathFromUtf8(path));
            std::unique_lock lock(decodeCache_->mutex);
            decodeCache_->requested.insert(key);
            const auto found = decodeCache_->jobs.find(key);
            if (found != decodeCache_->jobs.end()) {
                const auto job = found->second;
                job->requested = true;
                if (!decodeCache_->changed.wait_for(lock, std::chrono::seconds(2),
                        [&] { return job->done || decodeCache_->cancelled.load(); })) {
                    // デコーダ異常でメインスレッドまで永久停止させない。
                    HIKARI_LOG_WARN("[Audio][Preload] pending decode exceeded Load wait budget; playback skipped.");
                    return -1;
                }
                if (job->succeeded) {
                    decodeCache_->bytes -= job->sound.audioBytes.size() + job->sound.formatBytes.size();
                    sound = std::move(job->sound);
                    loaded = true;
                }
                // wait 中の rehash に備え、iterator は保持しない。
                decodeCache_->jobs.erase(key);
            }
        }
        if (!loaded && !DecodeSound(path, sound)) return -1;
        const size_t bytes = sound.audioBytes.size() + sound.formatBytes.size();
        if (bytes > kMaximumResidentSoundBytes - residentSoundBytes_) {
            HIKARI_LOG_WARN("[Audio] resident sound budget reached (256 MiB); use streamed WAV for long audio.");
            return -1;
        }
        const int handle = static_cast<int>(sounds_.size());
        sounds_.push_back(std::move(sound));
        residentSoundBytes_ += bytes;
        return handle;
    } catch (...) {
        HIKARI_LOG_ERROR("[Audio][XAudio2] sound load failed; playback skipped.");
        return -1;
    }
}

int AudioBackendXAudio2::Play(int soundHandle, bool loop, float volume, bool paused, float startSeconds, float rate) {
    if (!engine_ || soundHandle < 0 || soundHandle >= static_cast<int>(sounds_.size())) return -1;
    CleanupFinishedVoices();
    const SoundData& sound = sounds_[static_cast<size_t>(soundHandle)];
    // 一時的な replacement voice 一つを含む。stream worker は最大 8 本。
    size_t active = 0u, streams = 0u;
    for (const auto& current : voices_) {
        active += current.active;
        streams += current.stream != nullptr;
    }
    if (active >= 129u || (sound.streaming && streams >= 8u)) return -1;
    PlayingVoice playing;
    if (sound.streaming) {
        playing.stream = std::make_unique<AudioStreamVoice>();
        if (!playing.stream->Open(*engine_, PathFromUtf8(sound.path), sound.wave, loop, startSeconds)) return -1;
        playing.voice = playing.stream->GetVoice();
    } else {
        const auto* format = reinterpret_cast<const WAVEFORMATEX*>(sound.formatBytes.data());
        if (FAILED(engine_->CreateSourceVoice(&playing.voice, format, 0u, 8.0f))) return -1;
        const double samplePosition = double(startSeconds) * format->nSamplesPerSec;
        if (samplePosition >= double(sound.audioBytes.size() / format->nBlockAlign)) {
            playing.voice->DestroyVoice(); return -1;
        }
        const auto startSample = static_cast<uint64_t>(samplePosition);
        if (startSample >= sound.audioBytes.size() / format->nBlockAlign) {
            playing.voice->DestroyVoice(); return -1;
        }
        XAUDIO2_BUFFER buffer{};
        buffer.PlayBegin = static_cast<UINT32>(startSample);
        buffer.PlayLength = static_cast<UINT32>(sound.audioBytes.size() / format->nBlockAlign - startSample);
        // Seek starts part-way through the first pass; later loops cover the
        // complete clip. Non-looping buffers must leave both loop fields zero.
        buffer.LoopLength = loop ? static_cast<UINT32>(sound.audioBytes.size() / format->nBlockAlign) : 0u;
        buffer.AudioBytes = static_cast<UINT32>(sound.audioBytes.size());
        buffer.pAudioData = sound.audioBytes.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
        if (FAILED(playing.voice->SubmitSourceBuffer(&buffer))) {
            playing.voice->DestroyVoice(); return -1;
        }
    }
    playing.voice->SetFrequencyRatio(rate);
    playing.voice->SetVolume((std::max)(0.0f, volume));
    if (!paused && FAILED(playing.voice->Start())) {
        if (!playing.stream) playing.voice->DestroyVoice();
        return -1;
    }
    playing.soundHandle = soundHandle;
    playing.active = true;
    playing.paused = paused;
    for (size_t i = 0; i < voices_.size(); ++i) {
        if (!voices_[i].active) {
            voices_[i] = std::move(playing);
            return static_cast<int>(i);
        }
    }
    try { voices_.push_back(std::move(playing)); }
    catch (...) { if (playing.voice && !playing.stream) playing.voice->DestroyVoice(); throw; }
    return static_cast<int>(voices_.size() - 1u);
}

bool AudioBackendXAudio2::SetSpatial(int handle, const GameProjectAudioListener& listener,
    const GameProjectAudioSource& source) {
    if (!spatial_ || handle < 0 || handle >= static_cast<int>(voices_.size())) return false;
    auto& playing = voices_[static_cast<size_t>(handle)];
    if (!playing.active || !playing.voice) return false;
    const auto& sound = sounds_[static_cast<size_t>(playing.soundHandle)];
    const auto* format = reinterpret_cast<const WAVEFORMATEX*>(sound.formatBytes.data());
    if (format->nChannels != 1u) return false;
    X3DAUDIO_LISTENER receiver{};
    receiver.Position = {listener.position.x, listener.position.y, listener.position.z};
    receiver.OrientFront = {listener.forward.x, listener.forward.y, listener.forward.z};
    receiver.OrientTop = {listener.up.x, listener.up.y, listener.up.z};
    X3DAUDIO_DISTANCE_CURVE_POINT points[]{{0.0f, 1.0f},
        {source.minimumDistance / source.maximumDistance, 1.0f}, {1.0f, 0.0f}};
    X3DAUDIO_DISTANCE_CURVE curve{points, 3u};
    X3DAUDIO_EMITTER emitter{};
    emitter.Position = {source.position.x, source.position.y, source.position.z};
    emitter.OrientFront = {0.0f, 0.0f, 1.0f};
    emitter.OrientTop = {0.0f, 1.0f, 0.0f};
    emitter.ChannelCount = 1u;
    emitter.CurveDistanceScaler = source.maximumDistance;
    emitter.pVolumeCurve = &curve;
    float matrix[8]{};
    X3DAUDIO_DSP_SETTINGS dsp{};
    dsp.SrcChannelCount = 1u;
    dsp.DstChannelCount = spatial_->channels;
    dsp.pMatrixCoefficients = matrix;
    X3DAudioCalculate(spatial_->handle, &receiver, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);
    return SUCCEEDED(playing.voice->SetOutputMatrix(masteringVoice_, 1u, spatial_->channels, matrix));
}
bool AudioBackendXAudio2::SetPlaybackRate(int handle, float rate) {
    if (handle < 0 || handle >= static_cast<int>(voices_.size())) return false;
    const auto& playing = voices_[static_cast<size_t>(handle)];
    return playing.active && playing.voice && SUCCEEDED(playing.voice->SetFrequencyRatio(rate));
}
GameProjectAudioStats AudioBackendXAudio2::GetStats() const {
    GameProjectAudioStats stats;
    stats.residentBytes = residentSoundBytes_;
    for (const auto& playing : voices_) {
        if (playing.stream) {
            ++stats.streamingVoices;
            stats.streamBufferBytes += playing.stream->BufferBytes();
        }
    }
    return stats;
}

void AudioBackendXAudio2::Stop(int playHandle)
{
    DestroyVoiceSlot(playHandle);
}

void AudioBackendXAudio2::Pause(int playHandle)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (!playing.active || !playing.voice) {
        return;
    }

    playing.voice->Stop();
    playing.paused = true;
}

void AudioBackendXAudio2::Resume(int playHandle)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (!playing.active || !playing.voice) {
        return;
    }

    playing.voice->Start();
    playing.paused = false;
}

void AudioBackendXAudio2::SetVolume(int playHandle, float volume)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (playing.active && playing.voice) {
        playing.voice->SetVolume(std::max(0.0f, volume));
    }
}

bool AudioBackendXAudio2::IsPlaying(int playHandle)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return false;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (!playing.active || !playing.voice) {
        return false;
    }

    XAUDIO2_VOICE_STATE state{};
    playing.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (state.BuffersQueued == 0 && !(playing.stream && playing.stream->IsFeeding())) {
        DestroyVoiceSlot(playHandle);
        return false;
    }

    return true;
}

bool AudioBackendXAudio2::LoadWaveFile(const char* path, SoundData& outSound,
    const std::atomic<bool>* cancel) const {
    AudioWaveInfo info;
    const auto source = PathFromUtf8(path);
    if ((cancel && cancel->load()) || !ReadAudioWaveInfo(source, info)) return false;
    outSound.path = path;
    outSound.formatBytes = info.format;
    constexpr uint32_t kStreamingThreshold = 4u * 1024u * 1024u;
    if (info.dataBytes >= kStreamingThreshold) {
        outSound.wave = std::move(info);
        outSound.streaming = true;
        return true;
    }
    std::ifstream file(source, std::ios::binary);
    file.seekg(static_cast<std::streamoff>(info.dataOffset));
    outSound.audioBytes.resize(info.dataBytes);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(outSound.audioBytes.data()), info.dataBytes));
}

bool AudioBackendXAudio2::LoadMediaFile(
    const char* path,
    SoundData& outSound, const std::atomic<bool>* cancel) const
{
    using Microsoft::WRL::ComPtr;
    if (path == nullptr || !mediaFoundationStarted_) {
        return false;
    }
    const std::filesystem::path source = PathFromUtf8(path);
    if (source.empty()) {
        return false;
    }
    const std::wstring widePath = source.wstring();
    ComPtr<IMFSourceReader> reader{};
    const auto& media = GetMediaFoundationApi();
    HRESULT result = media.createReader(
        widePath.c_str(),
        nullptr,
        reader.GetAddressOf());
    if (FAILED(result) || reader == nullptr) {
        HIKARI_LOG_ERROR(
            std::string("[Audio][XAudio2] media source open failed: ") +
            path);
        return false;
    }

    (void)reader->SetStreamSelection(
        static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS),
        FALSE);
    (void)reader->SetStreamSelection(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
        TRUE);
    ComPtr<IMFMediaType> nativeType{};
    result = reader->GetNativeMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
        0u,
        nativeType.GetAddressOf());
    UINT32 channels = 0u;
    UINT32 sampleRate = 0u;
    if (FAILED(result) || nativeType == nullptr ||
        FAILED(nativeType->GetUINT32(
            MF_MT_AUDIO_NUM_CHANNELS, &channels)) ||
        FAILED(nativeType->GetUINT32(
            MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate)) ||
        channels == 0u || channels > XAUDIO2_MAX_AUDIO_CHANNELS ||
        sampleRate < XAUDIO2_MIN_SAMPLE_RATE || sampleRate > XAUDIO2_MAX_SAMPLE_RATE) {
        HIKARI_LOG_ERROR(
            std::string("[Audio][XAudio2] media format query failed: ") +
            path);
        return false;
    }

    ComPtr<IMFMediaType> outputType{};
    result = media.createType(outputType.GetAddressOf());
    if (FAILED(result) || outputType == nullptr ||
        FAILED(outputType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(outputType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM)) ||
        FAILED(outputType->SetUINT32(
            MF_MT_AUDIO_NUM_CHANNELS, channels)) ||
        FAILED(outputType->SetUINT32(
            MF_MT_AUDIO_SAMPLES_PER_SECOND, sampleRate)) ||
        FAILED(outputType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16u)) ||
        FAILED(outputType->SetUINT32(
            MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2u)) ||
        FAILED(outputType->SetUINT32(
            MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
            sampleRate * channels * 2u)) ||
        FAILED(reader->SetCurrentMediaType(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
            nullptr,
            outputType.Get()))) {
        HIKARI_LOG_ERROR(
            std::string("[Audio][XAudio2] PCM negotiation failed: ") +
            path);
        return false;
    }

    ComPtr<IMFMediaType> currentType{};
    result = reader->GetCurrentMediaType(
        static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
        currentType.GetAddressOf());
    WAVEFORMATEX* waveFormat = nullptr;
    UINT32 waveFormatBytes = 0u;
    if (FAILED(result) || currentType == nullptr ||
        FAILED(media.createWaveFormat(
            currentType.Get(),
            &waveFormat,
            &waveFormatBytes,
            MFWaveFormatExConvertFlag_Normal)) ||
        waveFormat == nullptr || waveFormatBytes < sizeof(WAVEFORMATEX) ||
        waveFormatBytes > 65536u || (waveFormat->wFormatTag != WAVE_FORMAT_PCM && waveFormat->wFormatTag != WAVE_FORMAT_IEEE_FLOAT && waveFormat->wFormatTag != WAVE_FORMAT_EXTENSIBLE) ||
        waveFormat->cbSize > waveFormatBytes - sizeof(WAVEFORMATEX) ||
        (waveFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE && waveFormat->cbSize < 22u)) {
        if (waveFormat != nullptr) {
            CoTaskMemFree(waveFormat);
        }
        HIKARI_LOG_ERROR(
            std::string("[Audio][XAudio2] decoded wave format failed: ") +
            path);
        return false;
    }
    std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> waveFormatOwner(waveFormat, &CoTaskMemFree);
    std::vector<uint8_t> formatBytes(
        reinterpret_cast<uint8_t*>(waveFormat),
        reinterpret_cast<uint8_t*>(waveFormat) + waveFormatBytes);
    waveFormatOwner.reset();

    std::vector<uint8_t> audioBytes{};
    const auto started = std::chrono::steady_clock::now();
    unsigned emptySamples = 0;
    for (;;) {
        if ((cancel && cancel->load()) ||
            std::chrono::steady_clock::now() - started > std::chrono::seconds(30)) {
            HIKARI_LOG_WARN("[Audio][XAudio2] decode cancelled or timed out.");
            return false;
        }
        DWORD streamIndex = 0u;
        DWORD flags = 0u;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample{};
        result = reader->ReadSample(
            static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM),
            0u,
            &streamIndex,
            &flags,
            &timestamp,
            sample.GetAddressOf());
        (void)streamIndex;
        (void)timestamp;
        if (FAILED(result)) {
            HIKARI_LOG_ERROR(
                std::string("[Audio][XAudio2] media decode failed: ") +
                path);
            return false;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0u) {
            break;
        }
        if (sample == nullptr) {
            if (++emptySamples > 1024u) return false;
            continue;
        }
        emptySamples = 0;
        ComPtr<IMFMediaBuffer> buffer{};
        if (FAILED(sample->ConvertToContiguousBuffer(
                buffer.GetAddressOf())) ||
            buffer == nullptr) {
            return false;
        }
        BYTE* bytes = nullptr;
        DWORD maximumLength = 0u;
        DWORD currentLength = 0u;
        if (FAILED(buffer->Lock(
                &bytes,
                &maximumLength,
                &currentLength))) {
            return false;
        }
        struct BufferUnlock {
            IMFMediaBuffer* buffer;
            ~BufferUnlock() { (void)buffer->Unlock(); }
        } unlock{buffer.Get()};
        if (!bytes) return false;
        if (currentLength > maximumLength || currentLength > kMaximumDecodedSoundBytes - audioBytes.size()) {
            HIKARI_LOG_ERROR("[Audio][XAudio2] decoded sound exceeds the memory budget.");
            return false;
        }
        audioBytes.insert(
            audioBytes.end(),
            bytes,
            bytes + currentLength);
    }
    if (audioBytes.empty()) {
        HIKARI_LOG_ERROR(
            std::string("[Audio][XAudio2] decoded audio is empty: ") + path);
        return false;
    }
    outSound.path = path;
    outSound.formatBytes = std::move(formatBytes);
    outSound.audioBytes = std::move(audioBytes);
    return true;
}

void AudioBackendXAudio2::DestroyVoiceSlot(int playHandle)
{
    if (playHandle < 0 || playHandle >= static_cast<int>(voices_.size())) {
        return;
    }

    PlayingVoice& playing = voices_[static_cast<size_t>(playHandle)];
    if (playing.stream) {
        playing.stream.reset();
        playing.voice = nullptr;
    }
    if (playing.voice) {
        playing.voice->Stop();
        playing.voice->FlushSourceBuffers();
        playing.voice->DestroyVoice();
    }

    playing = PlayingVoice{};
}

void AudioBackendXAudio2::CleanupFinishedVoices()
{
    for (size_t i = 0; i < voices_.size(); ++i) {
        PlayingVoice& playing = voices_[i];
        if (!playing.active || !playing.voice || playing.paused) {
            continue;
        }

        XAUDIO2_VOICE_STATE state{};
        playing.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (state.BuffersQueued == 0 && !(playing.stream && playing.stream->IsFeeding())) {
            DestroyVoiceSlot(static_cast<int>(i));
        }
    }
}

} // namespace HIKARI::AUDIO
