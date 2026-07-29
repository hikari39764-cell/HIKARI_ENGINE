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

#include "Assets/Lighting/HIKARI_LightProbeVolumeFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Lighting/HIKARI_LightingProviderViewSelection.h"

namespace HIKARI::RENDER3D::LIGHTPROBE {

    namespace {

        constexpr uint32_t kShCoeffCount =
            GFX::DESCRIPTOR::kLightProbeShVolumeTextureCount;
        constexpr DXGI_FORMAT kShVolumeFormat =
            DXGI_FORMAT_R16G16B16A16_FLOAT;
        static_assert(
            kShCoeffCount ==
                ASSETS::LIGHTING::kLightProbeShCoeffCount);
        static_assert(
            kLightProbeVolumeViewSlotCount ==
                GFX::DESCRIPTOR::kLightProbeVolumeSetCount);
        static_assert(
            kLightProbeVolumeResidentBudget ==
                GFX::DESCRIPTOR::
                    kLightProbeVolumeResidentSetCount);

        struct HalfTexel {
            uint16_t r = 0;
            uint16_t g = 0;
            uint16_t b = 0;
            uint16_t a = 0;
        };

        struct ResidentVolume {
            LightProbeVolumeRuntimeData data{};
            std::array<
                Microsoft::WRL::ComPtr<ID3D12Resource>,
                kShCoeffCount> shVolumes{};
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer{};
            std::vector<MATH::Vec3> cpuShCoeffs{};
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

        UINT DescriptorBaseIndex(size_t residentIndex) {
            return GFX::DESCRIPTOR::ToIndex(
                GFX::DESCRIPTOR::SystemSrv::LightProbeShVolume0) +
                static_cast<UINT>(
                    residentIndex * kShCoeffCount);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE VolumeSrvCpuAt(
            size_t residentIndex,
            uint32_t coeffIndex) {

            ID3D12Device* device = GFX::GetCurrentContext().device;
            ID3D12DescriptorHeap* heap =
                GFX::GetCurrentContext().srvHeap;
            if (device == nullptr || heap == nullptr) {
                return {};
            }
            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return GFX::DESCRIPTOR::CpuAt(
                heap,
                descriptorSize,
                DescriptorBaseIndex(residentIndex) + coeffIndex);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE VolumeSrvTableGpu(
            size_t residentIndex) {

            ID3D12Device* device = GFX::GetCurrentContext().device;
            ID3D12DescriptorHeap* heap =
                GFX::GetCurrentContext().srvHeap;
            if (device == nullptr || heap == nullptr) {
                return {};
            }
            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return GFX::DESCRIPTOR::GpuAt(
                heap,
                descriptorSize,
                DescriptorBaseIndex(residentIndex));
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC MakeVolumeSrvDesc() {
            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = kShVolumeFormat;
            srv.ViewDimension =
                D3D12_SRV_DIMENSION_TEXTURE3D;
            srv.Shader4ComponentMapping =
                D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Texture3D.MostDetailedMip = 0;
            srv.Texture3D.MipLevels = 1;
            return srv;
        }

        void WriteNullSrvs(size_t residentIndex) {
            ID3D12Device* device = GFX::GetCurrentContext().device;
            if (device == nullptr ||
                GFX::GetCurrentContext().srvHeap == nullptr) {
                return;
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srv =
                MakeVolumeSrvDesc();
            for (uint32_t coeff = 0;
                 coeff < kShCoeffCount;
                 ++coeff) {
                device->CreateShaderResourceView(
                    nullptr,
                    &srv,
                    VolumeSrvCpuAt(residentIndex, coeff));
            }
        }

        bool HasGpuResources(const ResidentVolume& resident) {
            if (resident.volumeDevice !=
                    GFX::GetCurrentContext().device) {
                return false;
            }
            return std::all_of(
                resident.shVolumes.begin(),
                resident.shVolumes.end(),
                [](const auto& volume) {
                    return volume != nullptr;
                });
        }

        void RetireResource(
            Microsoft::WRL::ComPtr<ID3D12Resource>& resource,
            const char* debugName) {

            if (resource == nullptr) {
                return;
            }
            IUnknown* raw = resource.Detach();
            GFX::RetireD3D12ObjectForCurrentFrame(
                raw,
                debugName);
        }

        void RetireResident(ResidentVolume& resident) {
            for (auto& volume : resident.shVolumes) {
                RetireResource(
                    volume,
                    "LightProbeVolume SH texture");
            }
            RetireResource(
                resident.uploadBuffer,
                "LightProbeVolume upload buffer");
            resident.volumeDevice = nullptr;
            resident.srvDevice = nullptr;
            resident.srvHeap = nullptr;
            resident.srvTable = {};
            resident.srvReady = false;
        }

        bool WriteVolumeSrvs(
            ResidentVolume& resident,
            size_t residentIndex) {

            ID3D12Device* device = GFX::GetCurrentContext().device;
            ID3D12DescriptorHeap* heap =
                GFX::GetCurrentContext().srvHeap;
            if (device == nullptr || heap == nullptr ||
                !HasGpuResources(resident)) {
                resident.srvReady = false;
                return false;
            }
            const D3D12_SHADER_RESOURCE_VIEW_DESC srv =
                MakeVolumeSrvDesc();
            for (uint32_t coeff = 0;
                 coeff < kShCoeffCount;
                 ++coeff) {
                device->CreateShaderResourceView(
                    resident.shVolumes[coeff].Get(),
                    &srv,
                    VolumeSrvCpuAt(residentIndex, coeff));
            }
            resident.srvDevice = device;
            resident.srvHeap = heap;
            resident.srvTable =
                VolumeSrvTableGpu(residentIndex);
            resident.srvReady =
                resident.srvTable.ptr != 0;
            return resident.srvReady;
        }

        HalfTexel ToHalfTexel(const MATH::Vec3& value) {
            using DirectX::PackedVector::XMConvertFloatToHalf;
            return HalfTexel{
                XMConvertFloatToHalf(value.x),
                XMConvertFloatToHalf(value.y),
                XMConvertFloatToHalf(value.z),
                0u
            };
        }

        std::vector<MATH::Vec3> BuildCpuCoeffs(
            const ASSETS::LIGHTING::LightProbeVolumeFileData&
                fileData) {

            std::vector<MATH::Vec3> coeffs{};
            coeffs.reserve(
                fileData.probes.size() * kShCoeffCount);
            for (const ASSETS::LIGHTING::LightProbeSh9& probe :
                 fileData.probes) {
                for (const MATH::Vec3& coeff : probe.coeffs) {
                    coeffs.push_back(coeff);
                }
            }
            return coeffs;
        }

        bool UploadVolumes(
            ResidentVolume& resident,
            size_t residentIndex,
            const std::vector<MATH::Vec3>& coeffs,
            uint32_t countX,
            uint32_t countY,
            uint32_t countZ,
            std::string& outMessage) {

            ID3D12Device* device = GFX::GetCurrentContext().device;
            ID3D12CommandQueue* queue =
                GFX::GetCurrentContext().queue;
            const size_t probeCount =
                static_cast<size_t>(countX) *
                countY *
                countZ;
            if (device == nullptr || queue == nullptr ||
                probeCount == 0u ||
                coeffs.size() !=
                    probeCount * kShCoeffCount) {
                outMessage =
                    "Light probe GPU upload context is missing.";
                return false;
            }

            const CD3DX12_RESOURCE_DESC volumeDesc =
                CD3DX12_RESOURCE_DESC::Tex3D(
                    kShVolumeFormat,
                    countX,
                    countY,
                    static_cast<UINT16>(countZ),
                    1u);
            const CD3DX12_HEAP_PROPERTIES defaultHeap(
                D3D12_HEAP_TYPE_DEFAULT);
            std::array<
                Microsoft::WRL::ComPtr<ID3D12Resource>,
                kShCoeffCount> volumes{};
            for (uint32_t coeff = 0;
                 coeff < kShCoeffCount;
                 ++coeff) {
                const HRESULT hr =
                    device->CreateCommittedResource(
                        &defaultHeap,
                        D3D12_HEAP_FLAG_NONE,
                        &volumeDesc,
                        D3D12_RESOURCE_STATE_COMMON,
                        nullptr,
                        IID_PPV_ARGS(
                            volumes[coeff].GetAddressOf()));
                if (!HIKARI_DX_CHECK(
                        hr,
                        "LightProbeVolumeRuntime::CreateShVolume")) {
                    outMessage =
                        "Failed to create light probe SH volume.";
                    return false;
                }
            }

            std::array<
                D3D12_PLACED_SUBRESOURCE_FOOTPRINT,
                kShCoeffCount> footprints{};
            std::array<UINT, kShCoeffCount> rowCounts{};
            std::array<UINT64, kShCoeffCount> rowBytes{};
            UINT64 uploadBytes = 0;
            for (uint32_t coeff = 0;
                 coeff < kShCoeffCount;
                 ++coeff) {
                UINT64 total = 0;
                device->GetCopyableFootprints(
                    &volumeDesc,
                    0,
                    1,
                    uploadBytes,
                    &footprints[coeff],
                    &rowCounts[coeff],
                    &rowBytes[coeff],
                    &total);
                uploadBytes =
                    footprints[coeff].Offset + total;
                uploadBytes =
                    (uploadBytes +
                        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT -
                        1u) &
                    ~static_cast<UINT64>(
                        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT -
                        1u);
            }

            const CD3DX12_HEAP_PROPERTIES uploadHeap(
                D3D12_HEAP_TYPE_UPLOAD);
            const CD3DX12_RESOURCE_DESC uploadDesc =
                CD3DX12_RESOURCE_DESC::Buffer(uploadBytes);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer{};
            HRESULT hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadBuffer.GetAddressOf()));
            if (!HIKARI_DX_CHECK(
                    hr,
                    "LightProbeVolumeRuntime::CreateUploadBuffer")) {
                outMessage =
                    "Failed to create light probe upload buffer.";
                return false;
            }

            uint8_t* mapped = nullptr;
            hr = uploadBuffer->Map(
                0,
                nullptr,
                reinterpret_cast<void**>(&mapped));
            if (FAILED(hr) || mapped == nullptr) {
                outMessage =
                    "Failed to map light probe upload buffer.";
                return false;
            }
            for (uint32_t coeff = 0;
                 coeff < kShCoeffCount;
                 ++coeff) {
                const auto& footprint =
                    footprints[coeff];
                for (uint32_t z = 0; z < countZ; ++z) {
                    for (uint32_t y = 0; y < countY; ++y) {
                        auto* row =
                            reinterpret_cast<HalfTexel*>(
                                mapped +
                                footprint.Offset +
                                (static_cast<UINT64>(z) *
                                    rowCounts[coeff] +
                                    y) *
                                    footprint.Footprint.RowPitch);
                        for (uint32_t x = 0;
                             x < countX;
                             ++x) {
                            const size_t probeIndex =
                                (static_cast<size_t>(z) *
                                    countY +
                                    y) *
                                    countX +
                                x;
                            row[x] = ToHalfTexel(
                                coeffs[
                                    probeIndex *
                                        kShCoeffCount +
                                    coeff]);
                        }
                    }
                }
            }
            uploadBuffer->Unmap(0, nullptr);

            Microsoft::WRL::ComPtr<ID3D12CommandAllocator>
                allocator{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>
                commandList{};
            Microsoft::WRL::ComPtr<ID3D12Fence> fence{};
            hr = device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(allocator.GetAddressOf()));
            if (FAILED(hr)) {
                outMessage =
                    "Failed to create light probe upload allocator.";
                return false;
            }
            hr = device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(commandList.GetAddressOf()));
            if (FAILED(hr)) {
                outMessage =
                    "Failed to create light probe upload list.";
                return false;
            }

            std::array<
                D3D12_RESOURCE_BARRIER,
                kShCoeffCount> toCopy{};
            for (uint32_t coeff = 0;
                 coeff < kShCoeffCount;
                 ++coeff) {
                toCopy[coeff] =
                    CD3DX12_RESOURCE_BARRIER::Transition(
                        volumes[coeff].Get(),
                        D3D12_RESOURCE_STATE_COMMON,
                        D3D12_RESOURCE_STATE_COPY_DEST);
            }
            commandList->ResourceBarrier(
                kShCoeffCount,
                toCopy.data());
            for (uint32_t coeff = 0;
                 coeff < kShCoeffCount;
                 ++coeff) {
                const CD3DX12_TEXTURE_COPY_LOCATION dst(
                    volumes[coeff].Get(),
                    0);
                const CD3DX12_TEXTURE_COPY_LOCATION src(
                    uploadBuffer.Get(),
                    footprints[coeff]);
                commandList->CopyTextureRegion(
                    &dst,
                    0,
                    0,
                    0,
                    &src,
                    nullptr);
            }
            std::array<
                D3D12_RESOURCE_BARRIER,
                kShCoeffCount> toShader{};
            for (uint32_t coeff = 0;
                 coeff < kShCoeffCount;
                 ++coeff) {
                toShader[coeff] =
                    CD3DX12_RESOURCE_BARRIER::Transition(
                        volumes[coeff].Get(),
                        D3D12_RESOURCE_STATE_COPY_DEST,
                        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            commandList->ResourceBarrier(
                kShCoeffCount,
                toShader.data());
            hr = commandList->Close();
            if (FAILED(hr)) {
                outMessage =
                    "Failed to close light probe upload list.";
                return false;
            }
            ID3D12CommandList* lists[] = {
                commandList.Get()
            };
            queue->ExecuteCommandLists(1, lists);

            hr = device->CreateFence(
                0,
                D3D12_FENCE_FLAG_NONE,
                IID_PPV_ARGS(fence.GetAddressOf()));
            if (FAILED(hr) ||
                FAILED(queue->Signal(fence.Get(), 1u))) {
                outMessage =
                    "Failed to signal light probe upload.";
                return false;
            }
            if (fence->GetCompletedValue() < 1u) {
                HANDLE eventHandle =
                    CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (eventHandle == nullptr) {
                    outMessage =
                        "Failed to create light probe fence event.";
                    return false;
                }
                hr = fence->SetEventOnCompletion(
                    1u,
                    eventHandle);
                if (SUCCEEDED(hr)) {
                    WaitForSingleObject(
                        eventHandle,
                        INFINITE);
                }
                CloseHandle(eventHandle);
                if (FAILED(hr)) {
                    outMessage =
                        "Failed to wait for light probe upload.";
                    return false;
                }
            }

            RetireResident(resident);
            resident.shVolumes = std::move(volumes);
            resident.uploadBuffer = std::move(uploadBuffer);
            resident.volumeDevice = device;
            resident.cpuShCoeffs = coeffs;
            if (!WriteVolumeSrvs(
                    resident,
                    residentIndex)) {
                outMessage =
                    "Light probe SRV descriptors are missing.";
                return false;
            }
            return true;
        }

        void RefreshValidity(
            ResidentVolume& resident,
            size_t residentIndex) {

            if (resident.srvDevice !=
                    GFX::GetCurrentContext().device ||
                resident.srvHeap !=
                    GFX::GetCurrentContext().srvHeap ||
                resident.srvTable.ptr == 0) {
                WriteVolumeSrvs(
                    resident,
                    residentIndex);
            }
            resident.data.valid =
                resident.data.enabled &&
                resident.data.probeCount > 0u &&
                resident.data.countX > 0u &&
                resident.data.countY > 0u &&
                resident.data.countZ > 0u &&
                resident.data.intensity > 0.0f &&
                HasGpuResources(resident) &&
                resident.srvReady;
        }

        bool LoadResident(
            ResidentVolume& resident,
            size_t residentIndex,
            const LightProbeVolumeLoadRequest& request,
            std::string& outMessage) {

            ASSETS::LIGHTING::LightProbeVolumeFileData fileData{};
            if (!ASSETS::LIGHTING::LoadLightProbeVolumeFile(
                    request.path,
                    fileData,
                    &outMessage)) {
                return false;
            }
            const std::vector<MATH::Vec3> coeffs =
                BuildCpuCoeffs(fileData);
            if (!UploadVolumes(
                    resident,
                    residentIndex,
                    coeffs,
                    fileData.countX,
                    fileData.countY,
                    fileData.countZ,
                    outMessage)) {
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
            resident.data.probeCount =
                static_cast<uint32_t>(
                    fileData.probes.size());
            resident.data.intensity =
                (std::max)(0.0f, request.intensity);
            resident.data.blendDistance =
                (std::max)(0.0f, request.blendDistance);
            resident.data.normalBias =
                std::clamp(request.normalBias, 0.0f, 0.5f);
            resident.data.priority = request.priority;
            resident.data.sourcePath =
                request.path.lexically_normal().generic_string();
            RefreshValidity(
                resident,
                residentIndex);
            return resident.data.valid;
        }

    } // namespace

    void Reset() {
        for (ResidentVolume& resident : gResidents) {
            RetireResident(resident);
        }
        gResidents.clear();
        for (size_t residentIndex = 0;
             residentIndex <
                kLightProbeVolumeResidentBudget;
             ++residentIndex) {
            WriteNullSrvs(residentIndex);
        }
        gResidentCount = 0;
        gViewSelection = {};
    }

    bool LoadLightProbeVolumeSet(
        std::vector<LightProbeVolumeLoadRequest> requests,
        std::vector<std::string>* outMessages) {

        std::stable_sort(
            requests.begin(),
            requests.end(),
            [](const LightProbeVolumeLoadRequest& lhs,
               const LightProbeVolumeLoadRequest& rhs) {
                if (lhs.priority != rhs.priority) {
                    return lhs.priority > rhs.priority;
                }
                return lhs.id < rhs.id;
            });
        Reset();

        const size_t loadCount = (std::min)(
            requests.size(),
            kLightProbeVolumeResidentBudget);
        gResidents.reserve(loadCount);
        for (size_t index = 0; index < loadCount; ++index) {
            std::string message{};
            ResidentVolume resident{};
            const size_t residentIndex =
                gResidents.size();
            if (LoadResident(
                    resident,
                    residentIndex,
                    requests[index],
                    message)) {
                gResidents.push_back(std::move(resident));
                ++gResidentCount;
                HIKARI_LOG_INFO(
                    "[Environment][IrradianceVolumeSet] loaded id=" +
                    requests[index].id +
                    " path=" +
                    requests[index].path.generic_string());
            } else {
                RetireResident(resident);
                HIKARI_LOG_WARN(
                    "[Environment][IrradianceVolumeSet] failed id=" +
                    requests[index].id +
                    " message=" + message);
            }
            if (outMessages != nullptr && !message.empty()) {
                outMessages->push_back(message);
            }
        }
        if (outMessages != nullptr &&
            requests.size() >
                kLightProbeVolumeResidentBudget) {
            outMessages->push_back(
                "[IrradianceVolumeSet] resident budget=" +
                std::to_string(
                    kLightProbeVolumeResidentBudget) +
                " requested=" +
                std::to_string(requests.size()));
        }
        return IsValid();
    }

    const std::vector<LightProbeVolumeRuntimeData>& GetVolumeSet() {
        static std::vector<LightProbeVolumeRuntimeData> snapshot{};
        snapshot.clear();
        for (size_t index = 0;
             index < gResidents.size();
             ++index) {
            RefreshValidity(
                gResidents[index],
                index);
            if (!gResidents[index].data.id.empty()) {
                snapshot.push_back(gResidents[index].data);
            }
        }
        return snapshot;
    }

    void PrepareView(
        const MATH::Vec3& cameraPosition,
        const MATH::Vec3& viewFocusPosition) {
        const LightProbeVolumeViewSelection previousSelection =
            gViewSelection;
        gViewSelection = {};
        if (IsLightProbeVolumeSamplingSuppressed()) {
            return;
        }

        struct Candidate {
            size_t index = 0;
            float influence = 0.0f;
            float distanceSquared =
                std::numeric_limits<float>::max();
        };
        const LIGHTING::LightingProviderViewSegment view{
            cameraPosition,
            viewFocusPosition
        };
        std::vector<Candidate> candidates{};
        for (size_t index = 0;
             index < gResidents.size();
            ++index) {
            ResidentVolume& resident = gResidents[index];
            RefreshValidity(
                resident,
                index);
            if (!resident.data.valid) {
                continue;
            }
            const MATH::Vec3 center{
                resident.data.origin.x +
                    resident.data.size.x * 0.5f,
                resident.data.origin.y +
                    resident.data.size.y * 0.5f,
                resident.data.origin.z +
                    resident.data.size.z * 0.5f
            };
            candidates.push_back(Candidate{
                index,
                LIGHTING::EvaluateBoxInfluenceAlongView(
                    view,
                    center,
                    resident.data.size,
                    resident.data.blendDistance),
                LIGHTING::DistanceSquaredToViewSegment(
                    view,
                    center)
            });
        }
        std::stable_sort(
            candidates.begin(),
            candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
                const bool lhsAffects =
                    lhs.influence > 0.0001f;
                const bool rhsAffects =
                    rhs.influence > 0.0001f;
                if (lhsAffects != rhsAffects) {
                    return lhsAffects;
                }
                const auto& lhsVolume =
                    gResidents[lhs.index].data;
                const auto& rhsVolume =
                    gResidents[rhs.index].data;
                if (lhsVolume.priority != rhsVolume.priority) {
                    return lhsVolume.priority >
                        rhsVolume.priority;
                }
                if (lhs.influence != rhs.influence) {
                    return lhs.influence > rhs.influence;
                }
                if (lhs.distanceSquared !=
                    rhs.distanceSquared) {
                    return lhs.distanceSquared <
                        rhs.distanceSquared;
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
             previousSlot < previousSelection.count &&
                selected.size() <
                    kLightProbeVolumeViewSlotCount;
             ++previousSlot) {
            const size_t previousIndex =
                previousSelection.providerIndices[
                    previousSlot];
            const auto found = std::find_if(
                candidates.begin(),
                candidates.end(),
                [previousIndex](const Candidate& candidate) {
                    return candidate.index == previousIndex;
                });
            const bool alreadySelected =
                std::any_of(
                    selected.begin(),
                    selected.end(),
                    [previousIndex](
                        const Candidate& candidate) {
                        return candidate.index ==
                            previousIndex;
                    });
            if (found != candidates.end() &&
                found->influence >= kRetainInfluence &&
                !alreadySelected) {
                selected.push_back(*found);
            }
        }
        for (const Candidate& candidate : candidates) {
            if (selected.size() >=
                kLightProbeVolumeViewSlotCount) {
                break;
            }
            const bool alreadySelected =
                std::any_of(
                    selected.begin(),
                    selected.end(),
                    [&candidate](
                        const Candidate& current) {
                        return current.index ==
                            candidate.index;
                    });
            if (!alreadySelected) {
                selected.push_back(candidate);
            }
        }

        gViewSelection.count = selected.size();
        for (size_t slot = 0;
             slot < gViewSelection.count;
             ++slot) {
            gViewSelection.providerIndices[slot] =
                selected[slot].index;
            gViewSelection.viewInfluences[slot] =
                selected[slot].influence;
        }
    }

    const LightProbeVolumeViewSelection& GetViewSelection() {
        return gViewSelection;
    }

    const LightProbeVolumeRuntimeData* GetViewVolume(
        size_t slot) {

        if (slot >= gViewSelection.count) {
            return nullptr;
        }
        const size_t residentIndex =
            gViewSelection.providerIndices[slot];
        return residentIndex < gResidents.size()
            ? &gResidents[residentIndex].data
            : nullptr;
    }

    bool IsLightProbeVolumeSamplingSuppressed() {
        return gSamplingSuppressDepth > 0;
    }

    ScopedLightProbeVolumeSamplingSuppress::
        ScopedLightProbeVolumeSamplingSuppress() {
        ++gSamplingSuppressDepth;
    }

    ScopedLightProbeVolumeSamplingSuppress::
        ~ScopedLightProbeVolumeSamplingSuppress() {
        gSamplingSuppressDepth =
            (std::max)(0, gSamplingSuppressDepth - 1);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetViewShVolumeSrvTable(
        size_t slot) {

        if (slot >= kLightProbeVolumeViewSlotCount) {
            return {};
        }
        if (IsLightProbeVolumeSamplingSuppressed() ||
            slot >= gViewSelection.count) {
            return VolumeSrvTableGpu(slot);
        }
        const size_t residentIndex =
            gViewSelection.providerIndices[slot];
        if (residentIndex >= gResidents.size()) {
            return {};
        }
        RefreshValidity(
            gResidents[residentIndex],
            residentIndex);
        return gResidents[residentIndex].data.valid
            ? gResidents[residentIndex].srvTable
            : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

    LightProbeVolumeDebugState GetDebugState() {
        LightProbeVolumeDebugState state{};
        state.residentCount =
            static_cast<uint32_t>(gResidentCount);
        state.selectedCount =
            static_cast<uint32_t>(gViewSelection.count);
        const LightProbeVolumeRuntimeData* selected =
            GetViewVolume(0u);
        const size_t residentIndex =
            gViewSelection.count > 0u
            ? gViewSelection.providerIndices[0]
            : 0u;
        if (selected == nullptr ||
            residentIndex >= gResidents.size()) {
            return state;
        }
        const ResidentVolume& resident =
            gResidents[residentIndex];
        state.valid = selected->valid;
        state.srvReady = resident.srvReady;
        state.hasBuffer = HasGpuResources(resident);
        state.probeCount = selected->probeCount;
        state.countX = selected->countX;
        state.countY = selected->countY;
        state.countZ = selected->countZ;
        state.srvHeapPtr = resident.srvTable.ptr;
        state.bufferPtr =
            resident.shVolumes[0] != nullptr
            ? reinterpret_cast<uint64_t>(
                resident.shVolumes[0].Get())
            : 0u;
        state.sourcePath = selected->sourcePath;
        return state;
    }

    bool IsValid() {
        for (size_t index = 0;
             index < gResidents.size();
             ++index) {
            RefreshValidity(
                gResidents[index],
                index);
            if (gResidents[index].data.valid) {
                return true;
            }
        }
        return false;
    }

} // namespace HIKARI::RENDER3D::LIGHTPROBE
