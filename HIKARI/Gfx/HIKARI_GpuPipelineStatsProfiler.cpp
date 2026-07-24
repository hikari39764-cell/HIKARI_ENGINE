#include "Gfx/HIKARI_GpuPipelineStatsProfiler.h"

#include <d3dx12.h>
#include <wrl.h>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"

namespace HIKARI::GFX::GPU_PIPELINE_STATS {

    namespace {
        using Microsoft::WRL::ComPtr;

        constexpr uint32_t kBufferedFrameCount = 3u;
        constexpr uint32_t kPassCount = static_cast<uint32_t>(GPU_PROFILE::Pass::Count);
        constexpr uint32_t kQueryCount = kPassCount;

        struct FrameSlot {
            ComPtr<ID3D12QueryHeap> queryHeap{};
            ComPtr<ID3D12Resource> readbackBuffer{};
            std::array<uint8_t, kPassCount> used{};
            std::array<uint8_t, kPassCount> active{};
            bool resolved = false;
            uint64_t frameIndex = 0;
        };

        struct ProfilerState {
            std::array<FrameSlot, kBufferedFrameCount> slots{};
            FrameSlot* currentSlot = nullptr;
            FrameSnapshot latest{};
            uint32_t writeSlotIndex = 0;
            bool initialized = false;
            bool enabled = true;
            bool meshShaderPipelineStatsSupported = false;
            const char* unavailableReason = "";
        };

        ProfilerState gState{};

        uint32_t PassIndex(GPU_PROFILE::Pass pass) {
            return static_cast<uint32_t>(pass);
        }

        bool IsValidPass(GPU_PROFILE::Pass pass) {
            return PassIndex(pass) < kPassCount;
        }

        void ResetLatestNames(FrameSnapshot& snapshot) {
            for (uint32_t i = 0; i < kPassCount; ++i) {
                const GPU_PROFILE::Pass pass = static_cast<GPU_PROFILE::Pass>(i);
                snapshot.passes[i].name = GPU_PROFILE::ToString(pass);
            }
        }

        void ResetUnavailableSnapshot(const char* reason) {
            gState.latest = {};
            gState.latest.initialized = gState.initialized;
            gState.latest.profilerEnabled = gState.enabled;
            gState.latest.pipelineStatsAvailable = false;
            gState.latest.meshShaderPipelineStatsSupported =
                gState.meshShaderPipelineStatsSupported;
            gState.latest.unavailableReason = reason != nullptr ? reason : "";
            ResetLatestNames(gState.latest);
        }

        bool CheckPipelineStatsSupport(ID3D12Device* device) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS9 options9{};
            const HRESULT hr = device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS9,
                &options9,
                sizeof(options9));
            return SUCCEEDED(hr) && options9.MeshShaderPipelineStatsSupported != FALSE;
        }

        bool EnsureInitialized(ID3D12Device* device) {
            if (gState.initialized) {
                return true;
            }
            if (device == nullptr) {
                ResetUnavailableSnapshot("Pipeline stats profiler requires a valid device.");
                return false;
            }

            gState.meshShaderPipelineStatsSupported = CheckPipelineStatsSupport(device);
            if (!gState.meshShaderPipelineStatsSupported) {
                ResetUnavailableSnapshot("Mesh shader pipeline statistics are not supported by this device/runtime.");
                return false;
            }

            for (FrameSlot& slot : gState.slots) {
                D3D12_QUERY_HEAP_DESC heapDesc{};
                heapDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS1;
                heapDesc.Count = kQueryCount;
                if (!HIKARI_DX_CHECK(
                        device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(slot.queryHeap.GetAddressOf())),
                        "GpuPipelineStatsProfiler::CreateQueryHeap")) {
                    return false;
                }

                const auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
                const auto bufferDesc =
                    CD3DX12_RESOURCE_DESC::Buffer(sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1) * kQueryCount);
                if (!HIKARI_DX_CHECK(
                        device->CreateCommittedResource(
                            &heapProps,
                            D3D12_HEAP_FLAG_NONE,
                            &bufferDesc,
                            D3D12_RESOURCE_STATE_COPY_DEST,
                            nullptr,
                            IID_PPV_ARGS(slot.readbackBuffer.GetAddressOf())),
                        "GpuPipelineStatsProfiler::CreateReadbackBuffer")) {
                    return false;
                }
                slot.readbackBuffer->SetName(L"HIKARI.GpuPipelineStatsProfiler.Readback");
            }

            gState.initialized = true;
            gState.latest = {};
            gState.latest.initialized = true;
            gState.latest.profilerEnabled = true;
            gState.latest.pipelineStatsAvailable = true;
            gState.latest.meshShaderPipelineStatsSupported = true;
            gState.latest.unavailableReason = "";
            ResetLatestNames(gState.latest);
            return true;
        }

        void CollectResolvedSlot(FrameSlot& slot) {
            if (!slot.resolved || slot.readbackBuffer == nullptr) {
                return;
            }

            const D3D12_RANGE readRange{
                0,
                sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1) * kQueryCount
            };
            void* mapped = nullptr;
            if (FAILED(slot.readbackBuffer->Map(0, &readRange, &mapped)) || mapped == nullptr) {
                return;
            }
            const auto* queryData =
                static_cast<const D3D12_QUERY_DATA_PIPELINE_STATISTICS1*>(mapped);

            FrameSnapshot snapshot{};
            snapshot.initialized = true;
            snapshot.profilerEnabled = true;
            snapshot.pipelineStatsAvailable = true;
            snapshot.meshShaderPipelineStatsSupported =
                gState.meshShaderPipelineStatsSupported;
            snapshot.frameIndex = slot.frameIndex;
            snapshot.unavailableReason = "";
            ResetLatestNames(snapshot);

            for (uint32_t i = 0; i < kPassCount; ++i) {
                if (slot.used[i] == 0) {
                    continue;
                }

                PassPipelineStats& stats = snapshot.passes[i];
                stats.valid = true;
                stats.counters = queryData[i];
            }

            const D3D12_RANGE writeRange{ 0, 0 };
            slot.readbackBuffer->Unmap(0, &writeRange);
            slot.resolved = false;
            gState.latest = snapshot;
        }

        void ResetSlot(FrameSlot& slot, uint64_t frameIndex) {
            slot.used.fill(0u);
            slot.active.fill(0u);
            slot.frameIndex = frameIndex;
            slot.resolved = false;
        }
    }

    void SetEnabled(bool enabled, const char* unavailableReason) {
        gState.enabled = enabled;
        gState.unavailableReason =
            enabled ? "" : (unavailableReason != nullptr ? unavailableReason : "");
        if (!enabled) {
            gState.currentSlot = nullptr;
            ResetUnavailableSnapshot(gState.unavailableReason);
        }
    }

    bool IsEnabled() {
        return gState.enabled;
    }

    void BeginFrame(
        ID3D12Device* device,
        ID3D12GraphicsCommandList* cmd,
        uint64_t frameIndex) {
        if (!gState.enabled) {
            gState.currentSlot = nullptr;
            ResetUnavailableSnapshot(gState.unavailableReason);
            return;
        }
        if (cmd == nullptr || !EnsureInitialized(device)) {
            gState.currentSlot = nullptr;
            return;
        }

        FrameSlot& slot = gState.slots[gState.writeSlotIndex % kBufferedFrameCount];
        CollectResolvedSlot(slot);
        ResetSlot(slot, frameIndex);
        gState.currentSlot = &slot;
        ++gState.writeSlotIndex;
    }

    void EndFrame(ID3D12GraphicsCommandList* cmd) {
        FrameSlot* slot = gState.currentSlot;
        if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || slot->queryHeap == nullptr) {
            gState.currentSlot = nullptr;
            return;
        }

        for (uint32_t i = 0; i < kPassCount; ++i) {
            if (slot->active[i] != 0) {
                cmd->EndQuery(
                    slot->queryHeap.Get(),
                    D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,
                    i);
                slot->active[i] = 0;
            }
            if (slot->used[i] != 0) {
                cmd->ResolveQueryData(
                    slot->queryHeap.Get(),
                    D3D12_QUERY_TYPE_PIPELINE_STATISTICS1,
                    i,
                    1u,
                    slot->readbackBuffer.Get(),
                    sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS1) * i);
            }
        }

        slot->resolved = true;
        gState.currentSlot = nullptr;
    }

    void Shutdown() {
        for (FrameSlot& slot : gState.slots) {
            slot.queryHeap.Reset();
            slot.readbackBuffer.Reset();
            slot.used.fill(0u);
            slot.active.fill(0u);
            slot.resolved = false;
        }
        gState = {};
    }

    bool BeginPass(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass) {
        FrameSlot* slot = gState.currentSlot;
        if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || !IsValidPass(pass)) {
            return false;
        }

        const uint32_t index = PassIndex(pass);
        if (slot->active[index] != 0 || slot->used[index] != 0 || slot->queryHeap == nullptr) {
            return false;
        }

        cmd->BeginQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, index);
        slot->active[index] = 1u;
        slot->used[index] = 1u;
        return true;
    }

    void EndPass(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass) {
        FrameSlot* slot = gState.currentSlot;
        if (!gState.enabled || !gState.initialized || slot == nullptr || cmd == nullptr || !IsValidPass(pass)) {
            return;
        }

        const uint32_t index = PassIndex(pass);
        if (slot->active[index] == 0 || slot->queryHeap == nullptr) {
            return;
        }

        cmd->EndQuery(slot->queryHeap.Get(), D3D12_QUERY_TYPE_PIPELINE_STATISTICS1, index);
        slot->active[index] = 0u;
    }

    const FrameSnapshot& GetLatestSnapshot() {
        return gState.latest;
    }

    ScopedPipelineStats::ScopedPipelineStats(ID3D12GraphicsCommandList* cmd, GPU_PROFILE::Pass pass)
        : cmd_(cmd)
        , pass_(pass) {
        if (cmd_ != nullptr && IsValidPass(pass_)) {
            active_ = GPU_PIPELINE_STATS::BeginPass(cmd_, pass_);
        }
    }

    ScopedPipelineStats::~ScopedPipelineStats() {
        if (active_) {
            GPU_PIPELINE_STATS::EndPass(cmd_, pass_);
        }
    }

} // namespace HIKARI::GFX::GPU_PIPELINE_STATS
