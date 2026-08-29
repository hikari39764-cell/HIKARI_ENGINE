#pragma once

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "Render3D/Resources/HIKARI_RenderResourcePool.h"
#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

namespace HIKARI::RENDER3D::TEMPORAL::DETAIL {

    struct TemporalTarget {
        RenderTarget2D target{};
        RenderResourceView srv{};
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        bool ready = false;
    };

    struct TemporalExternalTexture {
        RenderResourceView srv{};
        ID3D12Resource* resource = nullptr;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        bool ready = false;
    };

    struct TemporalResourceState {
        TemporalViewLease view{};
        TemporalResourceFrame activeFrame{};
        TemporalResourceFrame terminalFrame{};
        TemporalFrameState frame{};
        GFX::Context recordingContext{};
        GFX::Context retirementContext{};
        uint64_t ownerDeviceGeneration = 0;
        ID3D12Device* ownerDevice = nullptr;
        ID3D12CommandQueue* ownerQueue = nullptr;
        ID3D12DescriptorHeap* ownerHeap = nullptr;
        GFX::GpuDeferredReleaseQueue* ownerReleaseQueue = nullptr;
        GFX::ResourceStateTracker* ownerResourceStates = nullptr;
        ID3D12Fence* ownerFrameFence = nullptr;
        bool active = false;
        bool contextCompatible = true;
        bool terminalAborted = false;

        TemporalExternalTexture sceneColor{};
        RenderTextureView sceneDepth{};
        RenderTarget2D* sceneDepthOwner = nullptr;
        TemporalTarget motionVectors{};
        TemporalTarget motionMetadata{};
        TemporalTarget historyColor[2]{};
        TemporalTarget historyDepth[2]{};
        TemporalTarget taaResolvedColor{};
        TemporalTarget exposure{};
        TemporalTarget reactiveMask{};
        TemporalTarget transparencyMask{};
        TemporalTarget invalidDepthMotionMask{};
        TemporalTarget debugOutput{};
        COLOR::ExposureFrameState exposureState{};
        D3D12_GPU_DESCRIPTOR_HANDLE compositionBaseSrv{};
        RenderDebugView debugView = RenderDebugView::None;
        uint32_t historyReadIndex = 0;
        uint32_t historyWriteIndex = 1;
        bool historyColorValid = false;
        bool historyDepthValid = false;
        TemporalResourceFrame historyWritten{};
        TemporalResourceFrame historyOutputWritten{};

        TemporalResourceFrame sceneColorPrepared{};
        TemporalResourceFrame compositionBaseWritten{};
        TemporalResourceFrame motionWritten{};
        TemporalResourceFrame masksWritten{};
        TemporalResourceFrame exposureWritten{};
        TemporalResourceStats stats{};
    };

    struct TemporalResourceSystemState {
        std::unordered_map<
            uint64_t,
            std::unique_ptr<TemporalResourceState>> views{};
        uint64_t nextResourceGeneration = 1u;
        std::thread::id ownerThread{};
        bool ownerThreadAssigned = false;
        std::mutex ownerMutex{};
    };

    TemporalResourceSystemState& System();
    bool CheckOwnerThread(bool assignIfNeeded = false);
    bool HasValidContext(const GFX::Context& ctx);
    bool ContextMatchesOwner(
        const TemporalResourceState& state,
        const GFX::Context& ctx);
    uint64_t AllocateResourceGeneration();
    bool UsesDeferredRelease(TemporalResourceReleaseMode mode);
    void ReleaseAllResources(
        TemporalResourceState& state,
        TemporalResourceReleaseMode mode);
    void ReleaseExternalTexture(
        TemporalResourceState& state,
        TemporalExternalTexture& texture,
        TemporalResourceReleaseMode mode);
    RenderTextureView MakeTextureView(const TemporalTarget& target);
    RenderTextureView MakeTextureView(const TemporalExternalTexture& texture);
    bool HasCompleteBundle(const TemporalResourceState& state);
    bool StampMatches(
        TemporalResourceFrame stamp,
        TemporalResourceFrame expected);
    bool HistoryMatchesPrevious(const TemporalResourceState& state);
    void RefreshStats(TemporalResourceState& state);
    TemporalResourceState* FindActiveState(TemporalResourceFrame frame);
    TemporalResourceState* FindWritableState(TemporalResourceFrame frame);
    TemporalResourceState* FindViewState(TemporalViewLease view);
    TemporalResourceState* FindAnyViewState(RenderViewId viewId);
    void ClearFramePublication(
        TemporalResourceState& state,
        bool aborted);
    TemporalInputs BuildResidentInputs(
        const TemporalResourceState& state,
        const RenderTextureView& sceneDepth,
        const RenderTextureView& sceneColor);
    void ApplyResolveReadValidity(
        const TemporalResourceState& state,
        TemporalInputs& inputs);

} // namespace HIKARI::RENDER3D::TEMPORAL::DETAIL
