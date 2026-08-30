#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

#include <algorithm>
#include <array>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include "Gfx/HIKARI_GpuDeferredReleaseQueue.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalResourceSystemInternal.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    namespace DETAIL {
        constexpr DXGI_FORMAT kMotionVectorFormat = DXGI_FORMAT_R16G16_FLOAT;
        constexpr DXGI_FORMAT kMotionMetadataFormat = DXGI_FORMAT_R16G16_FLOAT;
        constexpr DXGI_FORMAT kHistoryColorFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        constexpr DXGI_FORMAT kHistoryDepthFormat = DXGI_FORMAT_R32_FLOAT;
        constexpr DXGI_FORMAT kMaskFormat = DXGI_FORMAT_R8_UNORM;
        constexpr DXGI_FORMAT kExposureFormat = DXGI_FORMAT_R32_FLOAT;

        TemporalResourceSystemState& System() {
            static TemporalResourceSystemState system{};
            return system;
        }

        bool CheckOwnerThread(bool assignIfNeeded) {
            TemporalResourceSystemState& system = System();
            const std::thread::id currentThread = std::this_thread::get_id();
            std::scoped_lock lock(system.ownerMutex);
            if (!system.ownerThreadAssigned) {
                if (!assignIfNeeded) {
                    return false;
                }
                system.ownerThread = currentThread;
                system.ownerThreadAssigned = true;
                return true;
            }
            return system.ownerThread == currentThread;
        }

        bool HasValidContext(const GFX::Context& ctx) {
            return ctx.deviceGeneration != 0u &&
                ctx.device != nullptr &&
                ctx.cmdList != nullptr &&
                ctx.queue != nullptr &&
                ctx.srvHeap != nullptr &&
                ctx.resourceStates != nullptr &&
                ctx.deferredReleaseQueue != nullptr &&
                ctx.frameFence != nullptr &&
                ctx.currentFrameRetireFenceValue != 0u;
        }

        bool ContextMatchesOwner(
            const TemporalResourceState& state,
            const GFX::Context& ctx) {

            return state.ownerDeviceGeneration == ctx.deviceGeneration &&
                state.ownerDevice == ctx.device &&
                state.ownerQueue == ctx.queue &&
                state.ownerHeap == ctx.srvHeap &&
                state.ownerReleaseQueue == ctx.deferredReleaseQueue &&
                state.ownerResourceStates == ctx.resourceStates &&
                state.ownerFrameFence == ctx.frameFence;
        }

        uint64_t AllocateResourceGeneration() {
            TemporalResourceSystemState& system = System();
            uint64_t generation = system.nextResourceGeneration++;
            if (generation == 0u) {
                generation = system.nextResourceGeneration++;
            }
            return generation;
        }

        bool UsesDeferredRelease(TemporalResourceReleaseMode mode) {
            return mode == TemporalResourceReleaseMode::DeferredUntilFence;
        }

        void RetireDescriptor(
            TemporalResourceState& state,
            RenderResourceView& view,
            TemporalResourceReleaseMode mode,
            const char* debugName) {

            if (!view.IsValid()) {
                return;
            }
            const RenderResourceView retired = view;
            view = {};
            if (UsesDeferredRelease(mode) &&
                state.retirementContext.deferredReleaseQueue != nullptr &&
                state.retirementContext.currentFrameRetireFenceValue != 0u) {
                state.retirementContext.deferredReleaseQueue->Enqueue(
                    state.retirementContext.currentFrameRetireFenceValue,
                    [retired]() {
                        (void)RENDER3D::ReleaseRenderResourceDescriptor(retired);
                    },
                    debugName != nullptr ? debugName : "Temporal.Descriptor");
                return;
            }
            (void)RENDER3D::ReleaseRenderResourceDescriptor(retired);
        }

        void ReleaseTarget(
            TemporalResourceState& state,
            TemporalTarget& target,
            TemporalResourceReleaseMode mode) {

            RetireDescriptor(
                state,
                target.srv,
                mode,
                "Temporal.TargetSRV");
            target.target.UpdateContext(
                UsesDeferredRelease(mode)
                    ? state.retirementContext
                    : GFX::Context{});
            target.target.Finalize();
            target.format = DXGI_FORMAT_UNKNOWN;
            target.width = 0;
            target.height = 0;
            target.ready = false;
        }

        void ReleaseExternalTexture(
            TemporalResourceState& state,
            TemporalExternalTexture& texture,
            TemporalResourceReleaseMode mode) {

            RetireDescriptor(
                state,
                texture.srv,
                mode,
                "Temporal.ExternalSRV");
            texture.resource = nullptr;
            texture.format = DXGI_FORMAT_UNKNOWN;
            texture.width = 0;
            texture.height = 0;
            texture.state = D3D12_RESOURCE_STATE_COMMON;
            texture.ready = false;
        }

        void ReleaseAllResources(
            TemporalResourceState& state,
            TemporalResourceReleaseMode mode) {

            ReleaseTemporalPassScratch(state, mode);
            ReleaseExternalTexture(state, state.sceneColor, mode);
            ReleaseTarget(state, state.motionVectors, mode);
            ReleaseTarget(state, state.motionMetadata, mode);
            ReleaseTarget(state, state.historyColor[0], mode);
            ReleaseTarget(state, state.historyColor[1], mode);
            ReleaseTarget(state, state.historyDepth[0], mode);
            ReleaseTarget(state, state.historyDepth[1], mode);
            ReleaseTarget(state, state.taaResolvedColor, mode);
            ReleaseTarget(state, state.exposure, mode);
            ReleaseTarget(state, state.reactiveMask, mode);
            ReleaseTarget(state, state.transparencyMask, mode);
            ReleaseTarget(state, state.invalidDepthMotionMask, mode);
            ReleaseTarget(state, state.debugOutput, mode);
            state.historyColorValid = false;
            state.historyDepthValid = false;
            state.historyWritten = {};
            state.historyOutputWritten = {};
        }

        RenderTextureView MakeTextureView(const TemporalTarget& target) {
            return {
                target.target.GetResource(),
                target.srv.gpu,
                target.format,
                target.width,
                target.height,
                1,
                target.target.GetColorState(),
                target.ready && target.target.GetResource() != nullptr &&
                    target.srv.gpu.ptr != 0u
            };
        }

        RenderTextureView MakeTextureView(
            const TemporalExternalTexture& texture) {

            return {
                texture.resource,
                texture.srv.gpu,
                texture.format,
                texture.width,
                texture.height,
                1,
                texture.state,
                texture.ready && texture.resource != nullptr &&
                    texture.srv.gpu.ptr != 0u
            };
        }

        bool EnsureTarget(
            TemporalResourceState& state,
            TemporalTarget& target,
            const GFX::Context& ctx,
            uint32_t width,
            uint32_t height,
            DXGI_FORMAT format,
            const char* debugName,
            const std::array<float, 4>& clearColor) {

            const uint32_t safeWidth = (std::max)(1u, width);
            const uint32_t safeHeight = (std::max)(1u, height);
            const bool recreate =
                !target.ready ||
                target.width != safeWidth ||
                target.height != safeHeight ||
                target.format != format ||
                target.target.GetResource() == nullptr;
            if (!recreate) {
                target.target.UpdateContext(ctx);
                return true;
            }

            // Release with the context that created/last used the old target.
            // Only after retirement may the target adopt the new recording
            // context for resource creation.
            ReleaseTarget(
                state,
                target,
                TemporalResourceReleaseMode::DeferredUntilFence);
            target.target.UpdateContext(ctx);
            target.target.SetDebugName(
                debugName != nullptr ? debugName : "Temporal.Target");
            if (!target.target.Init(
                    static_cast<int>(safeWidth),
                    static_cast<int>(safeHeight),
                    format,
                    false,
                    clearColor,
                    false)) {
                ReleaseTarget(
                    state,
                    target,
                    TemporalResourceReleaseMode::DeferredUntilFence);
                return false;
            }

            target.srv = RENDER3D::AllocateTexture2DSrvDescriptor(
                target.target.GetResource(),
                format,
                0,
                1);
            if (!target.srv.IsValid()) {
                ReleaseTarget(
                    state,
                    target,
                    TemporalResourceReleaseMode::DeferredUntilFence);
                return false;
            }

            target.format = format;
            target.width = safeWidth;
            target.height = safeHeight;
            target.ready = true;
            return true;
        }

        bool EnsureResources(
            TemporalResourceState& state,
            const TemporalFrameState& frame) {

            if (!HasValidContext(state.recordingContext)) {
                return false;
            }

            const uint32_t width = (std::max)(1u, frame.renderWidth);
            const uint32_t height = (std::max)(1u, frame.renderHeight);
            const bool sizeChanged =
                state.stats.width != 0u &&
                (state.stats.width != width || state.stats.height != height);

            bool ok = true;
            ok = EnsureTarget(
                state,
                state.motionVectors,
                state.recordingContext,
                width,
                height,
                kMotionVectorFormat,
                "Temporal.MotionVectors",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.motionMetadata,
                state.recordingContext,
                width,
                height,
                kMotionMetadataFormat,
                "Temporal.MotionMetadata",
                { 1.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.historyColor[0],
                state.recordingContext,
                width,
                height,
                kHistoryColorFormat,
                "Temporal.HistoryColor0",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.historyColor[1],
                state.recordingContext,
                width,
                height,
                kHistoryColorFormat,
                "Temporal.HistoryColor1",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.historyDepth[0],
                state.recordingContext,
                width,
                height,
                kHistoryDepthFormat,
                "Temporal.HistoryDepth0",
                { 1.0f, 1.0f, 1.0f, 1.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.historyDepth[1],
                state.recordingContext,
                width,
                height,
                kHistoryDepthFormat,
                "Temporal.HistoryDepth1",
                { 1.0f, 1.0f, 1.0f, 1.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.taaResolvedColor,
                state.recordingContext,
                width,
                height,
                kHistoryColorFormat,
                "Temporal.TaaResolvedColor",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.exposure,
                state.recordingContext,
                1u,
                1u,
                kExposureFormat,
                "Temporal.Exposure",
                { 1.0f, 1.0f, 1.0f, 1.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.reactiveMask,
                state.recordingContext,
                width,
                height,
                kMaskFormat,
                "Temporal.ReactiveMask",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.transparencyMask,
                state.recordingContext,
                width,
                height,
                kMaskFormat,
                "Temporal.TransparencyMask",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.invalidDepthMotionMask,
                state.recordingContext,
                width,
                height,
                kMaskFormat,
                "Temporal.InvalidDepthMotionMask",
                { 0.0f, 0.0f, 0.0f, 0.0f }) && ok;
            ok = EnsureTarget(
                state,
                state.debugOutput,
                state.recordingContext,
                width,
                height,
                kHistoryColorFormat,
                "Temporal.DebugOutput",
                { 0.0f, 0.0f, 0.0f, 1.0f }) && ok;

            if (!ok) {
                // A partially initialized bundle is never published. Retire
                // every member so the next Begin starts from one cold state.
                ReleaseAllResources(
                    state,
                    TemporalResourceReleaseMode::DeferredUntilFence);
                return false;
            }
            if (sizeChanged) {
                ++state.stats.resourceResizeCount;
            }
            return true;
        }

        bool HasCompleteBundle(const TemporalResourceState& state) {
            return state.motionVectors.ready &&
                state.motionMetadata.ready &&
                state.historyColor[0].ready &&
                state.historyColor[1].ready &&
                state.historyDepth[0].ready &&
                state.historyDepth[1].ready &&
                state.taaResolvedColor.ready &&
                state.exposure.ready &&
                state.reactiveMask.ready &&
                state.transparencyMask.ready &&
                state.invalidDepthMotionMask.ready &&
                state.debugOutput.ready;
        }

        bool StampMatches(
            TemporalResourceFrame stamp,
            TemporalResourceFrame expected) {

            return stamp.Matches(expected);
        }

        bool HistoryMatchesPrevious(const TemporalResourceState& state) {
            const TemporalResourceFrame& history = state.historyWritten;
            const TemporalResourceFrame& current = state.activeFrame;
            return history.IsValid() && current.IsValid() &&
                history.view.Matches(current.view) &&
                history.resourceGeneration == current.resourceGeneration &&
                history.deviceGeneration == current.deviceGeneration &&
                current.publication.frameIndex >
                    history.publication.frameIndex &&
                current.publication.frameIndex -
                    history.publication.frameIndex == 1u;
        }

        void RefreshStats(TemporalResourceState& state) {
            state.stats.view = state.view;
            state.stats.resourceFrame = state.activeFrame;
            state.stats.initialized =
                state.ownerDeviceGeneration != 0u &&
                state.ownerDevice != nullptr &&
                HasCompleteBundle(state);
            state.stats.active = state.active;
            state.stats.contextCompatible = state.contextCompatible;
            state.stats.width = state.frame.renderWidth;
            state.stats.height = state.frame.renderHeight;
            state.stats.motionVectorFormat = kMotionVectorFormat;
            state.stats.motionMetadataFormat = kMotionMetadataFormat;
            state.stats.historyColorFormat = kHistoryColorFormat;
            state.stats.historyDepthFormat = kHistoryDepthFormat;
            state.stats.sceneColorReady =
                state.sceneColor.ready &&
                StampMatches(state.sceneColorPrepared, state.activeFrame);
            state.stats.motionVectorReady = state.motionVectors.ready;
            state.stats.motionMetadataReady = state.motionMetadata.ready;
            state.stats.motionVectorWritten =
                StampMatches(state.motionWritten, state.activeFrame);
            state.stats.historyColorReady =
                state.historyColor[0].ready && state.historyColor[1].ready;
            state.stats.historyColorValid = state.historyColorValid;
            state.stats.historyDepthReady =
                state.historyDepth[0].ready && state.historyDepth[1].ready;
            state.stats.historyDepthValid = state.historyDepthValid;
            state.stats.taaResolvedColorReady = state.taaResolvedColor.ready;
            state.stats.exposureReady = state.exposure.ready;
            state.stats.exposureMode = state.exposureState.mode;
            state.stats.exposureMultiplier =
                state.exposureState.exposureMultiplier;
            state.stats.preExposure = state.exposureState.preExposure;
            state.stats.exposureWritten =
                StampMatches(state.exposureWritten, state.activeFrame);
            state.stats.reactiveMaskReady = state.reactiveMask.ready;
            state.stats.transparencyMaskReady = state.transparencyMask.ready;
            state.stats.invalidDepthMotionMaskReady =
                state.invalidDepthMotionMask.ready;
            state.stats.masksWritten =
                StampMatches(state.masksWritten, state.activeFrame);
            state.stats.debugOutputReady = state.debugOutput.ready;
            state.stats.debugView = state.debugView;
            state.stats.resourceGeneration =
                state.activeFrame.IsValid()
                    ? state.activeFrame.resourceGeneration
                    : state.stats.resourceGeneration;
        }

        TemporalResourceState* FindActiveState(TemporalResourceFrame frame) {
            if (!frame.IsValid() || !CheckOwnerThread()) {
                return nullptr;
            }
            TemporalResourceSystemState& system = System();
            const auto found = system.views.find(frame.view.viewId.value);
            if (found == system.views.end() || found->second == nullptr) {
                return nullptr;
            }
            TemporalResourceState& state = *found->second;
            if (!state.active ||
                !state.view.Matches(frame.view) ||
                !state.activeFrame.Matches(frame) ||
                state.ownerDeviceGeneration != frame.deviceGeneration) {
                return nullptr;
            }
            TemporalFrameState cpuFrame{};
            if (!TryGetTemporalFrameState(
                    frame.view,
                    frame.publication,
                    cpuFrame) ||
                !cpuFrame.publication.Matches(frame.publication)) {
                return nullptr;
            }
            return &state;
        }

        TemporalResourceState* FindWritableState(
            TemporalResourceFrame frame) {

            TemporalResourceState* state = FindActiveState(frame);
            if (state == nullptr) {
                return nullptr;
            }
            TemporalFrameState cpuFrame{};
            if (!TryGetTemporalFrameState(
                    frame.view,
                    frame.publication,
                    cpuFrame) ||
                cpuFrame.completed ||
                cpuFrame.aborted) {
                return nullptr;
            }
            return state;
        }

        TemporalResourceState* FindViewState(TemporalViewLease view) {
            if (!view.IsValid() || !CheckOwnerThread()) {
                return nullptr;
            }
            TemporalResourceSystemState& system = System();
            const auto found = system.views.find(view.viewId.value);
            if (found == system.views.end() || found->second == nullptr ||
                !found->second->view.Matches(view)) {
                return nullptr;
            }
            return found->second.get();
        }

        TemporalResourceState* FindAnyViewState(RenderViewId viewId) {
            if (viewId.value == 0u || !CheckOwnerThread()) {
                return nullptr;
            }
            TemporalResourceSystemState& system = System();
            const auto found = system.views.find(viewId.value);
            return found != system.views.end() && found->second != nullptr
                ? found->second.get()
                : nullptr;
        }

        void ClearFramePublication(
            TemporalResourceState& state,
            bool aborted) {

            state.terminalFrame = state.activeFrame;
            state.terminalAborted = aborted;
            state.active = false;
            state.activeFrame = {};
            state.sceneColorPrepared = {};
            state.compositionBaseWritten = {};
            state.motionWritten = {};
            state.masksWritten = {};
            state.exposureWritten = {};
            state.historyOutputWritten = {};
            state.compositionBaseSrv = {};
            state.sceneDepth = {};
            state.sceneDepthOwner = nullptr;
            state.stats.active = false;
            state.stats.resourceFrame = {};
            state.stats.motionVectorWritten = false;
            state.stats.masksWritten = false;
            state.stats.exposureWritten = false;
        }

        TemporalInputs BuildResidentInputs(
            const TemporalResourceState& state,
            const RenderTextureView& sceneDepth,
            const RenderTextureView& sceneColor) {

            TemporalInputs inputs{};
            inputs.resourceFrame = state.activeFrame;
            inputs.frame = state.frame;
            inputs.exposureState = state.exposureState;
            inputs.sceneDepth = sceneDepth;
            inputs.sceneColor = sceneColor;
            inputs.motionVectors = MakeTextureView(state.motionVectors);
            inputs.motionMetadata = MakeTextureView(state.motionMetadata);
            const bool historyCurrent = HistoryMatchesPrevious(state);
            if (state.frame.historyValid &&
                state.historyColorValid &&
                historyCurrent) {
                inputs.historyColorRead =
                    MakeTextureView(state.historyColor[state.historyReadIndex]);
            }
            inputs.historyColorWrite =
                MakeTextureView(state.historyColor[state.historyWriteIndex]);
            if (state.frame.historyValid &&
                state.historyDepthValid &&
                historyCurrent) {
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

        void ApplyResolveReadValidity(
            const TemporalResourceState& state,
            TemporalInputs& inputs) {

            if (!StampMatches(state.sceneColorPrepared, state.activeFrame)) {
                inputs.sceneColor = {};
            }
            if (!StampMatches(state.motionWritten, state.activeFrame)) {
                inputs.motionVectors = {};
                inputs.motionMetadata = {};
            }
            if (!StampMatches(state.exposureWritten, state.activeFrame)) {
                inputs.exposure = {};
            }
            if (!StampMatches(state.masksWritten, state.activeFrame)) {
                inputs.reactiveMask = {};
                inputs.transparencyMask = {};
                inputs.invalidDepthMotionMask = {};
            }
        }
    } // namespace DETAIL

    using namespace DETAIL;

    bool BeginTemporalResources(
        const GFX::Context& ctx,
        const TemporalFrameState& frame,
        TemporalResourceFrame& outFrame) {

        outFrame = {};
        if (!HasValidContext(ctx) ||
            !frame.view.IsValid() ||
            !frame.publication.IsValid() ||
            frame.view.viewId != frame.publication.viewId ||
            !CheckOwnerThread(true)) {
            return false;
        }

        TemporalFrameState verifiedFrame{};
        if (!TryGetTemporalFrameState(
                frame.view,
                frame.publication,
                verifiedFrame) ||
            !verifiedFrame.publication.Matches(frame.publication) ||
            verifiedFrame.completed ||
            verifiedFrame.aborted) {
            return false;
        }

        const RenderResourceDescriptorPoolStats descriptorStats =
            RENDER3D::GetRenderResourceDescriptorPoolStats();
        if (!descriptorStats.initialized ||
            descriptorStats.deviceGeneration != ctx.deviceGeneration) {
            return false;
        }

        TemporalResourceSystemState& system = System();
        auto found = system.views.find(frame.view.viewId.value);
        if (found != system.views.end() && found->second != nullptr &&
            !found->second->view.Matches(frame.view)) {
            if (found->second->active) {
                return false;
            }
            ReleaseAllResources(
                *found->second,
                TemporalResourceReleaseMode::DeferredUntilFence);
            system.views.erase(found);
            found = system.views.end();
        }

        if (found == system.views.end()) {
            if (system.views.size() >= kTemporalMaximumResidentViewCount) {
                return false;
            }
            auto state = std::make_unique<TemporalResourceState>();
            state->view = frame.view;
            state->ownerDeviceGeneration = ctx.deviceGeneration;
            state->ownerDevice = ctx.device;
            state->ownerQueue = ctx.queue;
            state->ownerHeap = ctx.srvHeap;
            state->ownerReleaseQueue = ctx.deferredReleaseQueue;
            state->ownerResourceStates = ctx.resourceStates;
            state->ownerFrameFence = ctx.frameFence;
            state->recordingContext = ctx;
            state->retirementContext = ctx;
            state->stats.view = frame.view;
            const auto [inserted, ok] = system.views.emplace(
                frame.view.viewId.value,
                std::move(state));
            if (!ok) {
                return false;
            }
            found = inserted;
        }

        TemporalResourceState& state = *found->second;
        if (!ContextMatchesOwner(state, ctx)) {
            state.contextCompatible = false;
            ++state.stats.rejectedContextCount;
            RefreshStats(state);
            return false;
        }
        state.contextCompatible = true;
        state.recordingContext = ctx;
        state.retirementContext = ctx;
        const bool bundleWasComplete = HasCompleteBundle(state);
        const uint64_t resizeCountBefore = state.stats.resourceResizeCount;

        if (state.active) {
            if (state.activeFrame.publication.Matches(frame.publication) &&
                state.activeFrame.view.Matches(frame.view)) {
                outFrame = state.activeFrame;
                return true;
            }
            state.historyColorValid = false;
            state.historyDepthValid = false;
            state.historyWritten = {};
            state.historyOutputWritten = {};
            ClearFramePublication(state, true);
        }

        if (!EnsureResources(state, verifiedFrame)) {
            state.stats.initialized = false;
            RefreshStats(state);
            system.views.erase(frame.view.viewId.value);
            return false;
        }
        if (!bundleWasComplete ||
            state.stats.resourceResizeCount != resizeCountBefore ||
            state.stats.resourceGeneration == 0u) {
            state.stats.resourceGeneration = AllocateResourceGeneration();
        }

        TemporalResourceFrame resourceFrame{};
        resourceFrame.view = frame.view;
        resourceFrame.publication = frame.publication;
        resourceFrame.resourceGeneration = state.stats.resourceGeneration;
        resourceFrame.deviceGeneration = ctx.deviceGeneration;

        state.frame = verifiedFrame;
        state.activeFrame = resourceFrame;
        state.active = true;
        state.sceneColorPrepared = {};
        state.compositionBaseWritten = {};
        state.motionWritten = {};
        state.masksWritten = {};
        state.exposureWritten = {};
        state.historyOutputWritten = {};
        state.compositionBaseSrv = {};
        state.sceneDepth = {};
        state.sceneDepthOwner = nullptr;
        state.stats.motionVectorWritten = false;
        state.stats.taaEnabled = false;
        state.stats.taaResolved = false;
        state.stats.masksWritten = false;
        state.stats.masksCompositionDerived = false;
        state.stats.exposureWritten = false;
        state.stats.rigidVelocityDrawCount = 0;
        state.stats.skinnedVelocityDrawCount = 0;
        state.stats.alphaMaskedVelocityDrawCount = 0;
        if (verifiedFrame.resetHistory) {
            if (state.historyColorValid ||
                state.historyDepthValid ||
                state.stats.lastResetReason != verifiedFrame.resetReason) {
                ++state.stats.historyInvalidationCount;
            }
            state.historyColorValid = false;
            state.historyDepthValid = false;
            state.historyWritten = {};
            state.historyOutputWritten = {};
            ClearTemporalGeometryHistory(state);
            state.stats.lastResetReason = verifiedFrame.resetReason;
        }
        state.historyWriteIndex = 1u - state.historyReadIndex;
        RefreshStats(state);
        outFrame = resourceFrame;
        return true;
    }

    bool EndTemporalResources(TemporalResourceFrame frame) {
        TemporalResourceState* state = FindActiveState(frame);
        if (state == nullptr) {
            TemporalResourceState* terminal = FindViewState(frame.view);
            return terminal != nullptr &&
                terminal->terminalFrame.Matches(frame) &&
                !terminal->terminalAborted;
        }
        TemporalFrameState terminalFrame{};
        if (!TryGetTemporalFrameState(
                frame.view,
                frame.publication,
                terminalFrame) ||
            !terminalFrame.completed || terminalFrame.aborted) {
            return false;
        }
        ClearFramePublication(*state, false);
        RefreshStats(*state);
        return true;
    }

    bool AbortTemporalResources(
        TemporalResourceFrame frame,
        TemporalHistoryResetReason reason) {
        TemporalResourceState* state = FindActiveState(frame);
        if (state == nullptr) {
            TemporalResourceState* terminal = FindViewState(frame.view);
            return terminal != nullptr &&
                terminal->terminalFrame.Matches(frame);
        }
        state->historyColorValid = false;
        state->historyDepthValid = false;
        state->historyWritten = {};
        state->historyOutputWritten = {};
        ClearTemporalGeometryHistory(*state);
        ++state->stats.historyInvalidationCount;
        if (!AbortTemporalFrame(
                frame.view,
                frame.publication,
                reason)) {
            (void)ResetTemporalFrameHistory(
                frame.view,
                reason);
        }
        ClearFramePublication(*state, true);
        RefreshStats(*state);
        return true;
    }

    bool ReleaseTemporalResources(
        TemporalViewLease view,
        TemporalResourceReleaseMode mode) {

        TemporalResourceState* state = FindViewState(view);
        if (state == nullptr || state->active) {
            return false;
        }
        if (mode == TemporalResourceReleaseMode::DeferredUntilFence &&
            (state->retirementContext.deferredReleaseQueue == nullptr ||
                state->retirementContext.currentFrameRetireFenceValue == 0u)) {
            return false;
        }
        if (mode == TemporalResourceReleaseMode::ImmediateAfterGpuIdle &&
            (state->retirementContext.frameFence == nullptr ||
                state->retirementContext.frameFence->GetCompletedValue() <
                    state->retirementContext.currentFrameRetireFenceValue)) {
            return false;
        }
        TemporalResourceSystemState& system = System();
        ReleaseAllResources(*state, mode);
        system.views.erase(view.viewId.value);
        return true;
    }

    bool ReleaseTemporalView(
        TemporalViewLease view,
        TemporalResourceReleaseMode mode) {

        if (!CanReleaseTemporalFrameViewState(view)) {
            return false;
        }
        TemporalResourceState* state = FindAnyViewState(view.viewId);
        if (state != nullptr) {
            if (!state->view.Matches(view) ||
                state->active ||
                !ReleaseTemporalResources(view, mode)) {
                return false;
            }
        }
        return ReleaseTemporalFrameViewState(view);
    }

    void ShutdownTemporalResourceSystem(TemporalResourceReleaseMode mode) {
        TemporalResourceSystemState& system = System();
        {
            std::scoped_lock lock(system.ownerMutex);
            if (!system.ownerThreadAssigned) {
                return;
            }
            if (system.ownerThread != std::this_thread::get_id()) {
                return;
            }
        }
        for (auto& [viewId, state] : system.views) {
            (void)viewId;
            if (state == nullptr) {
                continue;
            }
            if (state->activeFrame.IsValid()) {
                (void)AbortTemporalFrame(
                    state->activeFrame.view,
                    state->activeFrame.publication,
                    TemporalHistoryResetReason::ExplicitReset);
            }
            TemporalResourceReleaseMode releaseMode = mode;
            if (mode ==
                    TemporalResourceReleaseMode::ImmediateAfterGpuIdle &&
                (state->retirementContext.frameFence == nullptr ||
                    state->retirementContext.frameFence->
                        GetCompletedValue() <
                    state->retirementContext.
                        currentFrameRetireFenceValue)) {
                releaseMode =
                    TemporalResourceReleaseMode::DeferredUntilFence;
            }
            ReleaseAllResources(*state, releaseMode);
        }
        system.views.clear();
        {
            std::scoped_lock lock(system.ownerMutex);
            system.ownerThread = {};
            system.ownerThreadAssigned = false;
        }
    }

} // namespace HIKARI::RENDER3D::TEMPORAL
