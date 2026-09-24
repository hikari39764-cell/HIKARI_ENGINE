#pragma once

#include <Windows.h>
#include <d3d12.h>
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <string>
#include "Core/HIKARI_Logger.h"

namespace HIKARI::GFX {

[[noreturn]] inline void AbortUnsafeGpuSubmission(const char* operation) {
    const std::string message = std::string("GPU synchronization failed: ") + operation +
                                ". The application must close because GPU resource completion cannot be established. "
                                "Update the graphics driver and restart. See the application log.";
    HIKARI_LOG_ERROR(message);
    MessageBoxA(nullptr, message.c_str(), "HIKARI graphics error", MB_OK | MB_ICONERROR);
    // 未完了リソースを解放してドライバへ渡さず、OS に終了処理を委ねる。
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(DXGI_ERROR_DEVICE_HUNG));
    std::abort();
}

inline bool WaitForGpuCompletion(ID3D12Device* device, ID3D12Fence* fence, uint64_t value, HANDLE event,
                                 const char* operation) {
    if (!device || !fence)
        AbortUnsafeGpuSubmission(operation);
    const ULONGLONG started = GetTickCount64();
    bool reportedSlowWait = false;
    bool eventPrepared = false;
    bool useEvent = false;
    for (;;) {
        const uint64_t completed = fence->GetCompletedValue();
        const HRESULT removedReason = device->GetDeviceRemovedReason();
        if (completed == UINT64_MAX || FAILED(removedReason)) {
            std::ostringstream message;
            message << operation << ": graphics device removed; reason=0x" << std::hex
                    << static_cast<uint32_t>(removedReason) << std::dec << " target_fence=" << value
                    << " completed_fence=" << completed;
            HIKARI_LOG_ERROR(message.str());
            return false;
        }
        if (completed >= value)
            return true;
        if (!eventPrepared) {
            useEvent = event && SUCCEEDED(fence->SetEventOnCompletion(value, event));
            if (!useEvent)
                HIKARI_LOG_WARN(std::string(operation) + ": fence event unavailable; polling completion.");
            eventPrepared = true;
        }
        const ULONGLONG elapsed = GetTickCount64() - started;
        if (!reportedSlowWait && elapsed >= 1000) {
            HIKARI_LOG_WARN(std::string(operation) + ": GPU wait exceeded 1s; target_fence=" + std::to_string(value) +
                            " completed_fence=" + std::to_string(completed));
            reportedSlowWait = true;
        }
        // 待機中は Resize 等を再入させない。完了不明のままリソースを解放しない。
        if (elapsed >= 30000)
            AbortUnsafeGpuSubmission(operation);
        if (useEvent) {
            const DWORD wait = WaitForSingleObject(event, 100);
            if (wait == WAIT_FAILED)
                useEvent = false;
        } else {
            Sleep(10);
        }
    }
}

inline bool SignalAndWaitForGpuCompletion(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12Fence* fence,
                                          uint64_t value, HANDLE event, const char* operation) {
    if (!device || !queue || !fence)
        AbortUnsafeGpuSubmission(operation);
    const HRESULT result = queue->Signal(fence, value);
    if (FAILED(result)) {
        if (FAILED(device->GetDeviceRemovedReason()))
            return false;
        AbortUnsafeGpuSubmission(operation);
    }
    return WaitForGpuCompletion(device, fence, value, event, operation);
}
} // namespace HIKARI::GFX
