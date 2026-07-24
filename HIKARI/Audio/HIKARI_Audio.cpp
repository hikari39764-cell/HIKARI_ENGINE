#include "HIKARI_Audio.h"

#include "HIKARI_IAudioBackend.h"
#include "Backends/HIKARI_AudioBackend_XAudio2.h"

namespace HIKARI::AUDIO {

static std::unique_ptr<IAudioBackend> g_backend{};

namespace {

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
}

int Load(const char* path) { return g_backend ? g_backend->Load(path) : -1; }
int Play(int soundHandle, bool loop, float volume) { return g_backend ? g_backend->Play(soundHandle, loop, volume) : -1; }
void Stop(int playHandle) { if (g_backend) g_backend->Stop(playHandle); }
void Pause(int playHandle) { if (g_backend) g_backend->Pause(playHandle); }
void Resume(int playHandle) { if (g_backend) g_backend->Resume(playHandle); }
void SetVolume(int playHandle, float volume) { if (g_backend) g_backend->SetVolume(playHandle, volume); }
bool IsPlaying(int playHandle) { return g_backend ? g_backend->IsPlaying(playHandle) : false; }

} // namespace HIKARI::AUDIO
