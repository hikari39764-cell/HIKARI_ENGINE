#include "Gfx/HIKARI_VideoMemory.h"

#include <Windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "Diagnostics/HIKARI_MetricRegistry.h"

namespace HIKARI::GFX::VIDEO_MEMORY {

namespace {
using Microsoft::WRL::ComPtr;

constexpr const char* kMetricOwner = "Gfx.VideoMemory";

struct State {
    // 見つけたアダプターか、見つけられなかったデバイスの LUID。同じデバイスでは探し直さない。
    LUID deviceLuid{};
    bool hasLuid = false;
    ComPtr<IDXGIAdapter3> adapter{};
    std::optional<AdapterInfo> info{};
};

State gState{};

struct Metrics {
    METRIC::MetricHandle usage = METRIC::kInvalidMetric;
    METRIC::MetricHandle budget = METRIC::kInvalidMetric;
};

const Metrics& GetMetrics() {
    static const Metrics metrics = {
        METRIC::Register({METRIC::kVramUsage, kMetricOwner, "bytes", METRIC::MetricCategory::Memory,
                          "Local video memory used by this process."}),
        METRIC::Register({METRIC::kVramBudget, kMetricOwner, "bytes", METRIC::MetricCategory::Memory,
                          "Local video memory the OS currently lets this process use."})};
    return metrics;
}

void MarkUnavailable(const char* reason) {
    METRIC::MarkUnsupported(GetMetrics().usage, reason);
    METRIC::MarkUnsupported(GetMetrics().budget, reason);
}

// device のアダプターを探す。デバイスが変わった時だけ DXGI を引き直す。
bool ResolveAdapter(ID3D12Device* device) {
    const LUID luid = device->GetAdapterLuid();
    if (gState.hasLuid && gState.deviceLuid.LowPart == luid.LowPart && gState.deviceLuid.HighPart == luid.HighPart) {
        return gState.adapter != nullptr;
    }
    gState = {};
    gState.deviceLuid = luid;
    gState.hasLuid = true;

    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&gState.adapter)))) {
        gState.adapter.Reset();
        return false;
    }
    DXGI_ADAPTER_DESC1 desc{};
    if (SUCCEEDED(gState.adapter->GetDesc1(&desc))) {
        char name[512]{};
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
        gState.info = AdapterInfo{name, desc.VendorId, desc.DeviceId, desc.DedicatedVideoMemory};
    }
    return true;
}
} // namespace

void Publish(ID3D12Device* device, uint64_t frameIndex) {
    const Metrics& metrics = GetMetrics();
    if (device == nullptr || !ResolveAdapter(device)) {
        MarkUnavailable("The DXGI adapter of the device could not be found.");
        return;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO memory{};
    if (FAILED(gState.adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory))) {
        MarkUnavailable("DXGI could not report the local video memory.");
        return;
    }
    METRIC::Publish(metrics.usage, static_cast<double>(memory.CurrentUsage), frameIndex);
    METRIC::Publish(metrics.budget, static_cast<double>(memory.Budget), frameIndex);
}

void Shutdown() {
    gState = {};
    METRIC::ClearOwner(kMetricOwner);
}

std::optional<AdapterInfo> GetAdapterInfo() { return gState.info; }

} // namespace HIKARI::GFX::VIDEO_MEMORY
