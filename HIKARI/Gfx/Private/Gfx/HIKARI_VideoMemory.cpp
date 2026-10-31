#include "Gfx/HIKARI_VideoMemory.h"

#include <Windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <iterator>
#include <string>

#include "Diagnostics/HIKARI_MetricRegistry.h"

namespace HIKARI::GFX::VIDEO_MEMORY {

namespace {
using Microsoft::WRL::ComPtr;

constexpr const char* kMetricOwner = "Gfx.VideoMemory";
constexpr const char* kModule = "Gfx";

// 用途の表。指標 id の末尾と説明を 1 行にまとめる。Category の順に並べる。
struct CategoryInfo {
    const char* id;
    const char* help;
};
constexpr CategoryInfo kCategories[] = {
    {"texture", "Textures created by the texture manager (DxTextureManager), asset and generated."},
    {"renderTarget", "Render targets created through RenderTarget2D: scene, post, temporal and editor views."},
    {"geometry", "Cluster geometry buffers in default heaps. Mesh vertex/index buffers sit in upload heaps."},
    {"buffer", "GPU-resident buffers created through D3D12BufferFactory::CreateDefaultBuffer."},
    {"upload", "Upload/readback-heap resources from the central creation functions: host-visible memory, not VRAM."},
};
constexpr size_t kCategoryCount = std::size(kCategories);
static_assert(kCategoryCount == static_cast<size_t>(Category::Count), "every Category needs a row");

// 記帳中の資源のバイト数。資源はどのスレッドで壊れてもよいので atomic。
std::array<std::atomic<int64_t>, kCategoryCount> gTrackedBytes{};

// {7F1C6E2B-5D4A-4B38-9C0E-2A91B3D8E6F4}
constexpr GUID kTrackerGuid = {0x7f1c6e2b, 0x5d4a, 0x4b38, {0x9c, 0x0e, 0x2a, 0x91, 0xb3, 0xd8, 0xe6, 0xf4}};

// 資源の private data に持たせる印。資源が壊れて参照が 0 になった時に、記帳から自分の分を引く。
class ResourceTracker final : public IUnknown {
public:
    ResourceTracker(Category category, int64_t bytes) : category_(category), bytes_(bytes) {}

    void MarkCounted() {
        gTrackedBytes[static_cast<size_t>(category_)].fetch_add(bytes_);
        counted_ = true;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override {
        if (object == nullptr) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown)) {
            *object = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *object = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (remaining == 0u) {
            delete this;
        }
        return remaining;
    }

private:
    ~ResourceTracker() {
        if (counted_) {
            gTrackedBytes[static_cast<size_t>(category_)].fetch_sub(bytes_);
        }
    }

    std::atomic<ULONG> references_{1u};
    Category category_;
    int64_t bytes_;
    bool counted_ = false;
};

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
    std::array<METRIC::MetricHandle, kCategoryCount> categories{};
};

const Metrics& GetMetrics() {
    static const Metrics metrics = [] {
        Metrics created;
        created.usage = METRIC::Register({METRIC::kVramUsage, kMetricOwner, kModule, "bytes",
                                          METRIC::MetricCategory::Memory, "Local video memory used by this process."});
        created.budget =
            METRIC::Register({METRIC::kVramBudget, kMetricOwner, kModule, "bytes", METRIC::MetricCategory::Memory,
                              "Local video memory the OS currently lets this process use."});
        for (size_t index = 0; index < kCategoryCount; ++index) {
            created.categories[index] =
                METRIC::Register({std::string("memory.vram.") + kCategories[index].id, kMetricOwner, kModule, "bytes",
                                  METRIC::MetricCategory::Memory, kCategories[index].help});
        }
        return created;
    }();
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

void Track(ID3D12Resource* resource, Category category) {
    if (resource == nullptr || static_cast<size_t>(category) >= kCategoryCount) {
        return;
    }
    D3D12_HEAP_PROPERTIES heap{};
    if (SUCCEEDED(resource->GetHeapProperties(&heap, nullptr)) &&
        (heap.Type == D3D12_HEAP_TYPE_UPLOAD || heap.Type == D3D12_HEAP_TYPE_READBACK)) {
        category = Category::Upload;
    }
    ComPtr<ID3D12Device> device;
    if (FAILED(resource->GetDevice(IID_PPV_ARGS(&device)))) {
        return;
    }
    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    const D3D12_RESOURCE_ALLOCATION_INFO info = device->GetResourceAllocationInfo(0, 1, &desc);
    if (info.SizeInBytes == UINT64_MAX) {
        return;
    }
    auto* tracker = new ResourceTracker(category, static_cast<int64_t>(info.SizeInBytes));
    if (SUCCEEDED(resource->SetPrivateDataInterface(kTrackerGuid, tracker))) {
        tracker->MarkCounted();
    }
    tracker->Release();
}

void Publish(ID3D12Device* device, uint64_t frameIndex) {
    const Metrics& metrics = GetMetrics();
    for (size_t index = 0; index < kCategoryCount; ++index) {
        METRIC::Publish(metrics.categories[index], static_cast<double>(gTrackedBytes[index].load()), frameIndex);
    }
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
