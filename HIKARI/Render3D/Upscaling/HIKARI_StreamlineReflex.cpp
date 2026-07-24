#include "Render3D/Upscaling/HIKARI_StreamlineReflex.h"

#include "Render3D/Upscaling/HIKARI_StreamlineInternal.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl_pcl.h>
#include <sl_reflex.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING {

#if defined(HIKARI_WITH_STREAMLINE)
    namespace {
        sl::ReflexMode ToNativeMode(StreamlineReflexMode mode) {
            switch (mode) {
            case StreamlineReflexMode::LowLatency:
                return sl::ReflexMode::eLowLatency;
            case StreamlineReflexMode::LowLatencyWithBoost:
                return sl::ReflexMode::eLowLatencyWithBoost;
            case StreamlineReflexMode::Off:
            default:
                return sl::ReflexMode::eOff;
            }
        }

        bool RecordReflexResult(
            INTERNAL::StreamlineState& state,
            const char* operation,
            sl::Result result) {

            if (INTERNAL::RecordResult(state, operation, result)) {
                return true;
            }
            ++state.reflex.stats.failureCount;
            return false;
        }

        bool MarkPcl(sl::PCLMarker marker, uint32_t markerBit) {
            INTERNAL::StreamlineState& state = INTERNAL::GetState();
            if (!state.reflex.stats.pclSupported ||
                state.frameToken == nullptr) {
                return false;
            }
            if (!RecordReflexResult(
                    state,
                    "slPCLSetMarker",
                    slPCLSetMarker(marker, *state.frameToken))) {
                return false;
            }
            state.reflex.stats.markerMask |= markerBit;
            return true;
        }
    }
#endif

    bool ConfigureStreamlineReflex(StreamlineReflexMode mode) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        state.reflex.stats.mode = mode;
#if !defined(HIKARI_WITH_STREAMLINE)
        return false;
#else
        if (!state.reflex.stats.reflexSupported) {
            return false;
        }

        sl::ReflexOptions options{};
        options.mode = ToNativeMode(mode);
        if (!RecordReflexResult(
                state,
                "slReflexSetOptions",
                slReflexSetOptions(options))) {
            state.reflex.stats.optionsConfigured = false;
            return false;
        }
        state.reflex.stats.optionsConfigured = true;
        return true;
#endif
    }

    bool BeginStreamlineReflexFrame() {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
#if !defined(HIKARI_WITH_STREAMLINE)
        return false;
#else
        if (state.frameToken == nullptr) {
            return false;
        }

        bool ok = true;
        if (state.reflex.stats.reflexSupported) {
            const bool slept = RecordReflexResult(
                state,
                "slReflexSleep",
                slReflexSleep(*state.frameToken));
            state.reflex.stats.sleepCalled = slept;
            ok = slept && ok;
        }
        if (state.reflex.stats.pclSupported) {
            ok = MarkPcl(sl::PCLMarker::eSimulationStart, 1u << 0u) && ok;
        }
        return ok;
#endif
    }

    void EndStreamlineReflexSimulation() {
#if defined(HIKARI_WITH_STREAMLINE)
        (void)MarkPcl(sl::PCLMarker::eSimulationEnd, 1u << 1u);
#endif
    }

    void MarkStreamlineReflexRenderSubmitStart() {
#if defined(HIKARI_WITH_STREAMLINE)
        (void)MarkPcl(sl::PCLMarker::eRenderSubmitStart, 1u << 2u);
#endif
    }

    void MarkStreamlineReflexRenderSubmitEnd() {
#if defined(HIKARI_WITH_STREAMLINE)
        (void)MarkPcl(sl::PCLMarker::eRenderSubmitEnd, 1u << 3u);
#endif
    }

    void MarkStreamlineReflexPresentStart() {
#if defined(HIKARI_WITH_STREAMLINE)
        (void)MarkPcl(sl::PCLMarker::ePresentStart, 1u << 4u);
#endif
    }

    void MarkStreamlineReflexPresentEnd() {
#if defined(HIKARI_WITH_STREAMLINE)
        (void)MarkPcl(sl::PCLMarker::ePresentEnd, 1u << 5u);
#endif
    }

    const StreamlineReflexStats& GetStreamlineReflexStats() {
        return INTERNAL::GetState().reflex.stats;
    }

} // namespace HIKARI::RENDER3D::UPSCALING
