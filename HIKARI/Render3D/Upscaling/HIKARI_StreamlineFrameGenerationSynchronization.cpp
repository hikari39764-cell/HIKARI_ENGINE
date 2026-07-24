#include "Render3D/Upscaling/HIKARI_StreamlineFrameGeneration.h"

#include <string>

#include "Core/HIKARI_Logger.h"
#include "Render3D/Upscaling/HIKARI_StreamlineInternal.h"

#if defined(HIKARI_WITH_STREAMLINE)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#pragma warning(push, 0)
#include <sl_dlss_g.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING {

#if defined(HIKARI_WITH_STREAMLINE)
    namespace {
        std::string DescribeStatusFlags(uint32_t flags) {
            if (flags == 0u) {
                return {};
            }
            std::string result;
            auto append = [&result](const char* value) {
                if (!result.empty()) {
                    result += ", ";
                }
                result += value;
            };
            if ((flags & (1u << 0)) != 0u) append("resolution too low");
            if ((flags & (1u << 1)) != 0u) append("Reflex inactive");
            if ((flags & (1u << 2)) != 0u) append("HDR format unsupported");
            if ((flags & (1u << 3)) != 0u) append("common constants invalid");
            if ((flags & (1u << 4)) != 0u) append("back-buffer index missing");
            if ((flags & ~0x1Fu) != 0u) append("unknown SDK status");
            return result;
        }
    }
#endif

    bool CaptureStreamlineFrameGenerationCompletionAfterPresent() {
#if !defined(HIKARI_WITH_STREAMLINE)
        return true;
#else
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        INTERNAL::StreamlineFrameGenerationRuntimeState& frameGeneration =
            state.frameGeneration;
        StreamlineFrameGenerationStats& stats = frameGeneration.stats;
        if (!state.stats.initialized ||
            !stats.supported ||
            !stats.featureLoaded ||
            frameGeneration.resourcesReleased) {
            return true;
        }

        sl::DLSSGState nativeState{};
        const sl::Result result = slDLSSGGetState(
            state.viewport,
            nativeState,
            nullptr);
        if (!INTERNAL::RecordResult(
                state,
                "slDLSSGGetState(after Present)",
                result,
                false)) {
            stats.stateValid = false;
            ++stats.failureCount;
            stats.status = StreamlineFrameGenerationStatus::RuntimeFailure;
            stats.statusReason = state.stats.lastResult;
            frameGeneration.completionStateCapturedAfterPresent = false;
            return false;
        }

        stats.stateValid = true;
        stats.estimatedVramBytes = nativeState.estimatedVRAMUsageInBytes;
        stats.statusFlags = static_cast<uint32_t>(nativeState.status);
        stats.presentedFrames = nativeState.numFramesActuallyPresented;
        stats.maxGeneratedFrames = nativeState.numFramesToGenerateMax;
        frameGeneration.inputsProcessingCompletionFence =
            nativeState.inputsProcessingCompletionFence;
        frameGeneration.inputsProcessingCompletionFenceValue =
            nativeState.lastPresentInputsProcessingCompletionFenceValue;
        frameGeneration.completionStateCapturedAfterPresent = true;

        if (stats.statusFlags != 0u) {
            stats.status = StreamlineFrameGenerationStatus::SdkRejectedInputs;
            stats.statusReason = DescribeStatusFlags(stats.statusFlags);
        } else if (frameGeneration.settings.enabled &&
                   stats.optionsConfigured) {
            stats.status = StreamlineFrameGenerationStatus::Active;
            stats.statusReason.clear();
        }
        return true;
#endif
    }

    bool WaitForStreamlineFrameGenerationInputs(
        uint32_t timeoutMilliseconds) {
#if !defined(HIKARI_WITH_STREAMLINE)
        (void)timeoutMilliseconds;
        return true;
#else
        INTERNAL::StreamlineFrameGenerationRuntimeState& frameGeneration =
            INTERNAL::GetState().frameGeneration;
        if (!frameGeneration.completionStateCapturedAfterPresent ||
            frameGeneration.inputsProcessingCompletionFence == nullptr ||
            frameGeneration.inputsProcessingCompletionFenceValue == 0) {
            return true;
        }

        auto* completionFence = reinterpret_cast<ID3D12Fence*>(
            frameGeneration.inputsProcessingCompletionFence);
        const uint64_t completionValue =
            frameGeneration.inputsProcessingCompletionFenceValue;
        if (completionFence->GetCompletedValue() >= completionValue) {
            return true;
        }

        HANDLE completionEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (completionEvent == nullptr) {
            HIKARI_LOG_ERROR(
                "[Streamline] Could not create the DLSS-G input completion event.");
            return false;
        }
        const HRESULT eventResult = completionFence->SetEventOnCompletion(
            completionValue,
            completionEvent);
        if (FAILED(eventResult)) {
            CloseHandle(completionEvent);
            HIKARI_LOG_ERROR(
                "[Streamline] Could not arm the DLSS-G input completion fence.");
            return false;
        }

        const DWORD waitResult = WaitForSingleObject(
            completionEvent,
            timeoutMilliseconds);
        CloseHandle(completionEvent);
        if (waitResult != WAIT_OBJECT_0) {
            HIKARI_LOG_ERROR(
                waitResult == WAIT_TIMEOUT
                    ? "[Streamline] Timed out waiting for DLSS-G input processing."
                    : "[Streamline] Failed while waiting for DLSS-G input processing.");
            return false;
        }
        return true;
#endif
    }

} // namespace HIKARI::RENDER3D::UPSCALING
