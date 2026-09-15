#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    inline constexpr uint32_t kTemporalMaximumResidentViewCount = 2u;
    inline constexpr DXGI_FORMAT kTemporalMotionVectorFormat = DXGI_FORMAT_R16G16_FLOAT;
    // 前フレームの device depth は D32 と同精度を保つ。半精度では遠景が同じ値へ丸まる。
    inline constexpr DXGI_FORMAT kTemporalMotionMetadataFormat = DXGI_FORMAT_R32G32_FLOAT;

    enum class TemporalResourceReleaseMode : uint8_t {
        DeferredUntilFence,
        ImmediateAfterGpuIdle,
        ImmediateAfterDeviceLost,
    };

    struct TemporalResourceStats {
        TemporalViewLease view{};
        TemporalResourceFrame resourceFrame{};
        bool initialized = false;
        bool active = false;
        bool contextCompatible = true;
        uint32_t width = 0;
        uint32_t height = 0;
        DXGI_FORMAT motionVectorFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT motionMetadataFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT historyColorFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT historyDepthFormat = DXGI_FORMAT_UNKNOWN;
        bool sceneColorReady = false;
        bool motionVectorReady = false;
        bool motionMetadataReady = false;
        bool motionVectorWritten = false;
        bool historyColorReady = false;
        bool historyColorValid = false;
        bool historyDepthReady = false;
        bool historyDepthValid = false;
        bool taaResolvedColorReady = false;
        bool taaEnabled = false;
        bool taaResolved = false;
        bool exposureReady = false;
        bool reactiveMaskReady = false;
        bool transparencyMaskReady = false;
        bool invalidDepthMotionMaskReady = false;
        bool masksWritten = false;
        bool masksCompositionDerived = false;
        bool exposureWritten = false;
        COLOR::ExposureMode exposureMode =
            COLOR::ExposureMode::Manual;
        float exposureMultiplier = 1.0f;
        float preExposure = 1.0f;
        bool debugOutputReady = false;
        RenderDebugView debugView = RenderDebugView::None;
        uint32_t rigidVelocityDrawCount = 0;
        uint32_t skinnedVelocityDrawCount = 0;
        uint32_t alphaMaskedVelocityDrawCount = 0;
        uint64_t resourceResizeCount = 0;
        uint64_t historyInvalidationCount = 0;
        uint64_t resourceGeneration = 0;
        uint64_t rejectedContextCount = 0;
        TemporalHistoryResetReason lastResetReason =
            TemporalHistoryResetReason::FirstFrame;
    };

    bool BeginTemporalResources(
        const GFX::Context& ctx,
        const TemporalFrameState& frame,
        TemporalResourceFrame& outFrame);
    bool EndTemporalResources(TemporalResourceFrame frame);
    bool AbortTemporalResources(
        TemporalResourceFrame frame,
        TemporalHistoryResetReason reason =
            TemporalHistoryResetReason::ExplicitReset);
    bool ReleaseTemporalResources(
        TemporalViewLease view,
        TemporalResourceReleaseMode mode);
    bool ReleaseTemporalView(
        TemporalViewLease view,
        TemporalResourceReleaseMode mode);
    void ShutdownTemporalResourceSystem(
        TemporalResourceReleaseMode mode =
            TemporalResourceReleaseMode::ImmediateAfterGpuIdle);

    RenderTarget2D* GetMotionVectorRenderTarget(TemporalResourceFrame frame);
    RenderTarget2D* GetMotionMetadataRenderTarget(TemporalResourceFrame frame);
    RenderTarget2D* GetHistoryColorWriteRenderTarget(TemporalResourceFrame frame);
    RenderTarget2D* GetHistoryDepthWriteRenderTarget(TemporalResourceFrame frame);
    RenderTarget2D* GetTaaResolvedColorRenderTarget(TemporalResourceFrame frame);
    RenderTarget2D* GetReactiveMaskRenderTarget(TemporalResourceFrame frame);
    RenderTarget2D* GetTransparencyMaskRenderTarget(TemporalResourceFrame frame);
    RenderTarget2D* GetInvalidDepthMotionMaskRenderTarget(
        TemporalResourceFrame frame);
    RenderTarget2D* GetTemporalDebugRenderTarget(TemporalResourceFrame frame);
    bool PrepareSceneColorInput(
        TemporalResourceFrame frame,
        RenderTarget2D& source);
    bool SetTemporalCompositionBase(
        TemporalResourceFrame frame,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv);
    D3D12_GPU_DESCRIPTOR_HANDLE GetTemporalCompositionBase(
        TemporalResourceFrame frame);
    bool SetTemporalDebugView(
        TemporalResourceFrame frame,
        RenderDebugView view);
    RenderDebugView GetTemporalDebugView(TemporalResourceFrame frame);
    bool UpdateTemporalExposure(
        TemporalResourceFrame frame,
        const COLOR::ExposureFrameState& exposureState);
    bool MarkTemporalMasksWritten(
        TemporalResourceFrame frame,
        bool written,
        bool compositionDerived);
    bool SetTemporalGeometryDrawCounts(
        TemporalResourceFrame frame,
        uint32_t rigid,
        uint32_t skinned,
        uint32_t alphaMasked);
    bool MarkMotionVectorsWritten(
        TemporalResourceFrame frame,
        bool written);
    bool MarkTemporalAntiAliasing(
        TemporalResourceFrame frame,
        bool enabled,
        bool resolved);
    bool MarkTemporalHistoryOutputWritten(
        TemporalResourceFrame frame,
        bool colorWritten,
        bool depthWritten);
    bool CommitTemporalHistory(
        TemporalResourceFrame frame,
        bool written);

    bool TryBuildTemporalPassInputs(
        TemporalResourceFrame frame,
        const RenderTextureView& sceneDepth,
        const RenderTextureView& sceneColor,
        TemporalInputs& outInputs);
    bool TryBuildTemporalResolveInputs(
        TemporalResourceFrame frame,
        RenderTarget2D& sceneColorTarget,
        RenderTarget2D& sceneDepthTarget,
        TemporalInputs& outInputs);
    bool TryGetTemporalResolveInputs(
        TemporalResourceFrame frame,
        TemporalInputs& outInputs);

    TemporalResourceStats GetLatestTemporalResourceStats(RenderViewId viewId);
    uint32_t GetTemporalResidentViewCount();

} // namespace HIKARI::RENDER3D::TEMPORAL
