#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

#include <array>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <dxgi1_6.h>

#include "Core/HIKARI_Logger.h"
#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"
#include "Render3D/Upscaling/HIKARI_StreamlineInternal.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl_helpers.h>
#include <sl_security.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING {

    namespace {
#if defined(HIKARI_WITH_STREAMLINE)
        std::filesystem::path ModuleDirectory() {
            std::array<wchar_t, 32768> path{};
            const DWORD length = GetModuleFileNameW(
                nullptr,
                path.data(),
                static_cast<DWORD>(path.size()));
            if (length == 0 || length >= static_cast<DWORD>(path.size())) {
                return {};
            }
            return std::filesystem::path(path.data()).parent_path();
        }

        void StreamlineLogCallback(sl::LogType type, const char* message) {
            if (message == nullptr || message[0] == '\0') {
                return;
            }
            const std::string text = std::string("[Streamline] ") + message;
            if (type == sl::LogType::eError) {
                HIKARI_LOG_ERROR(text);
            } else if (type == sl::LogType::eWarn) {
                HIKARI_LOG_WARN(text);
            }
        }
#endif
    }

    namespace INTERNAL {

        StreamlineState& GetState() {
            static StreamlineState state{};
            static const bool initialized = [&] {
#if defined(HIKARI_WITH_STREAMLINE)
                state.stats.sdkCompiled = true;
                state.stats.status = StreamlineRuntimeStatus::AwaitingInitialization;
                state.stats.sdkVersion = kStreamlineSdkVersion;
#else
                state.stats.status = StreamlineRuntimeStatus::NotCompiled;
#endif
                return true;
            }();
            (void)initialized;
            return state;
        }

        void ResetFrameStats(StreamlineState& state, uint64_t frameIndex) {
            state.stats.frameIndex = frameIndex;
            state.stats.frameTokenReady = false;
            state.stats.constantsSubmitted = false;
            state.stats.dlssRequested = false;
            state.stats.dlssEvaluated = false;
            state.stats.fallbackUsed = false;
        }

#if defined(HIKARI_WITH_STREAMLINE)
        bool RecordResult(
            StreamlineState& state,
            const char* operation,
            sl::Result result,
            bool countFailure) {
            state.stats.lastOperation = operation != nullptr ? operation : "Streamline";
            state.stats.lastResult = sl::getResultAsStr(result);
            if (result == sl::Result::eOk) {
                return true;
            }
            if (countFailure) {
                ++state.stats.failureCount;
            }
            state.stats.status = result == sl::Result::eWarnOutOfVRAM
                ? StreamlineRuntimeStatus::ResourcePressure
                : StreamlineRuntimeStatus::RuntimeFailure;
            HIKARI_LOG_WARN(
                std::string("[Streamline] ") + state.stats.lastOperation +
                " failed: " + state.stats.lastResult);
            return false;
        }
#endif

    } // namespace INTERNAL

    const char* ToString(StreamlineRuntimeStatus status) {
        switch (status) {
        case StreamlineRuntimeStatus::NotCompiled: return "not compiled";
        case StreamlineRuntimeStatus::AwaitingInitialization: return "awaiting init";
        case StreamlineRuntimeStatus::SignatureRejected: return "signature rejected";
        case StreamlineRuntimeStatus::InitializationFailed: return "init failed";
        case StreamlineRuntimeStatus::AwaitingDevice: return "awaiting device";
        case StreamlineRuntimeStatus::DeviceFailed: return "device failed";
        case StreamlineRuntimeStatus::FeatureUnsupported: return "unsupported";
        case StreamlineRuntimeStatus::Ready: return "ready";
        case StreamlineRuntimeStatus::ResourcePressure: return "resource pressure";
        case StreamlineRuntimeStatus::RuntimeFailure: return "runtime failure";
        default: return "unknown";
        }
    }

    bool InitializeStreamlineEarly(bool enableFrameGenerationPlugins) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)enableFrameGenerationPlugins;
        state.stats.status = StreamlineRuntimeStatus::NotCompiled;
        return false;
#else
        if (state.stats.initialized) {
            return true;
        }

        const std::filesystem::path moduleDirectory = ModuleDirectory();
        const std::filesystem::path interposerPath =
            moduleDirectory / L"sl.interposer.dll";
        if (moduleDirectory.empty() ||
            !std::filesystem::is_regular_file(interposerPath) ||
            !sl::security::verifyEmbeddedSignature(interposerPath.c_str())) {
            state.stats.status = StreamlineRuntimeStatus::SignatureRejected;
            state.stats.lastOperation = "verify sl.interposer.dll";
            state.stats.lastResult = "signature verification failed";
            HIKARI_LOG_ERROR(
                "[Streamline] signed sl.interposer.dll was not found or failed verification.");
            return false;
        }

        state.pluginPath = moduleDirectory.wstring();
        const std::filesystem::path logDirectory = std::filesystem::absolute("logs");
        std::error_code error{};
        std::filesystem::create_directories(logDirectory, error);
        state.logPath = logDirectory.wstring();

        const wchar_t* pluginPaths[] = { state.pluginPath.c_str() };
        state.plugins.dlss = std::filesystem::is_regular_file(
            moduleDirectory / L"sl.dlss.dll");
        const bool frameGenerationPluginPresent = std::filesystem::is_regular_file(
            moduleDirectory / L"sl.dlss_g.dll");
        const bool reflexPluginPresent = std::filesystem::is_regular_file(
            moduleDirectory / L"sl.reflex.dll");
        const bool pclPluginPresent = std::filesystem::is_regular_file(
            moduleDirectory / L"sl.pcl.dll");
        state.plugins.frameGeneration =
            enableFrameGenerationPlugins && frameGenerationPluginPresent;
        state.plugins.reflex =
            enableFrameGenerationPlugins && reflexPluginPresent;
        state.plugins.pcl =
            enableFrameGenerationPlugins && pclPluginPresent;
        state.stats.dlssPluginPresent = state.plugins.dlss;
        state.reflex.stats.reflexPluginPresent = reflexPluginPresent;
        state.reflex.stats.pclPluginPresent = pclPluginPresent;
        state.frameGeneration.stats.pluginPresent =
            frameGenerationPluginPresent;

        std::vector<sl::Feature> features;
        if (state.plugins.dlss) {
            features.push_back(sl::kFeatureDLSS);
        }
        if (state.plugins.reflex) {
            features.push_back(sl::kFeatureReflex);
        }
        if (state.plugins.pcl) {
            features.push_back(sl::kFeaturePCL);
        }
        if (state.plugins.frameGeneration) {
            features.push_back(sl::kFeatureDLSS_G);
        }
        if (features.empty()) {
            state.stats.status = StreamlineRuntimeStatus::InitializationFailed;
            state.stats.lastOperation = "discover Streamline plugins";
            state.stats.lastResult = "no feature plugins found";
            HIKARI_LOG_WARN(
                "[Streamline] no feature plugins were found next to the executable.");
            return false;
        }
        sl::Preferences preferences{};
        preferences.showConsole = false;
        preferences.logLevel = sl::LogLevel::eDefault;
        preferences.pathsToPlugins = pluginPaths;
        preferences.numPathsToPlugins = static_cast<uint32_t>(std::size(pluginPaths));
        preferences.pathToLogsAndData = state.logPath.c_str();
        preferences.logMessageCallback = StreamlineLogCallback;
        preferences.flags =
            sl::PreferenceFlags::eDisableDebugText |
            sl::PreferenceFlags::eUseFrameBasedResourceTagging;
        preferences.featuresToLoad = features.data();
        preferences.numFeaturesToLoad = static_cast<uint32_t>(features.size());
        preferences.engine = sl::EngineType::eCustom;
        preferences.engineVersion = "HIKARI-1";
        preferences.projectId = "e7cc83d6-d200-4d4a-85e4-7d93e060b066";
        preferences.renderAPI = sl::RenderAPI::eD3D12;

        if (!INTERNAL::RecordResult(
                state,
                "slInit",
                slInit(preferences, sl::kSDKVersion))) {
            state.stats.status = StreamlineRuntimeStatus::InitializationFailed;
            return false;
        }

        state.stats.initialized = true;
        state.frameGeneration.stats.featureLoaded =
            state.plugins.frameGeneration;
        state.stats.status = StreamlineRuntimeStatus::AwaitingDevice;
        HIKARI_LOG_INFO(
            std::string("[Streamline] initialized SDK ") +
            INTERNAL::kStreamlineSdkVersion +
            " plugins=" + moduleDirectory.string() +
            " dlss=" + (state.plugins.dlss ? "yes" : "no") +
            " dlss_g=" + (frameGenerationPluginPresent ? "yes" : "no") +
            " reflex=" + (reflexPluginPresent ? "yes" : "no") +
            " pcl=" + (pclPluginPresent ? "yes" : "no") +
            " fg_host=" + (enableFrameGenerationPlugins ? "enabled" : "disabled"));
        return true;
#endif
    }

    bool AttachStreamlineDevice(ID3D12Device* device) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)device;
        return false;
#else
        if (!state.stats.initialized || device == nullptr) {
            state.stats.status = StreamlineRuntimeStatus::DeviceFailed;
            return false;
        }
        if (!INTERNAL::RecordResult(state, "slSetD3DDevice", slSetD3DDevice(device))) {
            state.stats.status = StreamlineRuntimeStatus::DeviceFailed;
            return false;
        }
        state.stats.deviceAttached = true;

        LUID luid = device->GetAdapterLuid();
        sl::AdapterInfo adapter{};
        adapter.deviceLUID = reinterpret_cast<uint8_t*>(&luid);
        adapter.deviceLUIDSizeInBytes = sizeof(luid);
        auto querySupport = [&adapter](sl::Feature feature) {
            return slIsFeatureSupported(feature, adapter) == sl::Result::eOk;
        };
        state.stats.dlssSupported =
            state.plugins.dlss && querySupport(sl::kFeatureDLSS);
        state.reflex.stats.reflexSupported =
            state.plugins.reflex && querySupport(sl::kFeatureReflex);
        state.reflex.stats.pclSupported =
            state.plugins.pcl && querySupport(sl::kFeaturePCL);
        state.frameGeneration.stats.supported =
            state.plugins.frameGeneration &&
            state.reflex.stats.reflexSupported &&
            state.reflex.stats.pclSupported &&
            querySupport(sl::kFeatureDLSS_G);

        if (state.reflex.stats.reflexSupported) {
            (void)ConfigureStreamlineReflex(
                state.frameGeneration.stats.supported
                    ? StreamlineReflexMode::LowLatency
                    : StreamlineReflexMode::Off);
        }
        const bool anySupported =
            state.stats.dlssSupported ||
            state.reflex.stats.reflexSupported ||
            state.reflex.stats.pclSupported ||
            state.frameGeneration.stats.supported;
        state.stats.status = anySupported
            ? StreamlineRuntimeStatus::Ready
            : StreamlineRuntimeStatus::FeatureUnsupported;
        state.stats.lastOperation = "query Streamline feature support";
        state.stats.lastResult = anySupported ? "eOk" : "unsupported";
        HIKARI_LOG_INFO(
            std::string("[Streamline] device features dlss=") +
            (state.stats.dlssSupported ? "yes" : "no") +
            " dlss_g=" +
            (state.frameGeneration.stats.supported ? "yes" : "no") +
            " reflex=" +
            (state.reflex.stats.reflexSupported ? "yes" : "no") +
            " pcl=" +
            (state.reflex.stats.pclSupported ? "yes" : "no"));
        return anySupported;
#endif
    }

    bool SetStreamlineFrameGenerationFeatureLoaded(bool loaded) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)loaded;
        return false;
#else
        if (!state.plugins.frameGeneration) {
            return !loaded;
        }
        if (!state.stats.initialized || !state.stats.deviceAttached) {
            return false;
        }
        if (state.frameGeneration.stats.featureLoaded == loaded) {
            return true;
        }

        if (!loaded &&
            (state.frameGeneration.stats.optionsConfigured ||
             !state.frameGeneration.resourcesReleased)) {
            HIKARI_LOG_ERROR(
                "[Streamline] DLSS-G unload rejected because the feature is still active or owns viewport resources.");
            return false;
        }
        if (!INTERNAL::RecordResult(
                state,
                loaded
                    ? "slSetFeatureLoaded(DLSS-G, true)"
                    : "slSetFeatureLoaded(DLSS-G, false)",
                slSetFeatureLoaded(sl::kFeatureDLSS_G, loaded))) {
            return false;
        }

        state.frameGeneration.stats.featureLoaded = loaded;
        state.frameGeneration.resourcesReleased = true;
        state.frameGeneration.stats.optionsConfigured = false;
        state.frameGeneration.stats.tagsSubmitted = false;
        state.frameGeneration.configuredGeneratedFrames = 0;
        state.frameGeneration.inputsProcessingCompletionFence = nullptr;
        state.frameGeneration.inputsProcessingCompletionFenceValue = 0;
        state.frameGeneration.completionStateCapturedAfterPresent = false;
        state.frameGeneration.stats.status =
            StreamlineFrameGenerationStatus::Suspended;
        state.frameGeneration.stats.statusReason = loaded
            ? "feature loaded; waiting for game presentation"
            : "feature unloaded for editor presentation";
        HIKARI_LOG_INFO(
            loaded
                ? "[Streamline] DLSS-G feature loaded for game presentation."
                : "[Streamline] DLSS-G feature unloaded for editor presentation.");
        return true;
#endif
    }

    bool IsStreamlineFrameGenerationFeatureLoaded() {
        return INTERNAL::GetState().frameGeneration.stats.featureLoaded;
    }

    void InspectStreamlineSwapChain(IDXGISwapChain4* swapChain) {
#if defined(HIKARI_WITH_STREAMLINE)
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        if (!state.stats.initialized || swapChain == nullptr) {
            return;
        }

        auto describe = [](const char* label, IDXGISwapChain4* candidate) {
            if (candidate == nullptr) {
                return std::string(label) + "=missing";
            }

            DXGI_SWAP_CHAIN_DESC1 desc{};
            UINT latency = 0;
            const HRESULT descResult = candidate->GetDesc1(&desc);
            const HRESULT latencyResult =
                candidate->GetMaximumFrameLatency(&latency);
            const HANDLE waitable = candidate->GetFrameLatencyWaitableObject();

            std::ostringstream stream;
            stream << label
                << " desc=" << (SUCCEEDED(descResult) ? "ok" : "failed")
                << " buffers=" << desc.BufferCount
                << " flags=0x" << std::hex << desc.Flags << std::dec
                << " latency="
                << (SUCCEEDED(latencyResult) ? std::to_string(latency) : "failed")
                << " waitable=" << (waitable != nullptr ? "yes" : "no");
            return stream.str();
        };

        void* nativeInterface = nullptr;
        const sl::Result nativeResult = slGetNativeInterface(
            swapChain,
            &nativeInterface);
        IDXGISwapChain4* nativeSwapChain =
            nativeResult != sl::Result::eOk
            ? nullptr
            : static_cast<IDXGISwapChain4*>(nativeInterface);

        HIKARI_LOG_INFO(
            std::string("[Streamline] swap-chain ") +
            describe("proxy", swapChain) + " " +
            describe("native", nativeSwapChain));
#else
        (void)swapChain;
#endif
    }

    void UpdateStreamlineContext(const GFX::Context& context) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        state.context = context;
        state.output.UpdateContext(context);
    }

    void ShutdownStreamline() {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        (void)DeactivateStreamlineFrameGeneration(true);
        state.output.Finalize();
        state.outputFormat = DXGI_FORMAT_UNKNOWN;
        state.outputWidth = 0;
        state.outputHeight = 0;
#if defined(HIKARI_WITH_STREAMLINE)
        state.frameToken = nullptr;
        if (state.stats.initialized) {
            (void)INTERNAL::RecordResult(state, "slShutdown", slShutdown(), false);
        }
        state.stats.initialized = false;
        state.stats.deviceAttached = false;
        state.stats.dlssPluginPresent = false;
        state.stats.dlssSupported = false;
        state.stats.frameTokenReady = false;
        state.stats.constantsSubmitted = false;
        state.optionsConfigured = false;
        state.configuredMode = StreamlineDlssMode::Off;
        state.optimalSettingsCached = false;
        state.plugins = {};
        state.reflex = {};
        state.frameGeneration = {};
        state.stats.status = StreamlineRuntimeStatus::AwaitingInitialization;
#endif
    }

    void ReleaseStreamlineTransientResources() {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        (void)DeactivateStreamlineFrameGeneration(true);
#if defined(HIKARI_WITH_STREAMLINE)
        if (state.stats.initialized &&
            (state.output.GetResource() != nullptr || state.optionsConfigured)) {
            (void)INTERNAL::RecordResult(
                state,
                "slFreeResources(DLSS)",
                slFreeResources(sl::kFeatureDLSS, state.viewport),
                false);
        }
        state.optionsConfigured = false;
        state.configuredMode = StreamlineDlssMode::Off;
        state.stats.retryFrameIndex = 0;
        state.stats.consecutiveFailureCount = 0;
        state.stats.retryPending = false;
        state.dlssLastFailureResult = sl::Result::eOk;
#endif
        state.output.Finalize();
        state.outputWidth = 0;
        state.outputHeight = 0;
        state.outputFormat = DXGI_FORMAT_UNKNOWN;
        state.stats.outputReady = false;
        state.stats.dlssEvaluated = false;
    }

    bool IsStreamlineDlssAvailable() {
        const StreamlineDebugStats& stats = INTERNAL::GetState().stats;
        return stats.sdkCompiled &&
            stats.initialized &&
            stats.deviceAttached &&
            stats.dlssSupported;
    }

    const StreamlineDebugStats& GetStreamlineDebugStats() {
        return INTERNAL::GetState().stats;
    }

} // namespace HIKARI::RENDER3D::UPSCALING
