#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

struct ID3D12GraphicsCommandList;

namespace HIKARI::GFX::PIX {

    // 色は ARGB 形式で、Alpha は不透明度ではなく、PIX 内でのイベントの種類を表すために使用される。色の選択は任意で、イベントの識別に役立つようにする。
    constexpr uint64_t kColorFrame = 0xFF4BA3FFull;
    constexpr uint64_t kColorRender = 0xFF67D391ull;
    constexpr uint64_t kColorPost = 0xFFFFB454ull;
    constexpr uint64_t kColorEditor = 0xFFC792EAull;
    constexpr uint64_t kColorUpload = 0xFFFF6B6Bull;

    bool IsCompiledIn();
    bool IsCapturerLoaded();
    bool AreEventMarkersEnabled();
    bool HasLastCapture();

    const std::filesystem::path& GetLastCapturePath();
    const std::string& GetLastStatusMessage();

    void Initialize(void* hwnd);
    void Shutdown();
    void Update();
    void SetEventMarkersEnabled(bool enabled);

    bool CaptureNextFrames(uint32_t frameCount = 1, bool openWhenReady = true);
    bool OpenLastCaptureInPix();

    void BeginCpuEvent(uint64_t color, const char* name);
    void EndCpuEvent();
    void SetCpuMarker(uint64_t color, const char* name);

    void BeginGpuEvent(ID3D12GraphicsCommandList* cmd, uint64_t color, const char* name);
    void EndGpuEvent(ID3D12GraphicsCommandList* cmd);
    void SetGpuMarker(ID3D12GraphicsCommandList* cmd, uint64_t color, const char* name);

    class ScopedCpuEvent {
    public:
        ScopedCpuEvent(uint64_t color, const char* name);
        ~ScopedCpuEvent();

        ScopedCpuEvent(const ScopedCpuEvent&) = delete;
        ScopedCpuEvent& operator=(const ScopedCpuEvent&) = delete;

    private:
        bool active_ = false;
    };

    class ScopedGpuEvent {
    public:
        ScopedGpuEvent(ID3D12GraphicsCommandList* cmd, uint64_t color, const char* name);
        ~ScopedGpuEvent();

        ScopedGpuEvent(const ScopedGpuEvent&) = delete;
        ScopedGpuEvent& operator=(const ScopedGpuEvent&) = delete;

    private:
        ID3D12GraphicsCommandList* cmd_ = nullptr;
        bool active_ = false;
    };

} // namespace HIKARI::GFX::PIX
