#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"
#include "Render3D/Debug/HIKARI_RenderDebugView.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    struct TemporalResourceStats {
        bool initialized = false;
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
        bool debugOutputReady = false;
        RenderDebugView debugView = RenderDebugView::None;
        uint32_t rigidVelocityDrawCount = 0;
        uint32_t skinnedVelocityDrawCount = 0;
        uint32_t alphaMaskedVelocityDrawCount = 0;
        uint64_t resourceResizeCount = 0;
        uint64_t historyInvalidationCount = 0;
        TemporalHistoryResetReason lastResetReason =
            TemporalHistoryResetReason::FirstFrame;
    };

    void UpdateTemporalResourceSystemContext(const GFX::Context& ctx);
    bool BeginTemporalResources(const TemporalFrameState& frame);
    void ShutdownTemporalResourceSystem();

    RenderTarget2D* GetMotionVectorRenderTarget();
    RenderTarget2D* GetMotionMetadataRenderTarget();
    RenderTarget2D* GetHistoryColorWriteRenderTarget();
    RenderTarget2D* GetHistoryDepthWriteRenderTarget();
    RenderTarget2D* GetTaaResolvedColorRenderTarget();
    RenderTarget2D* GetReactiveMaskRenderTarget();
    RenderTarget2D* GetTransparencyMaskRenderTarget();
    RenderTarget2D* GetInvalidDepthMotionMaskRenderTarget();
    RenderTarget2D* GetTemporalDebugRenderTarget();
    bool PrepareSceneColorInput(RenderTarget2D& source);
    void SetTemporalCompositionBase(D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv);
    D3D12_GPU_DESCRIPTOR_HANDLE GetTemporalCompositionBase();
    void SetTemporalDebugView(RenderDebugView view);
    RenderDebugView GetTemporalDebugView();
    bool UpdateTemporalExposure(float exposure);
    void MarkTemporalMasksWritten(bool written, bool compositionDerived);
    void SetTemporalGeometryDrawCounts(
        uint32_t rigid,
        uint32_t skinned,
        uint32_t alphaMasked);
    void MarkMotionVectorsWritten(bool written);
    void MarkTemporalAntiAliasing(bool enabled, bool resolved);
    void CommitTemporalHistory(bool written);

    TemporalInputs BuildTemporalInputs(
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv);
    TemporalInputs BuildTemporalInputs(
        RenderTarget2D& sceneColorTarget,
        RenderTarget2D& sceneDepthTarget);
    TemporalInputs BuildTemporalInputs(RenderTarget2D& sceneTarget);
    TemporalInputs GetCurrentTemporalInputs();

    const TemporalResourceStats& GetTemporalResourceStats();

} // namespace HIKARI::RENDER3D::TEMPORAL
