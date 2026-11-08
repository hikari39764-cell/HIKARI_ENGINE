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

// 所有者の表。指標 id の末尾と説明を 1 行にまとめる。Owner の順に並べる。
struct OwnerInfo {
    const char* id;
    const char* help;
};
constexpr OwnerInfo kOwners[] = {
    {"textures", "Asset and generated textures created by the texture manager (DxTextureManager)."},
    {"sceneTargets", "Scene color/depth, scene surface and reflection buffers, temporal history, depth pyramid."},
    {"post", "Post effects, color grading LUT, depth of field, motion blur, composites, automatic exposure."},
    {"shadow", "Shadow maps and ray traced shadow targets."},
    {"lighting", "Dynamic GI, probe volumes, environment cache, sky, atmosphere, volumetric clouds and lighting."},
    {"screenSpace", "SSAO, SSR and subsurface scattering targets."},
    {"geometry", "Meshes, clusters, meshlets, BLAS/TLAS and ocean spectrum."},
    {"gpuDriven", "Visibility, culling, command streams and GPU scene frame buffers."},
    {"vfx", "Particles, ground telegraphs, stylized screen features and world text."},
    {"ui", "Game UI effects and backdrop composite."},
    {"editor", "Editor view capture and baking tools."},
    {"diagnostics", "GPU timing and pipeline statistics queries and readback."},
    {"upload", "Upload/readback-heap resources of any owner: host-visible memory, not VRAM."},
};
constexpr size_t kOwnerCount = std::size(kOwners);
static_assert(kOwnerCount == static_cast<size_t>(Owner::Count), "every Owner needs a row");
constexpr const char* kUnattributedId = "memory.vram.unattributed";

// 記帳中の資源のバイト数。資源はどのスレッドで壊れてもよいので atomic。
std::array<std::atomic<int64_t>, kOwnerCount> gTrackedBytes{};

// {7F1C6E2B-5D4A-4B38-9C0E-2A91B3D8E6F4}
constexpr GUID kTrackerGuid = {0x7f1c6e2b, 0x5d4a, 0x4b38, {0x9c, 0x0e, 0x2a, 0x91, 0xb3, 0xd8, 0xe6, 0xf4}};

// 資源の private data に持たせる印。資源が壊れて参照が 0 になった時に、記帳から自分の分を引く。
class ResourceTracker final : public IUnknown {
public:
    ResourceTracker(Owner owner, int64_t bytes) : owner_(owner), bytes_(bytes) {}

    void MarkCounted() {
        gTrackedBytes[static_cast<size_t>(owner_)].fetch_add(bytes_);
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
            gTrackedBytes[static_cast<size_t>(owner_)].fetch_sub(bytes_);
        }
    }

    std::atomic<ULONG> references_{1u};
    Owner owner_;
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
    std::array<METRIC::MetricHandle, kOwnerCount> owners{};
    METRIC::MetricHandle unattributed = METRIC::kInvalidMetric;
};

const Metrics& GetMetrics() {
    static const Metrics metrics = [] {
        Metrics created;
        created.usage = METRIC::Register({METRIC::kVramUsage, kMetricOwner, kModule, "bytes",
                                          METRIC::MetricCategory::Memory, "Local video memory used by this process."});
        created.budget =
            METRIC::Register({METRIC::kVramBudget, kMetricOwner, kModule, "bytes", METRIC::MetricCategory::Memory,
                              "Local video memory the OS currently lets this process use."});
        for (size_t index = 0; index < kOwnerCount; ++index) {
            created.owners[index] =
                METRIC::Register({std::string("memory.vram.") + kOwners[index].id, kMetricOwner, kModule, "bytes",
                                  METRIC::MetricCategory::Memory, kOwners[index].help});
        }
        created.unattributed =
            METRIC::Register({kUnattributedId, kMetricOwner, kModule, "bytes", METRIC::MetricCategory::Memory,
                              "Local video memory in use that no owner recorded (usage minus owners)."});
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

// object の private data に印を持たせて bytes を記帳する（object が壊れると引かれる）。
void TrackObject(ID3D12Object* object, Owner owner, int64_t bytes) {
    auto* tracker = new ResourceTracker(owner, bytes);
    if (SUCCEEDED(object->SetPrivateDataInterface(kTrackerGuid, tracker))) {
        tracker->MarkCounted();
    }
    tracker->Release();
}

void TrackResource(ID3D12Resource* resource, Owner owner, D3D12_HEAP_TYPE heapType) {
    if (resource == nullptr || static_cast<size_t>(owner) >= kOwnerCount) {
        return;
    }
    if (heapType == D3D12_HEAP_TYPE_UPLOAD || heapType == D3D12_HEAP_TYPE_READBACK) {
        owner = Owner::Upload;
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
    TrackObject(resource, owner, static_cast<int64_t>(info.SizeInBytes));
}
} // namespace

HRESULT CreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* heapProperties,
                                D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC* desc,
                                D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue, Owner owner,
                                REFIID riid, void** resource) {
    ComPtr<ID3D12Resource> created;
    const HRESULT hr = device->CreateCommittedResource(heapProperties, heapFlags, desc, initialState, clearValue,
                                                       IID_PPV_ARGS(&created));
    if (FAILED(hr)) {
        if (resource != nullptr) {
            *resource = nullptr;
        }
        return hr;
    }
    TrackResource(created.Get(), owner, heapProperties->Type);
    return resource != nullptr ? created->QueryInterface(riid, resource) : S_OK;
}

HRESULT CreateHeap(ID3D12Device* device, const D3D12_HEAP_DESC* desc, Owner owner, REFIID riid, void** heap) {
    ComPtr<ID3D12Heap> created;
    const HRESULT hr = device->CreateHeap(desc, IID_PPV_ARGS(&created));
    if (FAILED(hr)) {
        if (heap != nullptr) {
            *heap = nullptr;
        }
        return hr;
    }
    const bool host =
        desc->Properties.Type == D3D12_HEAP_TYPE_UPLOAD || desc->Properties.Type == D3D12_HEAP_TYPE_READBACK;
    if (static_cast<size_t>(owner) < kOwnerCount) {
        TrackObject(created.Get(), host ? Owner::Upload : owner, static_cast<int64_t>(desc->SizeInBytes));
    }
    return heap != nullptr ? created->QueryInterface(riid, heap) : S_OK;
}

void Track(ID3D12Resource* resource, Owner owner) {
    D3D12_HEAP_PROPERTIES heap{};
    if (resource != nullptr && SUCCEEDED(resource->GetHeapProperties(&heap, nullptr))) {
        TrackResource(resource, owner, heap.Type);
    }
}

void Publish(ID3D12Device* device, uint64_t frameIndex) {
    const Metrics& metrics = GetMetrics();
    int64_t tracked = 0;
    for (size_t index = 0; index < kOwnerCount; ++index) {
        const int64_t bytes = gTrackedBytes[index].load();
        METRIC::Publish(metrics.owners[index], static_cast<double>(bytes), frameIndex);
        if (index != static_cast<size_t>(Owner::Upload)) {
            tracked += bytes;
        }
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
    const double untracked = static_cast<double>(memory.CurrentUsage) - static_cast<double>(tracked);
    METRIC::Publish(metrics.unattributed, untracked > 0.0 ? untracked : 0.0, frameIndex);
    METRIC::Publish(metrics.usage, static_cast<double>(memory.CurrentUsage), frameIndex);
    METRIC::Publish(metrics.budget, static_cast<double>(memory.Budget), frameIndex);
}

void Shutdown() {
    gState = {};
    METRIC::ClearOwner(kMetricOwner);
}

std::optional<AdapterInfo> GetAdapterInfo() { return gState.info; }

} // namespace HIKARI::GFX::VIDEO_MEMORY
