#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

#include <algorithm>
#include <array>
#include <string>

#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    namespace {
        constexpr DXGI_FORMAT kMotionVectorFormat = DXGI_FORMAT_R16G16_FLOAT;
        constexpr DXGI_FORMAT kMotionMetadataFormat = DXGI_FORMAT_R16G16_FLOAT;
        constexpr DXGI_FORMAT kHistoryColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT kHistoryDepthFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr DXGI_FORMAT kMaskFormat = DXGI_FORMAT_R8_UNORM;
        constexpr DXGI_FORMAT kExposureFormat = DXGI_FORMAT_R32_FLOAT;

        struct TemporalTarget {
            RenderTarget2D target{};
            RENDER3D::RenderResourceView srv{};
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            uint32_t width = 0;
            uint32_t height = 0;
            bool ready = false;
        };

        struct TemporalExternalTexture {
            RENDER3D::RenderResourceView srv{};
            ID3D12Resource* resource = nullptr;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
            uint32_t width = 0;
            uint32_t height = 0;
            D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
            bool ready = false;
        };

        struct TemporalResourceState {
            GFX::Context context{};
            TemporalFrameState frame{};
            TemporalExternalTexture sceneColor{};
            TemporalTextureView sceneDepth{};
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
            D3D12_GPU_DESCRIPTOR_HANDLE compositionBaseSrv{};
            RenderDebugView debugView = RenderDebugView::None;
            uint32_t historyReadIndex = 0;
            uint32_t historyWriteIndex = 1;
            bool historyColorValid = false;
            bool historyDepthValid = false;
            TemporalResourceStats stats{};
        };

        TemporalResourceState& State() {
            static TemporalResourceState state{};
            return state;
        }

        bool HasValidContext(const GFX::Context& ctx) {
            return ctx.device != nullptr && ctx.cmdList != nullptr;
        }

        void RetireDescriptor(
            TemporalResourceState& state,
            RenderResourceView& view,
            const char* debugName) {

            if (!view.IsValid()) return;
            const RenderResourceView retired = view;
            view = {};
            if (state.context.deferredReleaseQueue != nullptr &&
                state.context.currentFrameRetireFenceValue != 0) {
                state.context.deferredReleaseQueue->Enqueue(
                    state.context.currentFrameRetireFenceValue,
                    [retired]() {
                        (void)RENDER3D::ReleaseRenderResourceDescriptor(retired);
                    },
                    debugName != nullptr ? debugName : "Temporal.Descriptor");
                return;
            }
            (void)RENDER3D::ReleaseRenderResourceDescriptor(retired);
        }

        void ReleaseTarget(TemporalTarget& target) {
            RetireDescriptor(State(), target.srv, "Temporal.TargetSRV");
            target.target.Finalize();
            target.format = DXGI_FORMAT_UNKNOWN;
            target.width = 0;
            target.height = 0;
            target.ready = false;
        }

        TemporalTextureView MakeTextureView(const TemporalTarget& target) {
            TemporalTextureView view{};
            view.resource = target.target.GetResource();
            view.srv = target.srv.gpu;
            view.format = target.format;
            view.width = target.width;
            view.height = target.height;
            view.state = target.target.GetColorState();
            view.valid =
                target.ready &&
                view.resource != nullptr &&
                view.srv.ptr != 0;
            return view;
        }

        TemporalTextureView MakeTextureView(const TemporalExternalTexture& texture) {
            TemporalTextureView view{};
            view.resource = texture.resource;
            view.srv = texture.srv.gpu;
            view.format = texture.format;
            view.width = texture.width;
            view.height = texture.height;
            view.state = texture.state;
            view.valid =
                texture.ready &&
                view.resource != nullptr &&
                view.srv.ptr != 0;
            return view;
        }

        void ReleaseExternalTexture(TemporalExternalTexture& texture) {
            RetireDescriptor(State(), texture.srv, "Temporal.ExternalSRV");
            texture.resource = nullptr;
            texture.format = DXGI_FORMAT_UNKNOWN;
            texture.width = 0;
            texture.height = 0;
            texture.state = D3D12_RESOURCE_STATE_COMMON;
            texture.ready = false;
        }

        bool EnsureTarget(
            TemporalTarget& target,
            const GFX::Context& ctx,
            uint32_t width,
            uint32_t height,
            DXGI_FORMAT format,
            const char* debugName,
            const std::array<float, 4>& clearColor) {

            const uint32_t safeWidth = (std::max)(1u, width);
            const uint32_t safeHeight = (std::max)(1u, height);
            target.target.UpdateContext(ctx);

            const bool recreate =
                !target.ready ||
                target.width != safeWidth ||
                target.height != safeHeight ||
                target.format != format ||
                target.target.GetResource() == nullptr;
            if (!recreate) {
                return true;
            }

            ReleaseTarget(target);
            target.target.UpdateContext(ctx);
            target.target.SetDebugName(debugName != nullptr ? debugName : "Temporal.Target");
            if (!target.target.Init(
                    static_cast<int>(safeWidth),
                    static_cast<int>(safeHeight),
                    format,
                    false,
                    clearColor,
                    false)) {
                ReleaseTarget(target);
                return false;
            }

            target.srv = RENDER3D::AllocateTexture2DSrvDescriptor(
                target.target.GetResource(),
                format,
                0,
                1);
            if (!target.srv.IsValid()) {
                ReleaseTarget(target);
                return false;
            }

            target.format = format;
            target.width = safeWidth;
            target.height = safeHeight;
            target.ready = true;
            return true;
        }

        bool EnsureResources(TemporalResourceState& state) {
            if (!HasValidContext(state.context)) {
                return false;
            }

            const uint32_t width = (std::max)(1u, state.frame.renderWidth);
            const uint32_t height = (std::max)(1u, state.frame.renderHeight);
            const bool sizeChanged =
                state.stats.width != 0 &&
                (state.stats.width != width || state.stats.height != height);

            bool ok = true;
            ok = EnsureTarget(
                state.motionVectors,
                state.context,
                width,
                height,
                kMotionVectorFormat,
                "Temporal.MotionVectors",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state.motionMetadata,
                state.context,
                width,
                height,
                kMotionMetadataFormat,
                "Temporal.MotionMetadata",
                { 1.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state.historyColor[0],
                state.context,
                width,
                height,
                kHistoryColorFormat,
                "Temporal.HistoryColor0",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state.historyColor[1],
                state.context,
                width,
                height,
                kHistoryColorFormat,
                "Temporal.HistoryColor1",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state.historyDepth[0],
                state.context,
                width,
                height,
                kHistoryDepthFormat,
                "Temporal.HistoryDepth0",
                { 1.0f, 1.0f, 1.0f, 1.0f }) && ok;
            ok = EnsureTarget(
                state.historyDepth[1],
                state.context,
                width,
                height,
                kHistoryDepthFormat,
                "Temporal.HistoryDepth1",
                { 1.0f, 1.0f, 1.0f, 1.0f }) && ok;
            ok = EnsureTarget(
                state.taaResolvedColor,
                state.context,
                width,
                height,
                kHistoryColorFormat,
                "Temporal.TaaResolvedColor",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state.exposure,
                state.context,
                1,
                1,
                kExposureFormat,
                "Temporal.Exposure",
                { 1.0f, 1.0f, 1.0f, 1.0f }) && ok;
            ok = EnsureTarget(
                state.reactiveMask,
                state.context,
                width,
                height,
                kMaskFormat,
                "Temporal.ReactiveMask",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state.transparencyMask,
                state.context,
                width,
                height,
                kMaskFormat,
                "Temporal.TransparencyMask",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state.invalidDepthMotionMask,
                state.context,
                width,
                height,
                kMaskFormat,
                "Temporal.InvalidDepthMotionMask",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state.debugOutput,
                state.context,
                width,
                height,
                kHistoryColorFormat,
                "Temporal.DebugOutput",
                { 0.0f, 0.0f, 0.0f, 1.0f }) && ok;

            if (sizeChanged) {
                ++state.stats.resourceResizeCount;
            }
            return ok;
        }

        void RefreshStats(TemporalResourceState& state) {
            state.stats.initialized = HasValidContext(state.context);
            state.stats.width = state.frame.renderWidth;
            state.stats.height = state.frame.renderHeight;
            state.stats.motionVectorFormat = kMotionVectorFormat;
            state.stats.motionMetadataFormat = kMotionMetadataFormat;
            state.stats.historyColorFormat = kHistoryColorFormat;
            state.stats.historyDepthFormat = kHistoryDepthFormat;
            state.stats.sceneColorReady = state.sceneColor.ready;
            state.stats.motionVectorReady = state.motionVectors.ready;
            state.stats.motionMetadataReady = state.motionMetadata.ready;
            state.stats.historyColorReady =
                state.historyColor[0].ready && state.historyColor[1].ready;
            state.stats.historyColorValid = state.historyColorValid;
            state.stats.historyDepthReady =
                state.historyDepth[0].ready && state.historyDepth[1].ready;
            state.stats.historyDepthValid = state.historyDepthValid;
            state.stats.taaResolvedColorReady = state.taaResolvedColor.ready;
            state.stats.exposureReady = state.exposure.ready;
            state.stats.reactiveMaskReady = state.reactiveMask.ready;
            state.stats.transparencyMaskReady = state.transparencyMask.ready;
            state.stats.invalidDepthMotionMaskReady =
                state.invalidDepthMotionMask.ready;
            state.stats.debugOutputReady = state.debugOutput.ready;
            state.stats.debugView = state.debugView;
        }
    }

    void UpdateTemporalResourceSystemContext(const GFX::Context& ctx) {
        State().context = ctx;
    }

    bool BeginTemporalResources(const TemporalFrameState& frame) {
        TemporalResourceState& state = State();
        state.frame = frame;
        state.stats.motionVectorWritten = false;
        state.stats.taaEnabled = false;
        state.stats.taaResolved = false;
        state.stats.masksWritten = false;
        state.stats.masksCompositionDerived = false;
        state.stats.exposureWritten = false;
        state.stats.rigidVelocityDrawCount = 0;
        state.stats.skinnedVelocityDrawCount = 0;
        state.stats.alphaMaskedVelocityDrawCount = 0;
        state.compositionBaseSrv = {};
        state.sceneDepth = {};
        state.sceneDepthOwner = nullptr;
        if (frame.resetHistory) {
            if (state.historyColorValid ||
                state.historyDepthValid ||
                state.stats.lastResetReason != frame.resetReason) {
                ++state.stats.historyInvalidationCount;
            }
            state.historyColorValid = false;
            state.historyDepthValid = false;
            state.stats.lastResetReason = frame.resetReason;
        }

        state.historyWriteIndex = 1u - state.historyReadIndex;
        const bool ok = EnsureResources(state);
        RefreshStats(state);
        return ok;
    }

    void ShutdownTemporalResourceSystem() {
        TemporalResourceState& state = State();
        ReleaseExternalTexture(state.sceneColor);
        ReleaseTarget(state.motionVectors);
        ReleaseTarget(state.motionMetadata);
        ReleaseTarget(state.historyColor[0]);
        ReleaseTarget(state.historyColor[1]);
        ReleaseTarget(state.historyDepth[0]);
        ReleaseTarget(state.historyDepth[1]);
        ReleaseTarget(state.taaResolvedColor);
        ReleaseTarget(state.exposure);
        ReleaseTarget(state.reactiveMask);
        ReleaseTarget(state.transparencyMask);
        ReleaseTarget(state.invalidDepthMotionMask);
        ReleaseTarget(state.debugOutput);
        state.context = {};
        state.frame = {};
        state.historyReadIndex = 0;
        state.historyWriteIndex = 1;
        state.historyColorValid = false;
        state.historyDepthValid = false;
        state.compositionBaseSrv = {};
        state.debugView = RenderDebugView::None;
        state.stats = {};
    }

    RenderTarget2D* GetMotionVectorRenderTarget() {
        TemporalResourceState& state = State();
        return state.motionVectors.ready ? &state.motionVectors.target : nullptr;
    }

    RenderTarget2D* GetMotionMetadataRenderTarget() {
        TemporalResourceState& state = State();
        return state.motionMetadata.ready ? &state.motionMetadata.target : nullptr;
    }

    RenderTarget2D* GetHistoryColorWriteRenderTarget() {
        TemporalResourceState& state = State();
        TemporalTarget& target = state.historyColor[state.historyWriteIndex];
        return target.ready ? &target.target : nullptr;
    }

    RenderTarget2D* GetHistoryDepthWriteRenderTarget() {
        TemporalResourceState& state = State();
        TemporalTarget& target = state.historyDepth[state.historyWriteIndex];
        return target.ready ? &target.target : nullptr;
    }

    RenderTarget2D* GetTaaResolvedColorRenderTarget() {
        TemporalResourceState& state = State();
        return state.taaResolvedColor.ready ? &state.taaResolvedColor.target : nullptr;
    }

    RenderTarget2D* GetReactiveMaskRenderTarget() {
        TemporalResourceState& state = State();
        return state.reactiveMask.ready ? &state.reactiveMask.target : nullptr;
    }

    RenderTarget2D* GetTransparencyMaskRenderTarget() {
        TemporalResourceState& state = State();
        return state.transparencyMask.ready ? &state.transparencyMask.target : nullptr;
    }

    RenderTarget2D* GetInvalidDepthMotionMaskRenderTarget() {
        TemporalResourceState& state = State();
        return state.invalidDepthMotionMask.ready
            ? &state.invalidDepthMotionMask.target
            : nullptr;
    }

    RenderTarget2D* GetTemporalDebugRenderTarget() {
        TemporalResourceState& state = State();
        return state.debugOutput.ready ? &state.debugOutput.target : nullptr;
    }

    void SetTemporalCompositionBase(D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv) {
        State().compositionBaseSrv = sceneColorSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetTemporalCompositionBase() {
        return State().compositionBaseSrv;
    }

    void SetTemporalDebugView(RenderDebugView view) {
        TemporalResourceState& state = State();
        state.debugView = view;
        RefreshStats(state);
    }

    RenderDebugView GetTemporalDebugView() {
        return State().debugView;
    }

    bool UpdateTemporalExposure(float exposure) {
        TemporalResourceState& state = State();
        if (!state.exposure.ready || state.context.cmdList == nullptr) {
            state.stats.exposureWritten = false;
            return false;
        }
        const float safeExposure = (std::max)(exposure, 1e-4f);
        state.exposure.target.TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float clearValue[4] = {
            safeExposure,
            safeExposure,
            safeExposure,
            safeExposure
        };
        state.context.cmdList->ClearRenderTargetView(
            state.exposure.target.GetRtvHandle(),
            clearValue,
            0,
            nullptr);
        state.exposure.target.TransitionColor(
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        state.stats.exposureWritten = true;
        return true;
    }

    void MarkTemporalMasksWritten(bool written, bool compositionDerived) {
        TemporalResourceState& state = State();
        state.stats.masksWritten =
            written &&
            state.reactiveMask.ready &&
            state.transparencyMask.ready &&
            state.invalidDepthMotionMask.ready;
        state.stats.masksCompositionDerived =
            state.stats.masksWritten && compositionDerived;
    }

    void SetTemporalGeometryDrawCounts(
        uint32_t rigid,
        uint32_t skinned,
        uint32_t alphaMasked) {

        TemporalResourceState& state = State();
        state.stats.rigidVelocityDrawCount = rigid;
        state.stats.skinnedVelocityDrawCount = skinned;
        state.stats.alphaMaskedVelocityDrawCount = alphaMasked;
    }

    bool PrepareSceneColorInput(RenderTarget2D& source) {
        TemporalResourceState& state = State();
        ID3D12Resource* resource = source.GetResource();
        const DXGI_FORMAT format = source.GetFormat();
        const uint32_t width = static_cast<uint32_t>((std::max)(1, source.GetWidth()));
        const uint32_t height = static_cast<uint32_t>((std::max)(1, source.GetHeight()));
        if (!HasValidContext(state.context) ||
            resource == nullptr ||
            format == DXGI_FORMAT_UNKNOWN ||
            width == 0 ||
            height == 0) {
            ReleaseExternalTexture(state.sceneColor);
            RefreshStats(state);
            return false;
        }

        const bool recreate =
            !state.sceneColor.ready ||
            state.sceneColor.resource != resource ||
            state.sceneColor.format != format ||
            state.sceneColor.width != width ||
            state.sceneColor.height != height ||
            !state.sceneColor.srv.IsValid();
        if (recreate) {
            ReleaseExternalTexture(state.sceneColor);
            state.sceneColor.srv = RENDER3D::AllocateTexture2DSrvDescriptor(
                resource,
                format,
                0,
                1);
            if (!state.sceneColor.srv.IsValid()) {
                ReleaseExternalTexture(state.sceneColor);
                RefreshStats(state);
                return false;
            }
            state.sceneColor.resource = resource;
            state.sceneColor.format = format;
            state.sceneColor.width = width;
            state.sceneColor.height = height;
            state.sceneColor.ready = true;
        }
        state.sceneColor.state = source.GetColorState();
        RefreshStats(state);
        return true;
    }

    void MarkMotionVectorsWritten(bool written) {
        TemporalResourceState& state = State();
        state.stats.motionVectorWritten =
            written && state.motionVectors.ready && state.motionMetadata.ready;
    }

    void MarkTemporalAntiAliasing(bool enabled, bool resolved) {
        TemporalResourceState& state = State();
        state.stats.taaEnabled = enabled;
        state.stats.taaResolved =
            resolved && state.historyColorValid && state.historyDepthValid;
    }

    void CommitTemporalHistory(bool written) {
        TemporalResourceState& state = State();
        if (!written ||
            !state.historyColor[state.historyWriteIndex].ready ||
            !state.historyDepth[state.historyWriteIndex].ready) {
            return;
        }
        state.historyReadIndex = state.historyWriteIndex;
        state.historyWriteIndex = 1u - state.historyReadIndex;
        state.historyColorValid = true;
        state.historyDepthValid = true;
        RefreshStats(state);
    }

    TemporalInputs BuildTemporalInputs(
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv) {

        TemporalResourceState& state = State();
        TemporalInputs inputs{};
        inputs.frame = state.frame;
        inputs.sceneDepthSrv = sceneDepthSrv;
        inputs.sceneColorSrv = sceneColorSrv;
        inputs.hasSceneDepth = sceneDepthSrv.ptr != 0;
        inputs.hasSceneColor = sceneColorSrv.ptr != 0;
        inputs.sceneColor = MakeTextureView(state.sceneColor);
        inputs.sceneDepth = state.sceneDepth;
        inputs.motionVectors = MakeTextureView(state.motionVectors);
        inputs.motionMetadata = MakeTextureView(state.motionMetadata);
        if (state.historyColorValid) {
            inputs.historyColorRead =
                MakeTextureView(state.historyColor[state.historyReadIndex]);
        }
        inputs.historyColorWrite =
            MakeTextureView(state.historyColor[state.historyWriteIndex]);
        if (state.historyDepthValid) {
            inputs.historyDepthRead =
                MakeTextureView(state.historyDepth[state.historyReadIndex]);
        }
        inputs.historyDepthWrite =
            MakeTextureView(state.historyDepth[state.historyWriteIndex]);
        inputs.exposure = MakeTextureView(state.exposure);
        inputs.reactiveMask = MakeTextureView(state.reactiveMask);
        inputs.transparencyMask = MakeTextureView(state.transparencyMask);
        inputs.invalidDepthMotionMask =
            MakeTextureView(state.invalidDepthMotionMask);
        inputs.debugOutput = MakeTextureView(state.debugOutput);
        return inputs;
    }

    TemporalInputs BuildTemporalInputs(
        RenderTarget2D& sceneColorTarget,
        RenderTarget2D& sceneDepthTarget) {

        TemporalResourceState& state = State();
        TemporalInputs inputs = BuildTemporalInputs(
            sceneDepthTarget.GetDepthSrvGpu(),
            state.sceneColor.srv.gpu);
        inputs.hasSceneDepth =
            sceneDepthTarget.GetDepthResource() != nullptr &&
            inputs.sceneDepthSrv.ptr != 0;
        inputs.hasSceneColor =
            state.sceneColor.ready &&
            state.sceneColor.resource == sceneColorTarget.GetResource();
        inputs.sceneDepth.resource = sceneDepthTarget.GetDepthResource();
        inputs.sceneDepth.srv = inputs.sceneDepthSrv;
        inputs.sceneDepth.format = DXGI_FORMAT_R32_FLOAT;
        inputs.sceneDepth.width =
            static_cast<uint32_t>((std::max)(1, sceneDepthTarget.GetWidth()));
        inputs.sceneDepth.height =
            static_cast<uint32_t>((std::max)(1, sceneDepthTarget.GetHeight()));
        inputs.sceneDepth.state = sceneDepthTarget.GetDepthState();
        inputs.sceneDepth.valid = inputs.hasSceneDepth;
        state.sceneDepth = inputs.sceneDepth;
        state.sceneDepthOwner = &sceneDepthTarget;
        return inputs;
    }

    TemporalInputs BuildTemporalInputs(RenderTarget2D& sceneTarget) {
        return BuildTemporalInputs(sceneTarget, sceneTarget);
    }

    TemporalInputs GetCurrentTemporalInputs() {
        TemporalResourceState& state = State();
        TemporalInputs inputs = BuildTemporalInputs(
            state.sceneDepth.srv,
            state.sceneColor.srv.gpu);
        inputs.sceneDepth = state.sceneDepth;
        if (state.sceneDepthOwner != nullptr) {
            inputs.sceneDepth.state =
                state.sceneDepthOwner->GetDepthState();
        }
        inputs.hasSceneDepth = state.sceneDepth.valid;
        return inputs;
    }

    const TemporalResourceStats& GetTemporalResourceStats() {
        return State().stats;
    }

} // namespace HIKARI::RENDER3D::TEMPORAL
