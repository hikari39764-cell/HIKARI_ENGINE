#include "HIKARI_PixProfiler.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

#include <Windows.h>
#include <d3d12.h>

#include "Core/HIKARI_Logger.h"

#if defined(HIKARI_ENABLE_PIX)
#ifndef USE_PIX
#define USE_PIX
#endif
#include "pix3.h"
#endif

namespace HIKARI::GFX::PIX {
    namespace {
        struct PixState {
            HWND hwnd = nullptr;
            bool capturerLoadAttempted = false;
            bool capturerLoaded = false;
            bool eventMarkersEnabled = false;
            bool eventMarkerApiFailed = false;
            uint32_t eventMarkerFailureCount = 0;
            bool emitEvents = false;
            uint32_t emitEventFramesRemaining = 0;
            bool autoOpenPending = false;
            uint32_t autoOpenPollFramesRemaining = 0;
            uint32_t autoOpenStableFrameCount = 0;
            std::uintmax_t autoOpenLastFileSize = 0;
            std::filesystem::path lastCapturePath{};
            std::string lastStatusMessage = "PIX disabled.";
        };

        PixState gPix{};

        constexpr uint32_t kAutoOpenStableFrameThreshold = 90;
        constexpr std::uintmax_t kMinimumLikelyValidCaptureSize = 4096;

        std::filesystem::path MakeDefaultCaptureDirectory() {
            return std::filesystem::current_path() / "Library" / "PIX";
        }

        std::filesystem::path MakeCapturePath() {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm localTime{};
            localtime_s(&localTime, &time);

            std::wostringstream name;
            name << L"HIKARI_"
                 << std::put_time(&localTime, L"%Y%m%d_%H%M%S")
                 << L".wpix";

            return MakeDefaultCaptureDirectory() / name.str();
        }

        void SetStatus(std::string message) {
            gPix.lastStatusMessage = std::move(message);
        }

        bool ShouldEmitPixEvents() {
            return gPix.capturerLoaded && gPix.eventMarkersEnabled && !gPix.eventMarkerApiFailed && gPix.emitEvents;
        }

        void StopPixEventEmission() {
            gPix.emitEvents = false;
            gPix.emitEventFramesRemaining = 0;
        }

        void ArmPixEventEmission(uint32_t frameCount) {
            if (gPix.eventMarkerApiFailed) {
                StopPixEventEmission();
                return;
            }
            if (!gPix.eventMarkersEnabled) {
                StopPixEventEmission();
                return;
            }

            gPix.emitEvents = true;
            gPix.emitEventFramesRemaining = frameCount;
        }

        void DisablePixEventMarkersAfterException(const char* apiName) {
            ++gPix.eventMarkerFailureCount;
            gPix.eventMarkersEnabled = false;
            gPix.eventMarkerApiFailed = true;
            StopPixEventEmission();

            std::string message = "PIX event markers disabled after PIX runtime exception";
            if (apiName != nullptr && apiName[0] != '\0') {
                message += " in ";
                message += apiName;
            }
            message += ". Capture files still work.";

            SetStatus(message);
            HIKARI_LOG_WARN("[PIX] " + message);
        }

#if defined(HIKARI_ENABLE_PIX)
        template <typename Callback>
        bool InvokePixEventApi(const char* apiName, Callback&& callback) {
            if (!ShouldEmitPixEvents()) {
                return false;
            }

            try {
                callback();
                return true;
            } catch (...) {
                // PIX Runtime 側の例外が毎フレーム流れないように、マーカーだけを即座に止める。
                DisablePixEventMarkersAfterException(apiName);
                return false;
            }
        }
#endif

#if defined(HIKARI_ENABLE_PIX)
        bool IsDeferredCapturerLoadRequested() {
            wchar_t value[16]{};
            const DWORD length = GetEnvironmentVariableW(
                L"HIKARI_PIX_DEFER_LOAD",
                value,
                static_cast<DWORD>(sizeof(value) / sizeof(value[0])));
            return length == 1 && value[0] == L'1';
        }

        bool TryGetCaptureFileSize(const std::filesystem::path& path, std::uintmax_t& outSize) {
            std::error_code ec{};
            if (!std::filesystem::exists(path, ec) || ec) {
                return false;
            }

            const std::uintmax_t size = std::filesystem::file_size(path, ec);
            if (ec || size == 0) {
                return false;
            }

            outSize = size;
            return true;
        }

        bool CanOpenCaptureForRead(const std::filesystem::path& path) {
            const HANDLE file = CreateFileW(
                path.wstring().c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);

            if (file == INVALID_HANDLE_VALUE) {
                return false;
            }

            CloseHandle(file);
            return true;
        }

        bool CanOpenCaptureExclusivelyForRead(const std::filesystem::path& path) {
            // PIX UI may need the capture after the GPU capturer fully releases it.
            const HANDLE file = CreateFileW(
                path.wstring().c_str(),
                GENERIC_READ,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);

            if (file == INVALID_HANDLE_VALUE) {
                return false;
            }

            CloseHandle(file);
            return true;
        }

        bool IsCaptureFileReadableNow(const std::filesystem::path& path) {
            std::uintmax_t currentSize = 0;
            return TryGetCaptureFileSize(path, currentSize) && CanOpenCaptureForRead(path);
        }

        bool IsCaptureFileLikelyValid(const std::filesystem::path& path, std::uintmax_t& outSize) {
            if (!TryGetCaptureFileSize(path, outSize)) {
                return false;
            }

            return outSize >= kMinimumLikelyValidCaptureSize;
        }

        bool IsCaptureFileReadyToAutoOpen(const std::filesystem::path& path) {
            std::uintmax_t currentSize = 0;
            if (!TryGetCaptureFileSize(path, currentSize)) {
                gPix.autoOpenStableFrameCount = 0;
                gPix.autoOpenLastFileSize = 0;
                return false;
            }

            if (currentSize == gPix.autoOpenLastFileSize) {
                ++gPix.autoOpenStableFrameCount;
            } else {
                gPix.autoOpenLastFileSize = currentSize;
                gPix.autoOpenStableFrameCount = 0;
                return false;
            }

            if (gPix.autoOpenStableFrameCount < kAutoOpenStableFrameThreshold) {
                return false;
            }

            if (currentSize < kMinimumLikelyValidCaptureSize) {
                SetStatus("PIX capture file is too small. Restart and capture again.");
                gPix.autoOpenPending = false;
                return false;
            }

            return CanOpenCaptureExclusivelyForRead(path);
        }

        bool OpenCapturePathInPixUi(const std::filesystem::path& path) {
            const HINSTANCE result = PIXOpenCaptureInUI(path.wstring().c_str());
            const auto value = reinterpret_cast<intptr_t>(result);
            return value > 32;
        }

        bool EnsureGpuCapturerLoaded() {
            if (gPix.capturerLoaded) {
                return true;
            }
            if (gPix.capturerLoadAttempted) {
                return false;
            }

            gPix.capturerLoadAttempted = true;
            const HMODULE capturer = PIXLoadLatestWinPixGpuCapturerLibrary();
            if (capturer == nullptr) {
                SetStatus("PIX GPU capturer was not found. Install Microsoft PIX or open the app from PIX.");
                HIKARI_LOG_WARN("[PIX] WinPixGpuCapturer.dll was not found.");
                return false;
            }

            gPix.capturerLoaded = true;
            if (gPix.hwnd != nullptr) {
                PIXSetTargetWindow(gPix.hwnd);
            }
            const HRESULT hudResult = PIXSetHUDOptions(
                PIX_HUD_SHOW_ON_NO_WINDOWS);
            if (FAILED(hudResult)) {
                std::ostringstream oss;
                oss << "[PIX] failed to hide capturer HUD. hr=0x"
                    << std::hex
                    << static_cast<unsigned long>(hudResult);
                HIKARI_LOG_WARN(oss.str());
            }

            SetStatus("PIX GPU capturer loaded.");
            HIKARI_LOG_INFO("[PIX] GPU capturer loaded.");
            return true;
        }
#endif
    }

    bool IsCompiledIn() {
#if defined(HIKARI_ENABLE_PIX)
        return true;
#else
        return false;
#endif
    }

    bool IsCapturerLoaded() {
        return gPix.capturerLoaded;
    }

    bool AreEventMarkersEnabled() {
        return gPix.eventMarkersEnabled;
    }

    bool HasLastCapture() {
        return !gPix.lastCapturePath.empty();
    }

    const std::filesystem::path& GetLastCapturePath() {
        return gPix.lastCapturePath;
    }

    const std::string& GetLastStatusMessage() {
        return gPix.lastStatusMessage;
    }

    void Initialize(void* hwnd) {
        gPix.hwnd = static_cast<HWND>(hwnd);
#if defined(HIKARI_ENABLE_PIX)
        // GPU CaptureはD3D12 Device作成前にCapturerを読み込む必要がある。
        if (IsDeferredCapturerLoadRequested()) {
            SetStatus("PIX compiled in. GPU capturer load is deferred by HIKARI_PIX_DEFER_LOAD=1.");
            HIKARI_LOG_WARN("[PIX] GPU capturer load deferred. GPU captures may not include D3D12 work.");
            return;
        }

        if (EnsureGpuCapturerLoaded()) {
            SetStatus("PIX GPU capturer ready before D3D12 initialization.");
        }
#else
        SetStatus("PIX disabled in this build.");
#endif
    }

    void Shutdown() {
        StopPixEventEmission();
        gPix.autoOpenPending = false;
        gPix.autoOpenPollFramesRemaining = 0;
        gPix.autoOpenStableFrameCount = 0;
        gPix.autoOpenLastFileSize = 0;
    }

    void Update() {
#if defined(HIKARI_ENABLE_PIX)
        if (!gPix.autoOpenPending) {
            if (gPix.emitEventFramesRemaining > 0) {
                --gPix.emitEventFramesRemaining;
                if (gPix.emitEventFramesRemaining == 0) {
                    StopPixEventEmission();
                }
            }
            return;
        }

        if (gPix.emitEventFramesRemaining > 0) {
            --gPix.emitEventFramesRemaining;
            if (gPix.emitEventFramesRemaining == 0) {
                StopPixEventEmission();
            }
        }

        if (gPix.autoOpenPollFramesRemaining > 0) {
            if (!gPix.lastCapturePath.empty() && IsCaptureFileReadyToAutoOpen(gPix.lastCapturePath)) {
                if (OpenCapturePathInPixUi(gPix.lastCapturePath)) {
                    gPix.autoOpenPending = false;
                    StopPixEventEmission();
                    SetStatus("PIX capture opened: " + gPix.lastCapturePath.generic_string());
                    return;
                }

                gPix.autoOpenPending = false;
                StopPixEventEmission();
                SetStatus("PIX capture is ready, but PIX UI could not open it.");
                return;
            }

            --gPix.autoOpenPollFramesRemaining;
            return;
        }

        gPix.autoOpenPending = false;
        StopPixEventEmission();
        SetStatus("PIX capture is not ready yet. Use Open Last PIX Capture after it finishes.");
#endif
    }

    void SetEventMarkersEnabled(bool enabled) {
        gPix.eventMarkersEnabled = enabled;
        if (enabled) {
            gPix.eventMarkerApiFailed = false;
            gPix.eventMarkerFailureCount = 0;
        }
        if (!enabled) {
            StopPixEventEmission();
            SetStatus("PIX event markers disabled. Captures still work without per-frame markers.");
            return;
        }

#if defined(HIKARI_ENABLE_PIX)
        SetStatus("PIX event markers enabled for the next capture.");
#else
        SetStatus("PIX is not enabled in this build.");
#endif
    }

    bool CaptureNextFrames(uint32_t frameCount, bool openWhenReady) {
#if defined(HIKARI_ENABLE_PIX)
        if (frameCount == 0) {
            frameCount = 1;
        }
        if (!EnsureGpuCapturerLoaded()) {
            return false;
        }

        std::error_code ec{};
        const std::filesystem::path captureDirectory = MakeDefaultCaptureDirectory();
        std::filesystem::create_directories(captureDirectory, ec);
        if (ec) {
            SetStatus("Failed to create PIX capture directory: " + ec.message());
            HIKARI_LOG_WARN("[PIX] failed to create capture directory: " + ec.message());
            return false;
        }

        gPix.lastCapturePath = MakeCapturePath();
        const HRESULT hr = PIXGpuCaptureNextFrames(gPix.lastCapturePath.wstring().c_str(), frameCount);
        if (FAILED(hr)) {
            std::ostringstream oss;
            oss << "PIX capture request failed. hr=0x" << std::hex << static_cast<unsigned long>(hr);
            SetStatus(oss.str());
            HIKARI_LOG_WARN("[PIX] " + oss.str());
            return false;
        }

        gPix.autoOpenPending = openWhenReady;
        gPix.autoOpenPollFramesRemaining = frameCount + 600;
        gPix.autoOpenStableFrameCount = 0;
        gPix.autoOpenLastFileSize = 0;
        // ImGuiのCaptureボタンは現在フレームの後半で押されるため、
        // PIXが取得する次フレームまでイベント発行を保持する。
        const uint32_t markerFrameCount =
            frameCount < UINT32_MAX ? frameCount + 1u : frameCount;
        ArmPixEventEmission(markerFrameCount);
        const std::string markerMode = gPix.eventMarkersEnabled ? " eventMarkers=on" : " eventMarkers=off";
        SetStatus("PIX capture requested: " + gPix.lastCapturePath.generic_string() + markerMode);
        HIKARI_LOG_INFO("[PIX] capture requested: " + gPix.lastCapturePath.generic_string() + markerMode);
        return true;
#else
        (void)frameCount;
        (void)openWhenReady;
        SetStatus("PIX is not enabled in this build.");
        return false;
#endif
    }

    bool OpenLastCaptureInPix() {
#if defined(HIKARI_ENABLE_PIX)
        if (gPix.lastCapturePath.empty()) {
            SetStatus("No PIX capture file has been requested yet.");
            return false;
        }

        std::uintmax_t size = 0;
        if (!IsCaptureFileLikelyValid(gPix.lastCapturePath, size)) {
            std::ostringstream oss;
            oss << "PIX capture file is too small (" << size << " bytes). Restart and capture again.";
            SetStatus(oss.str());
            return false;
        }

        if (!IsCaptureFileReadableNow(gPix.lastCapturePath) ||
            !CanOpenCaptureExclusivelyForRead(gPix.lastCapturePath)) {
            SetStatus("PIX capture file is still being written. Try again shortly.");
            return false;
        }

        if (!OpenCapturePathInPixUi(gPix.lastCapturePath)) {
            SetStatus("Failed to open PIX capture file.");
            return false;
        }

        SetStatus("Opened PIX capture: " + gPix.lastCapturePath.generic_string());
        return true;
#else
        SetStatus("PIX is not enabled in this build.");
        return false;
#endif
    }

    void BeginCpuEvent(uint64_t color, const char* name) {
#if defined(HIKARI_ENABLE_PIX)
        InvokePixEventApi("PIXBeginEvent(cpu)", [&]() {
            PIXBeginEvent(color, name ? name : "<unnamed>");
        });
#else
        (void)color;
        (void)name;
#endif
    }

    void EndCpuEvent() {
#if defined(HIKARI_ENABLE_PIX)
        InvokePixEventApi("PIXEndEvent(cpu)", [&]() {
            PIXEndEvent();
        });
#endif
    }

    void SetCpuMarker(uint64_t color, const char* name) {
#if defined(HIKARI_ENABLE_PIX)
        InvokePixEventApi("PIXSetMarker(cpu)", [&]() {
            PIXSetMarker(color, name ? name : "<unnamed>");
        });
#else
        (void)color;
        (void)name;
#endif
    }

    void BeginGpuEvent(ID3D12GraphicsCommandList* cmd, uint64_t color, const char* name) {
#if defined(HIKARI_ENABLE_PIX)
        if (cmd != nullptr) {
            InvokePixEventApi("PIXBeginEvent(gpu)", [&]() {
                PIXBeginEvent(cmd, color, name ? name : "<unnamed>");
            });
        }
#else
        (void)cmd;
        (void)color;
        (void)name;
#endif
    }

    void EndGpuEvent(ID3D12GraphicsCommandList* cmd) {
#if defined(HIKARI_ENABLE_PIX)
        if (cmd != nullptr) {
            InvokePixEventApi("PIXEndEvent(gpu)", [&]() {
                PIXEndEvent(cmd);
            });
        }
#else
        (void)cmd;
#endif
    }

    void SetGpuMarker(ID3D12GraphicsCommandList* cmd, uint64_t color, const char* name) {
#if defined(HIKARI_ENABLE_PIX)
        if (cmd != nullptr) {
            InvokePixEventApi("PIXSetMarker(gpu)", [&]() {
                PIXSetMarker(cmd, color, name ? name : "<unnamed>");
            });
        }
#else
        (void)cmd;
        (void)color;
        (void)name;
#endif
    }

    ScopedCpuEvent::ScopedCpuEvent(uint64_t color, const char* name) {
        if (ShouldEmitPixEvents()) {
            BeginCpuEvent(color, name);
            active_ = ShouldEmitPixEvents();
        }
    }

    ScopedCpuEvent::~ScopedCpuEvent() {
        if (active_) {
            EndCpuEvent();
        }
    }

    ScopedGpuEvent::ScopedGpuEvent(ID3D12GraphicsCommandList* cmd, uint64_t color, const char* name)
        : cmd_(cmd) {
        if (cmd_ != nullptr && ShouldEmitPixEvents()) {
            BeginGpuEvent(cmd_, color, name);
            active_ = ShouldEmitPixEvents();
        }
    }

    ScopedGpuEvent::~ScopedGpuEvent() {
        if (active_) {
            EndGpuEvent(cmd_);
        }
    }

} // namespace HIKARI::GFX::PIX
