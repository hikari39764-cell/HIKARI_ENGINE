#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

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
#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "HIKARI_Services.h"

namespace HIKARI::RENDER3D::LIGHTPROBE {

    namespace {

        // SH9 係数は係数ごとに 1 枚の Texture3D (texel=probe, RGB=係数) に焼き、
        // shader 側は hardware trilinear で係数を補間してから SH を評価する。
        constexpr uint32_t kShCoeffCount =
            GFX::DESCRIPTOR::kLightProbeShVolumeTextureCount;
        constexpr DXGI_FORMAT kShVolumeFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

        static_assert(
            kShCoeffCount == ASSETS::LIGHTING::kLightProbeShCoeffCount,
            "SH volume texture count must match the baked SH coefficient count");

        struct HalfTexel {
            uint16_t r = 0;
            uint16_t g = 0;
            uint16_t b = 0;
            uint16_t a = 0;
        };

        LightProbeVolumeRuntimeData gData{};
        Microsoft::WRL::ComPtr<ID3D12Resource> gShVolumes[kShCoeffCount]{};
        Microsoft::WRL::ComPtr<ID3D12Resource> gUploadBuffer{};
        // device 再作成時に GPU volume を張り直すための CPU 側 SH (probe-major)。
        std::vector<MATH::Vec3> gCpuShCoeffs{};
        uint32_t gVolumeCountX = 0;
        uint32_t gVolumeCountY = 0;
        uint32_t gVolumeCountZ = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE gShVolumeSrvTable{};
        ID3D12Device* gVolumeDevice = nullptr;
        ID3D12Device* gSrvDevice = nullptr;
        ID3D12DescriptorHeap* gSrvHeap = nullptr;
        bool gHasValidSrvDescriptor = false;
        bool gSrvDescriptorIsNull = false;
        int gSamplingSuppressDepth = 0;

        void SetMessage(std::string* outMessage, std::string message) {
            if (outMessage != nullptr) {
                *outMessage = std::move(message);
            }
        }

        D3D12_CPU_DESCRIPTOR_HANDLE VolumeSrvCpuAt(uint32_t coeffIndex) {
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || heap == nullptr) {
                return {};
            }

            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return GFX::DESCRIPTOR::CpuAt(
                heap,
                descriptorSize,
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::LightProbeShVolume0) +
                    coeffIndex);
        }

        D3D12_GPU_DESCRIPTOR_HANDLE VolumeSrvTableGpu() {
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || heap == nullptr) {
                return {};
            }

            const UINT descriptorSize =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return GFX::DESCRIPTOR::GpuAt(
                heap,
                descriptorSize,
                GFX::DESCRIPTOR::ToIndex(GFX::DESCRIPTOR::SystemSrv::LightProbeShVolume0));
        }

        void ClearSrvDescriptorState() {
            gShVolumeSrvTable = {};
            gSrvDevice = nullptr;
            gSrvHeap = nullptr;
            gHasValidSrvDescriptor = false;
            gSrvDescriptorIsNull = false;
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
                debugName != nullptr ? debugName : "LightProbeVolume resource");
        }

        void RetireGpuVolumes() {
            for (Microsoft::WRL::ComPtr<ID3D12Resource>& volume : gShVolumes) {
                RetireResource(volume, "LightProbeVolume SH volume texture");
            }
            RetireResource(gUploadBuffer, "LightProbeVolume upload buffer");
            gVolumeCountX = 0;
            gVolumeCountY = 0;
            gVolumeCountZ = 0;
            gVolumeDevice = nullptr;
        }

        bool HasGpuVolumes() {
            if (gVolumeCountX == 0u || gVolumeCountY == 0u || gVolumeCountZ == 0u ||
                gVolumeDevice != SERVICES::gCtx.device) {
                return false;
            }
            for (const Microsoft::WRL::ComPtr<ID3D12Resource>& volume : gShVolumes) {
                if (volume == nullptr) {
                    return false;
                }
            }
            return true;
        }

        bool IsSrvContextCurrent() {
            return gSrvDevice == SERVICES::gCtx.device &&
                gSrvHeap == SERVICES::gCtx.srvHeap &&
                gShVolumeSrvTable.ptr != 0 &&
                gHasValidSrvDescriptor;
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

        bool WriteNullSrvs() {
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || heap == nullptr) {
                ClearSrvDescriptorState();
                return false;
            }

            const D3D12_SHADER_RESOURCE_VIEW_DESC srv = MakeVolumeSrvDesc();
            for (uint32_t i = 0; i < kShCoeffCount; ++i) {
                const D3D12_CPU_DESCRIPTOR_HANDLE cpu = VolumeSrvCpuAt(i);
                if (cpu.ptr == 0) {
                    ClearSrvDescriptorState();
                    return false;
                }
                device->CreateShaderResourceView(nullptr, &srv, cpu);
            }

            gShVolumeSrvTable = VolumeSrvTableGpu();
            gSrvDevice = device;
            gSrvHeap = heap;
            gHasValidSrvDescriptor = gShVolumeSrvTable.ptr != 0;
            gSrvDescriptorIsNull = gHasValidSrvDescriptor;
            return gHasValidSrvDescriptor;
        }

        bool WriteVolumeSrvs() {
            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12DescriptorHeap* heap = SERVICES::gCtx.srvHeap;
            if (device == nullptr || heap == nullptr || !HasGpuVolumes()) {
                ClearSrvDescriptorState();
                return false;
            }

            const D3D12_SHADER_RESOURCE_VIEW_DESC srv = MakeVolumeSrvDesc();
            for (uint32_t i = 0; i < kShCoeffCount; ++i) {
                const D3D12_CPU_DESCRIPTOR_HANDLE cpu = VolumeSrvCpuAt(i);
                if (cpu.ptr == 0) {
                    ClearSrvDescriptorState();
                    return false;
                }
                device->CreateShaderResourceView(gShVolumes[i].Get(), &srv, cpu);
            }

            gShVolumeSrvTable = VolumeSrvTableGpu();
            gSrvDevice = device;
            gSrvHeap = heap;
            gHasValidSrvDescriptor = gShVolumeSrvTable.ptr != 0;
            gSrvDescriptorIsNull = false;
            return gHasValidSrvDescriptor;
        }

        bool UploadVolumes(
            const std::vector<MATH::Vec3>& coeffs,
            uint32_t countX,
            uint32_t countY,
            uint32_t countZ,
            std::string* outMessage);

        bool EnsureLightProbeSrvDescriptor() {
            if (SERVICES::gCtx.device == nullptr || SERVICES::gCtx.srvHeap == nullptr) {
                ClearSrvDescriptorState();
                return false;
            }

            if (!gData.enabled) {
                if (!IsSrvContextCurrent() || !gSrvDescriptorIsNull) {
                    WriteNullSrvs();
                }
                return false;
            }

            if (!HasGpuVolumes()) {
                if (!gCpuShCoeffs.empty() &&
                    gData.countX > 0u && gData.countY > 0u && gData.countZ > 0u) {
                    // device 再作成時は保持している SH から volume を張り直す。
                    if (UploadVolumes(
                        gCpuShCoeffs,
                        gData.countX,
                        gData.countY,
                        gData.countZ,
                        nullptr)) {
                        return true;
                    }
                }
                WriteNullSrvs();
                return false;
            }

            if (!IsSrvContextCurrent()) {
                return WriteVolumeSrvs();
            }

            return true;
        }

        std::vector<MATH::Vec3> BuildCpuCoeffs(
            const ASSETS::LIGHTING::LightProbeVolumeFileData& fileData) {

            std::vector<MATH::Vec3> coeffs{};
            coeffs.reserve(fileData.probes.size() * kShCoeffCount);
            for (const ASSETS::LIGHTING::LightProbeSh9& probe : fileData.probes) {
                for (const MATH::Vec3& coeff : probe.coeffs) {
                    coeffs.push_back(coeff);
                }
            }
            return coeffs;
        }

        HalfTexel ToHalfTexel(const MATH::Vec3& value) {
            using DirectX::PackedVector::XMConvertFloatToHalf;
            HalfTexel texel{};
            texel.r = XMConvertFloatToHalf(value.x);
            texel.g = XMConvertFloatToHalf(value.y);
            texel.b = XMConvertFloatToHalf(value.z);
            texel.a = 0;
            return texel;
        }

        bool UploadVolumes(
            const std::vector<MATH::Vec3>& coeffs,
            uint32_t countX,
            uint32_t countY,
            uint32_t countZ,
            std::string* outMessage) {

            ID3D12Device* device = SERVICES::gCtx.device;
            ID3D12CommandQueue* queue = SERVICES::gCtx.queue;
            const size_t probeCount =
                static_cast<size_t>(countX) * countY * countZ;
            if (device == nullptr || queue == nullptr ||
                probeCount == 0u ||
                coeffs.size() != probeCount * kShCoeffCount) {
                SetMessage(outMessage, "Light probe GPU upload context is missing.");
                return false;
            }

            const CD3DX12_RESOURCE_DESC volumeDesc = CD3DX12_RESOURCE_DESC::Tex3D(
                kShVolumeFormat,
                countX,
                countY,
                static_cast<UINT16>(countZ),
                1);

            const CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
            Microsoft::WRL::ComPtr<ID3D12Resource> volumes[kShCoeffCount]{};
            for (uint32_t i = 0; i < kShCoeffCount; ++i) {
                const HRESULT hr = device->CreateCommittedResource(
                    &defaultHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &volumeDesc,
                    D3D12_RESOURCE_STATE_COMMON,
                    nullptr,
                    IID_PPV_ARGS(volumes[i].GetAddressOf()));
                if (!HIKARI_DX_CHECK(hr, "LightProbeVolumeRuntime::CreateShVolume")) {
                    SetMessage(outMessage, "Failed to create light probe SH volume texture.");
                    return false;
                }
            }

            // 9 枚分の footprint を 1 本の upload buffer に直列配置する。
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprints[kShCoeffCount]{};
            UINT rowCounts[kShCoeffCount]{};
            UINT64 rowBytes[kShCoeffCount]{};
            UINT64 uploadBytes = 0;
            for (uint32_t i = 0; i < kShCoeffCount; ++i) {
                UINT64 total = 0;
                device->GetCopyableFootprints(
                    &volumeDesc,
                    0,
                    1,
                    uploadBytes,
                    &footprints[i],
                    &rowCounts[i],
                    &rowBytes[i],
                    &total);
                uploadBytes = footprints[i].Offset + total;
                uploadBytes =
                    (uploadBytes + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u) &
                    ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u);
            }

            const CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);
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
            if (!HIKARI_DX_CHECK(hr, "LightProbeVolumeRuntime::CreateUploadBuffer")) {
                SetMessage(outMessage, "Failed to create light probe upload buffer.");
                return false;
            }

            uint8_t* mapped = nullptr;
            hr = uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
            if (FAILED(hr) || mapped == nullptr) {
                SetMessage(outMessage, "Failed to map light probe upload buffer.");
                return false;
            }
            for (uint32_t coeff = 0; coeff < kShCoeffCount; ++coeff) {
                const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp = footprints[coeff];
                for (uint32_t z = 0; z < countZ; ++z) {
                    for (uint32_t y = 0; y < countY; ++y) {
                        HalfTexel* row = reinterpret_cast<HalfTexel*>(
                            mapped + fp.Offset +
                            (static_cast<UINT64>(z) * rowCounts[coeff] + y) *
                                fp.Footprint.RowPitch);
                        for (uint32_t x = 0; x < countX; ++x) {
                            const size_t probeIndex =
                                (static_cast<size_t>(z) * countY + y) * countX + x;
                            row[x] = ToHalfTexel(
                                coeffs[probeIndex * kShCoeffCount + coeff]);
                        }
                    }
                }
            }
            uploadBuffer->Unmap(0, nullptr);

            Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator{};
            Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd{};
            Microsoft::WRL::ComPtr<ID3D12Fence> fence{};
            hr = device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(allocator.GetAddressOf()));
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to create light probe upload allocator.");
                return false;
            }
            hr = device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(cmd.GetAddressOf()));
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to create light probe upload command list.");
                return false;
            }

            D3D12_RESOURCE_BARRIER toCopyDest[kShCoeffCount]{};
            for (uint32_t i = 0; i < kShCoeffCount; ++i) {
                toCopyDest[i] = CD3DX12_RESOURCE_BARRIER::Transition(
                    volumes[i].Get(),
                    D3D12_RESOURCE_STATE_COMMON,
                    D3D12_RESOURCE_STATE_COPY_DEST);
            }
            cmd->ResourceBarrier(kShCoeffCount, toCopyDest);

            for (uint32_t i = 0; i < kShCoeffCount; ++i) {
                const CD3DX12_TEXTURE_COPY_LOCATION dst(volumes[i].Get(), 0);
                const CD3DX12_TEXTURE_COPY_LOCATION src(uploadBuffer.Get(), footprints[i]);
                cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }

            D3D12_RESOURCE_BARRIER toShaderResource[kShCoeffCount]{};
            for (uint32_t i = 0; i < kShCoeffCount; ++i) {
                toShaderResource[i] = CD3DX12_RESOURCE_BARRIER::Transition(
                    volumes[i].Get(),
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            }
            cmd->ResourceBarrier(kShCoeffCount, toShaderResource);

            hr = cmd->Close();
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to close light probe upload command list.");
                return false;
            }

            ID3D12CommandList* lists[] = { cmd.Get() };
            queue->ExecuteCommandLists(1, lists);

            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.GetAddressOf()));
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to create light probe upload fence.");
                return false;
            }

            constexpr uint64_t kFenceValue = 1;
            hr = queue->Signal(fence.Get(), kFenceValue);
            if (FAILED(hr)) {
                SetMessage(outMessage, "Failed to signal light probe upload fence.");
                return false;
            }
            if (fence->GetCompletedValue() < kFenceValue) {
                HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
                if (eventHandle == nullptr) {
                    SetMessage(outMessage, "Failed to create light probe upload fence event.");
                    return false;
                }
                hr = fence->SetEventOnCompletion(kFenceValue, eventHandle);
                if (SUCCEEDED(hr)) {
                    WaitForSingleObject(eventHandle, INFINITE);
                }
                CloseHandle(eventHandle);
                if (FAILED(hr)) {
                    SetMessage(outMessage, "Failed to wait light probe upload fence.");
                    return false;
                }
            }

            RetireGpuVolumes();
            for (uint32_t i = 0; i < kShCoeffCount; ++i) {
                gShVolumes[i] = std::move(volumes[i]);
            }
            gUploadBuffer = std::move(uploadBuffer);
            gVolumeCountX = countX;
            gVolumeCountY = countY;
            gVolumeCountZ = countZ;
            gVolumeDevice = device;
            if (!WriteVolumeSrvs()) {
                SetMessage(outMessage, "Light probe SRV descriptors are missing.");
                return false;
            }
            gCpuShCoeffs = coeffs;
            return true;
        }

        void RefreshValidity() {
            const bool srvReady = EnsureLightProbeSrvDescriptor();
            gData.valid = gData.enabled &&
                gSamplingSuppressDepth <= 0 &&
                gData.probeCount > 0u &&
                gData.countX >= 1u &&
                gData.countY >= 1u &&
                gData.countZ >= 1u &&
                HasGpuVolumes() &&
                srvReady &&
                gData.intensity > 0.0f;
        }

        void LogState(const char* reason) {
            std::ostringstream oss{};
            oss << "[Environment][LightProbe]"
                << " reason=" << (reason ? reason : "state")
                << " enabled=" << (gData.enabled ? "true" : "false")
                << " valid=" << (gData.valid ? "true" : "false")
                << " probes=" << gData.probeCount
                << " grid=" << gData.countX << "x" << gData.countY << "x" << gData.countZ
                << " intensity=" << gData.intensity
                << " path=" << gData.sourcePath;
            HIKARI_LOG_INFO(oss.str());
        }

    } // namespace

    void Reset() {
        gData = {};
        RetireGpuVolumes();
        gCpuShCoeffs.clear();
        WriteNullSrvs();
        RefreshValidity();
    }

    bool LoadLightProbeVolume(
        const std::filesystem::path& path,
        std::string* outMessage) {

        ASSETS::LIGHTING::LightProbeVolumeFileData fileData{};
        std::string loadMessage{};
        if (!ASSETS::LIGHTING::LoadLightProbeVolumeFile(path, fileData, &loadMessage)) {
            Reset();
            SetMessage(outMessage, loadMessage);
            return false;
        }

        const std::vector<MATH::Vec3> coeffs = BuildCpuCoeffs(fileData);
        if (!UploadVolumes(
            coeffs,
            fileData.countX,
            fileData.countY,
            fileData.countZ,
            outMessage)) {
            Reset();
            return false;
        }

        gData.enabled = true;
        gData.origin = fileData.origin;
        gData.size = fileData.size;
        gData.spacing = fileData.spacing;
        gData.countX = fileData.countX;
        gData.countY = fileData.countY;
        gData.countZ = fileData.countZ;
        gData.probeCount = static_cast<uint32_t>(fileData.probes.size());
        gData.sourcePath = path.lexically_normal().generic_string();
        RefreshValidity();
        LogState("Load");
        SetMessage(outMessage, loadMessage);
        return gData.valid;
    }

    void SetLightProbeVolumeEnabled(bool enabled) {
        gData.enabled = enabled;
        if (!enabled) {
            gData.valid = false;
            WriteNullSrvs();
            return;
        }
        RefreshValidity();
    }

    void SetLightProbeVolumeIntensity(float intensity) {
        gData.intensity = (std::max)(0.0f, intensity);
        RefreshValidity();
    }

    const LightProbeVolumeRuntimeData& GetRuntimeData() {
        RefreshValidity();
        return gData;
    }

    bool IsLightProbeVolumeSamplingSuppressed() {
        return gSamplingSuppressDepth > 0;
    }

    ScopedLightProbeVolumeSamplingSuppress::ScopedLightProbeVolumeSamplingSuppress() {
        ++gSamplingSuppressDepth;
    }

    ScopedLightProbeVolumeSamplingSuppress::~ScopedLightProbeVolumeSamplingSuppress() {
        gSamplingSuppressDepth = (std::max)(0, gSamplingSuppressDepth - 1);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetShVolumeSrvTable() {
        EnsureLightProbeSrvDescriptor();
        return gShVolumeSrvTable;
    }

    bool HasGpuBuffer() {
        return HasGpuVolumes();
    }

    bool IsSrvReady() {
        return EnsureLightProbeSrvDescriptor();
    }

    LightProbeVolumeDebugState GetDebugState() {
        const bool srvReady = EnsureLightProbeSrvDescriptor();
        RefreshValidity();

        LightProbeVolumeDebugState state{};
        state.valid = gData.valid;
        state.srvReady = srvReady;
        state.hasBuffer = HasGpuVolumes();
        state.probeCount = gData.probeCount;
        state.countX = gData.countX;
        state.countY = gData.countY;
        state.countZ = gData.countZ;
        state.srvHeapPtr = reinterpret_cast<uint64_t>(gSrvHeap);
        state.bufferPtr = reinterpret_cast<uint64_t>(gShVolumes[0].Get());
        state.sourcePath = gData.sourcePath;
        return state;
    }

    bool IsValid() {
        RefreshValidity();
        return gData.valid;
    }

} // namespace HIKARI::RENDER3D::LIGHTPROBE
