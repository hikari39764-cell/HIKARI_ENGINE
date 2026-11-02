#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <d3dx12.h>
#include <DirectXPackedVector.h>
#include <wrl/client.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

#include "RenderContracts/Lighting/HIKARI_LightProbeVolumeFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Gfx/HIKARI_VideoMemory.h"
#include "Render3D/Lighting/HIKARI_LightingProviderViewSelection.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::LIGHTPROBE {

IrradianceVolumeGpuData BuildIrradianceVolumeGpuData(const LightProbeVolumeRuntimeData& volume, float samplingMode) {
    IrradianceVolumeGpuData gpu{};
    gpu.originSampling = {volume.origin.x, volume.origin.y, volume.origin.z,
                          volume.dynamic && samplingMode > 0.0f ? 3.0f : samplingMode};
    gpu.spacingIntensity = {std::max(0.0001f, volume.spacing.x), std::max(0.0001f, volume.spacing.y),
                            std::max(0.0001f, volume.spacing.z), std::max(0.0f, volume.intensity)};
    gpu.countsProbeCount = {static_cast<float>(volume.countX), static_cast<float>(volume.countY),
                            static_cast<float>(volume.countZ), static_cast<float>(volume.probeCount)};
    gpu.sizeBlendDistance = {volume.size.x, volume.size.y, volume.size.z, std::max(0.0f, volume.blendDistance)};
    gpu.selectionParams = {static_cast<float>(volume.priority),
                           static_cast<float>(volume.visibilityTextureDescriptorIndex + 1u),
                           std::clamp(volume.normalBias, 0.0f, 0.5f), volume.visibilityDistanceScale};
    return gpu;
}

namespace {

constexpr uint32_t kShCoeffCount = GFX::DESCRIPTOR::kLightProbeShVolumeTextureCount;
constexpr DXGI_FORMAT kShVolumeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr uint32_t kUploadTextureCount = kShCoeffCount + 1u;
constexpr uint32_t kVisibilityTileSize = ASSETS::LIGHTING::kLightProbeVisibilityResolution + 2u;
static_assert(kShCoeffCount == ASSETS::LIGHTING::kLightProbeShCoeffCount);
static_assert(kLightProbeVolumeViewSlotCount == GFX::DESCRIPTOR::kLightProbeVolumeSetCount);
static_assert(kLightProbeVolumeResidentBudget == GFX::DESCRIPTOR::kLightProbeVolumeResidentSetCount);

struct HalfTexel {
    uint16_t r = 0;
    uint16_t g = 0;
    uint16_t b = 0;
    uint16_t a = 0;
};

struct ResidentVolume {
    LightProbeVolumeRuntimeData data{};
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kShCoeffCount> shVolumes{};
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer{};
    Microsoft::WRL::ComPtr<ID3D12Resource> visibilityTexture{};
    TextureResourceHandle visibilityResource{};
    ID3D12Device* volumeDevice = nullptr;
    ID3D12Device* srvDevice = nullptr;
    ID3D12DescriptorHeap* srvHeap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE srvTable{};
    bool srvReady = false;
};

std::vector<ResidentVolume> gResidents{};
size_t gResidentCount = 0;
LightProbeVolumeViewSelection gViewSelection{};
int gSamplingSuppressDepth = 0;
std::span<const LightProbeVolumeRuntimeData> gDynamicGiVolumes{};

UINT DescriptorBaseIndex(size_t residentIndex) {
    return GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::LightProbeShVolume0) +
           static_cast<UINT>(residentIndex * kShCoeffCount);
}

D3D12_CPU_DESCRIPTOR_HANDLE VolumeSrvCpuAt(size_t residentIndex, uint32_t coeffIndex) {
    ID3D12Device* device = GFX::GetCurrentContext().device;
    ID3D12DescriptorHeap* heap = GFX::GetCurrentContext().srvHeap;
    if (device == nullptr || heap == nullptr) {
        return {};
    }
    const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return GFX::DESCRIPTOR::CpuAt(heap, descriptorSize, DescriptorBaseIndex(residentIndex) + coeffIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE VolumeSrvTableGpu(size_t residentIndex) {
    ID3D12Device* device = GFX::GetCurrentContext().device;
    ID3D12DescriptorHeap* heap = GFX::GetCurrentContext().srvHeap;
    if (device == nullptr || heap == nullptr) {
        return {};
    }
    const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return GFX::DESCRIPTOR::GpuAt(heap, descriptorSize, DescriptorBaseIndex(residentIndex));
}

D3D12_SHADER_RESOURCE_VIEW_DESC MakeVolumeSrvDesc() {
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = kShVolumeFormat;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture3D.MostDetailedMip = 0;
    srv.Texture3D.MipLevels = 1;
    return srv;
}

void WriteNullSrvs(size_t residentIndex) {
    ID3D12Device* device = GFX::GetCurrentContext().device;
    if (device == nullptr || GFX::GetCurrentContext().srvHeap == nullptr) {
        return;
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC srv = MakeVolumeSrvDesc();
    for (uint32_t coeff = 0; coeff < kShCoeffCount; ++coeff) {
        device->CreateShaderResourceView(nullptr, &srv, VolumeSrvCpuAt(residentIndex, coeff));
    }
}

bool HasGpuResources(const ResidentVolume& resident) {
    if (resident.volumeDevice != GFX::GetCurrentContext().device) {
        return false;
    }
    return resident.visibilityTexture && IsTextureResourceValid(resident.visibilityResource) &&
           std::all_of(resident.shVolumes.begin(), resident.shVolumes.end(),
                       [](const auto& volume) { return volume != nullptr; });
}

void RetireResource(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, const char* debugName) {
    if (resource == nullptr) {
        return;
    }
    IUnknown* raw = resource.Detach();
    GFX::RetireD3D12ObjectForCurrentFrame(raw, debugName);
}

void RetireResident(ResidentVolume& resident) {
    if (resident.visibilityResource)
        ReleaseTextureResource(resident.visibilityResource);
    resident.visibilityResource = {};
    RetireResource(resident.visibilityTexture, "LightProbeVolume visibility texture");
    for (auto& volume : resident.shVolumes) {
        RetireResource(volume, "LightProbeVolume SH texture");
    }
    RetireResource(resident.uploadBuffer, "LightProbeVolume upload buffer");
    resident.volumeDevice = nullptr;
    resident.srvDevice = nullptr;
    resident.srvHeap = nullptr;
    resident.srvTable = {};
    resident.srvReady = false;
}

bool WriteVolumeSrvs(ResidentVolume& resident, size_t residentIndex) {
    ID3D12Device* device = GFX::GetCurrentContext().device;
    ID3D12DescriptorHeap* heap = GFX::GetCurrentContext().srvHeap;
    if (device == nullptr || heap == nullptr || !HasGpuResources(resident)) {
        resident.srvReady = false;
        return false;
    }
    const D3D12_SHADER_RESOURCE_VIEW_DESC srv = MakeVolumeSrvDesc();
    for (uint32_t coeff = 0; coeff < kShCoeffCount; ++coeff) {
        device->CreateShaderResourceView(resident.shVolumes[coeff].Get(), &srv, VolumeSrvCpuAt(residentIndex, coeff));
    }
    resident.srvDevice = device;
    resident.srvHeap = heap;
    resident.srvTable = VolumeSrvTableGpu(residentIndex);
    resident.srvReady = resident.srvTable.ptr != 0;
    return resident.srvReady;
}

HalfTexel ToHalfTexel(const MATH::Vec3& value) {
    using DirectX::PackedVector::XMConvertFloatToHalf;
    return HalfTexel{XMConvertFloatToHalf(value.x), XMConvertFloatToHalf(value.y), XMConvertFloatToHalf(value.z), 0u};
}

bool UploadVolumes(ResidentVolume& resident, size_t residentIndex,
                   const ASSETS::LIGHTING::LightProbeVolumeFileData& fileData, std::string& outMessage) {
    ID3D12Device* device = GFX::GetCurrentContext().device;
    ID3D12CommandQueue* queue = GFX::GetCurrentContext().queue;
    const uint32_t countX = fileData.countX, countY = fileData.countY, countZ = fileData.countZ;
    const size_t probeCount = static_cast<size_t>(countX) * countY * countZ;
    if (device == nullptr || queue == nullptr || probeCount == 0u ||
        !ASSETS::LIGHTING::ValidateLightProbeVolumeData(fileData, &outMessage)) {
        outMessage = "Light probe GPU upload context is missing.";
        return false;
    }

    const CD3DX12_RESOURCE_DESC volumeDesc =
        CD3DX12_RESOURCE_DESC::Tex3D(kShVolumeFormat, countX, countY, static_cast<UINT16>(countZ), 1u);
    const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
    std::array<D3D12_RESOURCE_DESC, kUploadTextureCount> descriptions;
    std::fill_n(descriptions.begin(), kShCoeffCount, volumeDesc);
    descriptions[kShCoeffCount] = CD3DX12_RESOURCE_DESC::Tex2D(
        DXGI_FORMAT_R32G32_FLOAT, static_cast<UINT64>(countX) * countY * kVisibilityTileSize,
        countZ * kVisibilityTileSize, 1u, 1u);
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, kUploadTextureCount> volumes{};
    for (uint32_t coeff = 0; coeff < kUploadTextureCount; ++coeff) {
        const HRESULT hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
            device, &defaultHeap, D3D12_HEAP_FLAG_NONE, &descriptions[coeff], D3D12_RESOURCE_STATE_COMMON, nullptr,
            GFX::VIDEO_MEMORY::Owner::Lighting, IID_PPV_ARGS(volumes[coeff].GetAddressOf()));
        if (!HIKARI_DX_CHECK(hr, "LightProbeVolumeRuntime::CreateShVolume")) {
            outMessage = "Failed to create light probe SH volume.";
            return false;
        }
    }

    std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT, kUploadTextureCount> footprints{};
    std::array<UINT, kUploadTextureCount> rowCounts{};
    std::array<UINT64, kUploadTextureCount> rowBytes{};
    UINT64 uploadBytes = 0;
    for (uint32_t coeff = 0; coeff < kUploadTextureCount; ++coeff) {
        UINT64 total = 0;
        device->GetCopyableFootprints(&descriptions[coeff], 0, 1, uploadBytes, &footprints[coeff], &rowCounts[coeff],
                                      &rowBytes[coeff], &total);
        uploadBytes = footprints[coeff].Offset + total;
        uploadBytes = (uploadBytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u) &
                      ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u);
    }

    const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
    const CD3DX12_RESOURCE_DESC uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBytes);
    Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer{};
    HRESULT hr = GFX::VIDEO_MEMORY::CreateCommittedResource(
        device, &uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        GFX::VIDEO_MEMORY::Owner::Lighting, IID_PPV_ARGS(uploadBuffer.GetAddressOf()));
    if (!HIKARI_DX_CHECK(hr, "LightProbeVolumeRuntime::CreateUploadBuffer")) {
        outMessage = "Failed to create light probe upload buffer.";
        return false;
    }

    uint8_t* mapped = nullptr;
    hr = uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr) || mapped == nullptr) {
        outMessage = "Failed to map light probe upload buffer.";
        return false;
    }
    for (uint32_t coeff = 0; coeff < kShCoeffCount; ++coeff) {
        const auto& footprint = footprints[coeff];
        for (uint32_t z = 0; z < countZ; ++z) {
            for (uint32_t y = 0; y < countY; ++y) {
                auto* row = reinterpret_cast<HalfTexel*>(mapped + footprint.Offset +
                                                         (static_cast<UINT64>(z) * rowCounts[coeff] + y) *
                                                             footprint.Footprint.RowPitch);
                for (uint32_t x = 0; x < countX; ++x) {
                    const size_t probeIndex = (static_cast<size_t>(z) * countY + y) * countX + x;
                    row[x] = ToHalfTexel(fileData.probes[probeIndex].coeffs[coeff]);
                }
            }
        }
    }
    // 各 octahedral tile の一画素境界を折り返し、隣の probe を補間しない。
    constexpr int side = ASSETS::LIGHTING::kLightProbeVisibilityResolution;
    const auto& visibilityFootprint = footprints[kShCoeffCount];
    for (uint32_t probe = 0; probe < probeCount; ++probe) {
        const uint32_t tileX = (probe % (countX * countY)) * kVisibilityTileSize;
        const uint32_t tileY = (probe / (countX * countY)) * kVisibilityTileSize;
        for (int y = 0; y < static_cast<int>(kVisibilityTileSize); ++y) {
            auto* row =
                reinterpret_cast<MATH::Vec2*>(mapped + visibilityFootprint.Offset +
                                              static_cast<size_t>(tileY + y) * visibilityFootprint.Footprint.RowPitch);
            for (int x = 0; x < static_cast<int>(kVisibilityTileSize); ++x) {
                int sx = x - 1, sy = y - 1;
                if (sx < 0) {
                    sx = 0;
                    sy = side - 1 - sy;
                } else if (sx >= side) {
                    sx = side - 1;
                    sy = side - 1 - sy;
                }
                if (sy < 0) {
                    sy = 0;
                    sx = side - 1 - sx;
                } else if (sy >= side) {
                    sy = side - 1;
                    sx = side - 1 - sx;
                }
                row[tileX + x] = fileData.visibilityMoments[static_cast<size_t>(probe) * side * side + sy * side + sx];
            }
        }
    }
    uploadBuffer->Unmap(0, nullptr);

    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator{};
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList{};
    Microsoft::WRL::ComPtr<ID3D12Fence> fence{};
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf()));
    if (FAILED(hr)) {
        outMessage = "Failed to create light probe upload allocator.";
        return false;
    }
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr,
                                   IID_PPV_ARGS(commandList.GetAddressOf()));
    if (FAILED(hr)) {
        outMessage = "Failed to create light probe upload list.";
        return false;
    }

    std::array<D3D12_RESOURCE_BARRIER, kUploadTextureCount> toCopy{};
    for (uint32_t coeff = 0; coeff < kUploadTextureCount; ++coeff) {
        toCopy[coeff] = CD3DX12_RESOURCE_BARRIER::Transition(volumes[coeff].Get(), D3D12_RESOURCE_STATE_COMMON,
                                                             D3D12_RESOURCE_STATE_COPY_DEST);
    }
    commandList->ResourceBarrier(kUploadTextureCount, toCopy.data());
    for (uint32_t coeff = 0; coeff < kUploadTextureCount; ++coeff) {
        const CD3DX12_TEXTURE_COPY_LOCATION dst(volumes[coeff].Get(), 0);
        const CD3DX12_TEXTURE_COPY_LOCATION src(uploadBuffer.Get(), footprints[coeff]);
        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    }
    std::array<D3D12_RESOURCE_BARRIER, kUploadTextureCount> toShader{};
    for (uint32_t coeff = 0; coeff < kUploadTextureCount; ++coeff) {
        toShader[coeff] = CD3DX12_RESOURCE_BARRIER::Transition(volumes[coeff].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                               D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
    }
    commandList->ResourceBarrier(kUploadTextureCount, toShader.data());
    hr = commandList->Close();
    if (FAILED(hr)) {
        outMessage = "Failed to close light probe upload list.";
        return false;
    }
    ID3D12CommandList* lists[] = {commandList.Get()};
    queue->ExecuteCommandLists(1, lists);

    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
    if (FAILED(hr) || FAILED(queue->Signal(fence.Get(), 1u))) {
        outMessage = "Failed to signal light probe upload.";
        return false;
    }
    if (fence->GetCompletedValue() < 1u) {
        HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (eventHandle == nullptr) {
            outMessage = "Failed to create light probe fence event.";
            return false;
        }
        hr = fence->SetEventOnCompletion(1u, eventHandle);
        if (SUCCEEDED(hr)) {
            WaitForSingleObject(eventHandle, INFINITE);
        }
        CloseHandle(eventHandle);
        if (FAILED(hr)) {
            outMessage = "Failed to wait for light probe upload.";
            return false;
        }
    }

    RenderResourceDesc visibilityDesc{};
    visibilityDesc.debugName = "LightProbeVolume.Visibility";
    const TextureResourceHandle visibilityResource =
        RegisterTextureResourceFromNative(volumes[kShCoeffCount].Get(), DXGI_FORMAT_R32G32_FLOAT, visibilityDesc);
    if (!visibilityResource ||
        GetTextureResourceSrvDescriptorIndex(visibilityResource) >= GFX::DESCRIPTOR::kUserSrvCount) {
        if (visibilityResource)
            ReleaseTextureResource(visibilityResource);
        outMessage = "Light probe visibility descriptor allocation failed.";
        return false;
    }
    RetireResident(resident);
    for (uint32_t coefficient = 0; coefficient < kShCoeffCount; ++coefficient)
        resident.shVolumes[coefficient] = std::move(volumes[coefficient]);
    resident.visibilityTexture = std::move(volumes[kShCoeffCount]);
    resident.visibilityResource = visibilityResource;
    resident.uploadBuffer = std::move(uploadBuffer);
    resident.volumeDevice = device;
    if (!WriteVolumeSrvs(resident, residentIndex)) {
        outMessage = "Light probe SRV descriptors are missing.";
        return false;
    }
    return true;
}

void RefreshValidity(ResidentVolume& resident, size_t residentIndex) {
    if (resident.srvDevice != GFX::GetCurrentContext().device || resident.srvHeap != GFX::GetCurrentContext().srvHeap ||
        resident.srvTable.ptr == 0) {
        WriteVolumeSrvs(resident, residentIndex);
    }
    resident.data.valid = resident.data.enabled && resident.data.probeCount > 0u && resident.data.countX > 0u &&
                          resident.data.countY > 0u && resident.data.countZ > 0u &&
                          std::isfinite(resident.data.intensity) && resident.data.intensity >= 0.0f &&
                          HasGpuResources(resident) && resident.srvReady;
    resident.data.visibilityTextureDescriptorIndex =
        resident.data.valid ? GetTextureResourceSrvDescriptorIndex(resident.visibilityResource) : UINT32_MAX;
}

bool LoadResident(ResidentVolume& resident, size_t residentIndex, const LightProbeVolumeLoadRequest& request,
                  std::string& outMessage) {
    ASSETS::LIGHTING::LightProbeVolumeFileData fileData{};
    if (!ASSETS::LIGHTING::LoadLightProbeVolumeFile(request.path, fileData, &outMessage)) {
        return false;
    }
    if (!UploadVolumes(resident, residentIndex, fileData, outMessage)) {
        return false;
    }
    resident.data.id = request.id;
    resident.data.enabled = true;
    resident.data.origin = fileData.origin;
    resident.data.size = fileData.size;
    resident.data.spacing = fileData.spacing;
    resident.data.countX = fileData.countX;
    resident.data.countY = fileData.countY;
    resident.data.countZ = fileData.countZ;
    resident.data.probeCount = static_cast<uint32_t>(fileData.probes.size());
    resident.data.visibilityDistanceScale = fileData.visibilityDistanceScale;
    resident.data.intensity = (std::max)(0.0f, request.intensity);
    resident.data.blendDistance = (std::max)(0.0f, request.blendDistance);
    resident.data.normalBias = std::clamp(request.normalBias, 0.0f, 0.5f);
    resident.data.priority = request.priority;
    resident.data.sourcePath = request.path.lexically_normal().generic_string();
    RefreshValidity(resident, residentIndex);
    return resident.data.valid;
}

} // namespace

void Reset() {
    for (ResidentVolume& resident : gResidents) {
        RetireResident(resident);
    }
    gResidents.clear();
    for (size_t residentIndex = 0; residentIndex < kLightProbeVolumeResidentBudget; ++residentIndex) {
        WriteNullSrvs(residentIndex);
    }
    gResidentCount = 0;
    gViewSelection = {};
}

bool LoadLightProbeVolumeSet(std::vector<LightProbeVolumeLoadRequest> requests, std::vector<std::string>* outMessages) {
    std::stable_sort(requests.begin(), requests.end(),
                     [](const LightProbeVolumeLoadRequest& lhs, const LightProbeVolumeLoadRequest& rhs) {
                         if (lhs.priority != rhs.priority) {
                             return lhs.priority > rhs.priority;
                         }
                         return lhs.id < rhs.id;
                     });
    Reset();

    const size_t loadCount = (std::min)(requests.size(), kLightProbeVolumeResidentBudget);
    gResidents.reserve(loadCount);
    for (size_t index = 0; index < loadCount; ++index) {
        std::string message{};
        ResidentVolume resident{};
        const size_t residentIndex = gResidents.size();
        if (LoadResident(resident, residentIndex, requests[index], message)) {
            gResidents.push_back(std::move(resident));
            ++gResidentCount;
            HIKARI_LOG_INFO("[Environment][IrradianceVolumeSet] loaded id=" + requests[index].id +
                            " path=" + requests[index].path.generic_string());
        } else {
            RetireResident(resident);
            HIKARI_LOG_WARN("[Environment][IrradianceVolumeSet] failed id=" + requests[index].id +
                            " message=" + message);
        }
        if (outMessages != nullptr && !message.empty()) {
            outMessages->push_back(message);
        }
    }
    if (outMessages != nullptr && requests.size() > kLightProbeVolumeResidentBudget) {
        outMessages->push_back(
            "[IrradianceVolumeSet] resident budget=" + std::to_string(kLightProbeVolumeResidentBudget) +
            " requested=" + std::to_string(requests.size()));
    }
    return IsValid();
}

const std::vector<LightProbeVolumeRuntimeData>& GetVolumeSet() {
    static std::vector<LightProbeVolumeRuntimeData> snapshot{};
    snapshot.clear();
    for (size_t index = 0; index < gResidents.size(); ++index) {
        RefreshValidity(gResidents[index], index);
        if (!gResidents[index].data.id.empty()) {
            snapshot.push_back(gResidents[index].data);
        }
    }
    return snapshot;
}

void PrepareView(const MATH::Vec3& cameraPosition, const MATH::Vec3& viewFocusPosition) {
    const LightProbeVolumeViewSelection previousSelection = gViewSelection;
    gViewSelection = {};
    if (IsLightProbeVolumeSamplingSuppressed()) {
        return;
    }

    struct Candidate {
        size_t index = 0;
        float influence = 0.0f;
        float distanceSquared = std::numeric_limits<float>::max();
    };
    const LIGHTING::LightingProviderViewSegment view{cameraPosition, viewFocusPosition};
    std::vector<Candidate> candidates{};
    for (size_t index = 0; index < gResidents.size(); ++index) {
        ResidentVolume& resident = gResidents[index];
        RefreshValidity(resident, index);
        if (!resident.data.valid) {
            continue;
        }
        const MATH::Vec3 center{resident.data.origin.x + resident.data.size.x * 0.5f,
                                resident.data.origin.y + resident.data.size.y * 0.5f,
                                resident.data.origin.z + resident.data.size.z * 0.5f};
        // 影響は格子の外へ半間隔、そこから blendDistance で消える（HikariEvaluateBakedVolumeInfluence）。
        const float reach = 2.0f * (std::max)(resident.data.blendDistance, 0.0f);
        const MATH::Vec3 influenceSize{resident.data.size.x + resident.data.spacing.x + reach,
                                       resident.data.size.y + resident.data.spacing.y + reach,
                                       resident.data.size.z + resident.data.spacing.z + reach};
        candidates.push_back(Candidate{
            index, LIGHTING::EvaluateBoxInfluenceAlongView(view, center, influenceSize, resident.data.blendDistance),
            LIGHTING::DistanceSquaredToViewSegment(view, center)});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
        const bool lhsAffects = lhs.influence > 0.0001f;
        const bool rhsAffects = rhs.influence > 0.0001f;
        if (lhsAffects != rhsAffects) {
            return lhsAffects;
        }
        const auto& lhsVolume = gResidents[lhs.index].data;
        const auto& rhsVolume = gResidents[rhs.index].data;
        if (lhsVolume.priority != rhsVolume.priority) {
            return lhsVolume.priority > rhsVolume.priority;
        }
        if (lhs.influence != rhs.influence) {
            return lhs.influence > rhs.influence;
        }
        if (lhs.distanceSquared != rhs.distanceSquared) {
            return lhs.distanceSquared < rhs.distanceSquared;
        }
        return lhsVolume.id < rhsVolume.id;
    });

    std::vector<Candidate> selected{};
    selected.reserve(kLightProbeVolumeViewSlotCount);
    if (!candidates.empty()) {
        selected.push_back(candidates.front());
    }
    constexpr float kRetainInfluence = 0.05f;
    for (size_t previousSlot = 0;
         previousSlot < previousSelection.count && selected.size() < kLightProbeVolumeViewSlotCount; ++previousSlot) {
        const size_t previousIndex = previousSelection.providerIndices[previousSlot];
        const auto found =
            std::find_if(candidates.begin(), candidates.end(),
                         [previousIndex](const Candidate& candidate) { return candidate.index == previousIndex; });
        const bool alreadySelected =
            std::any_of(selected.begin(), selected.end(),
                        [previousIndex](const Candidate& candidate) { return candidate.index == previousIndex; });
        if (found != candidates.end() && found->influence >= kRetainInfluence && !alreadySelected) {
            selected.push_back(*found);
        }
    }
    for (const Candidate& candidate : candidates) {
        if (selected.size() >= kLightProbeVolumeViewSlotCount) {
            break;
        }
        const bool alreadySelected =
            std::any_of(selected.begin(), selected.end(),
                        [&candidate](const Candidate& current) { return current.index == candidate.index; });
        if (!alreadySelected) {
            selected.push_back(candidate);
        }
    }

    gViewSelection.count = selected.size();
    for (size_t slot = 0; slot < gViewSelection.count; ++slot) {
        gViewSelection.providerIndices[slot] = selected[slot].index;
        gViewSelection.viewInfluences[slot] = selected[slot].influence;
    }
}

const LightProbeVolumeViewSelection& GetViewSelection() { return gViewSelection; }

const LightProbeVolumeRuntimeData* GetViewVolume(size_t slot) {
    if (slot >= kLightProbeVolumeViewSlotCount || IsLightProbeVolumeSamplingSuppressed())
        return nullptr;
    if (slot >= gViewSelection.count) {
        return nullptr;
    }
    const size_t residentIndex = gViewSelection.providerIndices[slot];
    return residentIndex < gResidents.size() ? &gResidents[residentIndex].data : nullptr;
}

bool IsLightProbeVolumeSamplingSuppressed() { return gSamplingSuppressDepth > 0; }

ScopedDynamicGiVolumes::ScopedDynamicGiVolumes(std::span<const LightProbeVolumeRuntimeData> volumes)
    : previous_(gDynamicGiVolumes) {
    gDynamicGiVolumes = volumes.first((std::min)(volumes.size(), kDynamicGiVolumeCount));
}

ScopedDynamicGiVolumes::~ScopedDynamicGiVolumes() { gDynamicGiVolumes = previous_; }

std::span<const LightProbeVolumeRuntimeData> GetDynamicGiVolumes() {
    return IsLightProbeVolumeSamplingSuppressed() ? std::span<const LightProbeVolumeRuntimeData>{} : gDynamicGiVolumes;
}

ScopedLightProbeVolumeSamplingSuppress::ScopedLightProbeVolumeSamplingSuppress() {
    previousSelection_ = gViewSelection;
    ++gSamplingSuppressDepth;
}

ScopedLightProbeVolumeSamplingSuppress::~ScopedLightProbeVolumeSamplingSuppress() {
    gSamplingSuppressDepth = (std::max)(0, gSamplingSuppressDepth - 1);
    gViewSelection = previousSelection_;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetViewShVolumeSrvTable(size_t slot) {
    if (slot >= kLightProbeVolumeViewSlotCount) {
        return {};
    }
    if (IsLightProbeVolumeSamplingSuppressed() || slot >= gViewSelection.count) {
        return VolumeSrvTableGpu(slot);
    }
    const size_t residentIndex = gViewSelection.providerIndices[slot];
    if (residentIndex >= gResidents.size()) {
        return {};
    }
    RefreshValidity(gResidents[residentIndex], residentIndex);
    return gResidents[residentIndex].data.valid ? gResidents[residentIndex].srvTable : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

LightProbeVolumeDebugState GetDebugState() {
    LightProbeVolumeDebugState state{};
    state.residentCount = static_cast<uint32_t>(gResidentCount);
    state.selectedCount = static_cast<uint32_t>(gViewSelection.count);
    const LightProbeVolumeRuntimeData* selected = GetViewVolume(0u);
    const size_t residentIndex = gViewSelection.count > 0u ? gViewSelection.providerIndices[0] : 0u;
    if (selected == nullptr || residentIndex >= gResidents.size()) {
        return state;
    }
    const ResidentVolume& resident = gResidents[residentIndex];
    state.valid = selected->valid;
    state.srvReady = resident.srvReady;
    state.hasBuffer = HasGpuResources(resident);
    state.probeCount = selected->probeCount;
    state.countX = selected->countX;
    state.countY = selected->countY;
    state.countZ = selected->countZ;
    state.srvHeapPtr = resident.srvTable.ptr;
    state.bufferPtr = resident.shVolumes[0] != nullptr ? reinterpret_cast<uint64_t>(resident.shVolumes[0].Get()) : 0u;
    state.sourcePath = selected->sourcePath;
    return state;
}

bool IsValid() {
    for (size_t index = 0; index < gResidents.size(); ++index) {
        RefreshValidity(gResidents[index], index);
        if (gResidents[index].data.valid) {
            return true;
        }
    }
    return false;
}

} // namespace HIKARI::RENDER3D::LIGHTPROBE
