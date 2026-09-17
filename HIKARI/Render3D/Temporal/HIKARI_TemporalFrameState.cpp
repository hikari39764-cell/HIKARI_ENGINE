#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    namespace {
        struct TemporalFrameStateStore {
            TemporalFrameState current{};
            TemporalCameraData committedCamera{};
            uint32_t committedRenderWidth = 0;
            uint32_t committedRenderHeight = 0;
            uint32_t committedOutputWidth = 0;
            uint32_t committedOutputHeight = 0;
            RenderViewPurpose committedPurpose = RenderViewPurpose::Game;
            uint64_t committedCameraSourceObjectId = 0;
            bool pendingExplicitReset = true;
            TemporalHistoryResetReason pendingResetReason =
                TemporalHistoryResetReason::FirstFrame;
            uint64_t committedHistoryConfigurationKey = 0;
            bool hasCommittedCamera = false;
            bool hasCommittedHistory = false;
            bool currentFrameCompleted = true;
            bool currentFrameResetRequested = false;
        };

        struct TemporalViewState {
            TemporalViewLease lease{};
            TemporalFrameStateStore frame{};
        };

        struct TemporalFrameStateSystem {
            std::unordered_map<uint64_t, TemporalViewState> views{};
            uint64_t nextLeaseGeneration = 1u;
            std::thread::id ownerThread{};
            bool ownerThreadAssigned = false;
            std::mutex ownerMutex{};
        };

        TemporalFrameStateSystem& System() {
            static TemporalFrameStateSystem system{};
            return system;
        }

        bool CheckOwnerThread(bool assignIfNeeded = false) {
            TemporalFrameStateSystem& system = System();
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

        MATH::Vec4 MakeSizeParams(uint32_t width, uint32_t height) {
            const uint32_t safeWidth = (std::max)(1u, width);
            const uint32_t safeHeight = (std::max)(1u, height);
            return {
                static_cast<float>(safeWidth),
                static_cast<float>(safeHeight),
                1.0f / static_cast<float>(safeWidth),
                1.0f / static_cast<float>(safeHeight)
            };
        }

        float Halton(uint32_t index, uint32_t base) {
            float result = 0.0f;
            float fraction = 1.0f / static_cast<float>(base);
            while (index > 0) {
                result += static_cast<float>(index % base) * fraction;
                index /= base;
                fraction /= static_cast<float>(base);
            }
            return result;
        }

        MATH::Vec4 MakeJitterParams(
            uint32_t phase,
            uint32_t renderWidth,
            uint32_t renderHeight,
            bool enabled) {

            if (!enabled || renderWidth == 0 || renderHeight == 0) {
                return { 0.0f, 0.0f, 0.0f, 0.0f };
            }

            const uint32_t sampleIndex = (phase % 16u) + 1u;
            // Center the finite 16-phase sequence itself. The raw Halton prefix
            // has a small non-zero mean that otherwise becomes a persistent
            // sub-pixel camera offset after convergence.
            constexpr float kPhaseMeanX = -0.029296875f;
            constexpr float kPhaseMeanY = -0.037037037f;
            const float pixelX = Halton(sampleIndex, 2u) - 0.5f - kPhaseMeanX;
            const float pixelY = Halton(sampleIndex, 3u) - 0.5f - kPhaseMeanY;
            return {
                pixelX,
                pixelY,
                (2.0f * pixelX) / static_cast<float>((std::max)(1u, renderWidth)),
                (-2.0f * pixelY) / static_cast<float>((std::max)(1u, renderHeight))
            };
        }

        MATH::Mat4 ApplyClipSpaceJitter(
            const MATH::Mat4& viewProj,
            const MATH::Vec4& jitter) {

            MATH::Mat4 result = viewProj;
            for (int column = 0; column < 4; ++column) {
                result.m[column][0] += jitter.z * viewProj.m[column][3];
                result.m[column][1] += jitter.w * viewProj.m[column][3];
            }
            return result;
        }

        TemporalCameraData BuildCameraData(
            const Camera3D& camera,
            const TemporalCameraData* previousCamera,
            bool previousValid,
            uint32_t renderWidth,
            uint32_t renderHeight,
            uint32_t outputWidth,
            uint32_t outputHeight,
            const MATH::Vec4& jitter) {

            TemporalCameraData data{};
            data.view = camera.GetView();
            data.proj = camera.GetProj();
            data.unjitteredViewProj = camera.GetViewProj();
            data.invUnjitteredViewProj = MATH::Inverse(data.unjitteredViewProj);
            data.jitter = jitter;
            data.jittered = std::abs(jitter.z) > 0.0f || std::abs(jitter.w) > 0.0f;
            data.viewProj = data.jittered
                ? ApplyClipSpaceJitter(data.unjitteredViewProj, jitter)
                : data.unjitteredViewProj;
            data.invViewProj = MATH::Inverse(data.viewProj);

            const MATH::Vec3 cameraPos = camera.GetPosition();
            data.cameraPos = { cameraPos.x, cameraPos.y, cameraPos.z, 1.0f };
            data.screenParams = MakeSizeParams(renderWidth, renderHeight);
            data.outputParams = MakeSizeParams(outputWidth, outputHeight);
            data.nearZ = camera.GetNearZ();
            data.farZ = camera.GetFarZ();
            data.fovYRad = camera.GetFovYRad();
            data.aspect = camera.GetAspect();
            data.valid = true;
            data.previousValid = previousValid;

            if (previousValid && previousCamera != nullptr) {
                data.prevViewProj = previousCamera->viewProj;
                data.prevUnjitteredViewProj =
                    previousCamera->unjitteredViewProj;
                data.prevCameraPos = previousCamera->cameraPos;
                data.prevJitter = previousCamera->jitter;
                data.clipToPrevClip = data.prevViewProj * data.invViewProj;
            } else {
                data.prevViewProj = data.viewProj;
                data.prevUnjitteredViewProj = data.unjitteredViewProj;
                data.prevCameraPos = data.cameraPos;
                data.prevJitter = { 0.0f, 0.0f, 0.0f, 0.0f };
                data.clipToPrevClip = MATH::Mat4::Identity();
            }
            return data;
        }

        TemporalHistoryResetReason ResolveResetReason(
            const TemporalFrameDesc& desc,
            const TemporalFrameStateStore& store) {

            if (desc.camera == nullptr ||
                desc.renderWidth == 0 ||
                desc.renderHeight == 0 ||
                desc.outputWidth == 0 ||
                desc.outputHeight == 0) {
                return TemporalHistoryResetReason::InvalidInput;
            }
            if (desc.forceHistoryReset) {
                return TemporalHistoryResetReason::ExplicitReset;
            }
            if (store.pendingExplicitReset) {
                return store.pendingResetReason;
            }
            if (!store.hasCommittedCamera) {
                return TemporalHistoryResetReason::FirstFrame;
            }
            if (!store.currentFrameCompleted) {
                return TemporalHistoryResetReason::PreviousFrameIncomplete;
            }
            if (store.current.publication.IsValid() &&
                desc.frame.frameIndex > store.current.frameIndex &&
                desc.frame.frameIndex - store.current.frameIndex > 1u) {
                return TemporalHistoryResetReason::FrameDiscontinuity;
            }
            if (desc.cameraCut) {
                return TemporalHistoryResetReason::CameraCut;
            }
            if (desc.purpose != store.committedPurpose) {
                return TemporalHistoryResetReason::ViewPurposeChanged;
            }
            if (desc.cameraSourceObjectId !=
                store.committedCameraSourceObjectId) {
                return TemporalHistoryResetReason::CameraSourceChanged;
            }
            if (desc.projectionChanged) {
                return TemporalHistoryResetReason::ProjectionChanged;
            }
            if (desc.historyConfigurationKey !=
                store.committedHistoryConfigurationKey) {
                return TemporalHistoryResetReason::ConfigurationChanged;
            }
            if (desc.renderWidth != store.committedRenderWidth ||
                desc.renderHeight != store.committedRenderHeight ||
                desc.outputWidth != store.committedOutputWidth ||
                desc.outputHeight != store.committedOutputHeight) {
                return TemporalHistoryResetReason::Resize;
            }
            return TemporalHistoryResetReason::None;
        }

        TemporalFrameState BuildRejectedFrame(
            const TemporalFrameDesc& desc,
            TemporalHistoryResetReason reason) {

            TemporalFrameState rejected{};
            rejected.view = desc.view;
            rejected.publication = desc.frame;
            rejected.purpose = desc.purpose;
            rejected.cameraSourceObjectId = desc.cameraSourceObjectId;
            rejected.cameraRevision = desc.cameraRevision;
            rejected.historyConfigurationKey =
                desc.historyConfigurationKey;
            rejected.frameIndex = desc.frame.frameIndex;
            rejected.renderWidth = (std::max)(1u, desc.renderWidth);
            rejected.renderHeight = (std::max)(1u, desc.renderHeight);
            rejected.outputWidth = (std::max)(1u, desc.outputWidth);
            rejected.outputHeight = (std::max)(1u, desc.outputHeight);
            rejected.historyValid = false;
            rejected.resetHistory = true;
            rejected.completed = true;
            rejected.aborted = true;
            rejected.temporalResolveAllowed = false;
            rejected.resetReason = reason;
            return rejected;
        }
    }

    const char* ToString(TemporalHistoryResetReason reason) {
        switch (reason) {
        case TemporalHistoryResetReason::None: return "none";
        case TemporalHistoryResetReason::FirstFrame: return "first-frame";
        case TemporalHistoryResetReason::Resize: return "resize";
        case TemporalHistoryResetReason::CameraCut: return "camera-cut";
        case TemporalHistoryResetReason::ProjectionChanged:
            return "projection-changed";
        case TemporalHistoryResetReason::ConfigurationChanged:
            return "configuration-changed";
        case TemporalHistoryResetReason::ViewPurposeChanged:
            return "view-purpose-changed";
        case TemporalHistoryResetReason::CameraSourceChanged:
            return "camera-source-changed";
        case TemporalHistoryResetReason::FrameDiscontinuity:
            return "frame-discontinuity";
        case TemporalHistoryResetReason::PreviousFrameIncomplete:
            return "previous-frame-incomplete";
        case TemporalHistoryResetReason::InvalidViewLease:
            return "invalid-view-lease";
        case TemporalHistoryResetReason::InvalidFrameToken:
            return "invalid-frame-token";
        case TemporalHistoryResetReason::InvalidInput: return "invalid-input";
        case TemporalHistoryResetReason::ExplicitReset: return "explicit-reset";
        default: return "unknown";
        }
    }

    bool AcquireTemporalView(
        RenderViewId viewId,
        TemporalViewLease& outLease) {

        outLease = {};
        if (viewId.value == 0u || !CheckOwnerThread(true)) {
            return false;
        }
        TemporalFrameStateSystem& system = System();
        const auto existing = system.views.find(viewId.value);
        if (existing != system.views.end()) {
            outLease = existing->second.lease;
            return true;
        }
        if (system.views.size() >= kTemporalMaximumViewCount) {
            return false;
        }

        uint64_t generation = system.nextLeaseGeneration++;
        if (generation == 0u) {
            generation = system.nextLeaseGeneration++;
        }
        TemporalViewState state{};
        state.lease = { viewId, generation };
        const auto [inserted, ok] =
            system.views.emplace(viewId.value, std::move(state));
        if (!ok) {
            return false;
        }
        outLease = inserted->second.lease;
        return true;
    }

    bool CanReleaseTemporalFrameViewState(TemporalViewLease lease) {
        if (!lease.IsValid() || !CheckOwnerThread()) {
            return false;
        }
        TemporalFrameStateSystem& system = System();
        const auto found = system.views.find(lease.viewId.value);
        if (found == system.views.end() ||
            !found->second.lease.Matches(lease)) {
            return false;
        }
        if (!found->second.frame.currentFrameCompleted) {
            return false;
        }
        return true;
    }

    bool ReleaseTemporalFrameViewState(TemporalViewLease lease) {
        if (!CanReleaseTemporalFrameViewState(lease)) {
            return false;
        }
        TemporalFrameStateSystem& system = System();
        const auto found = system.views.find(lease.viewId.value);
        if (found == system.views.end()) {
            return false;
        }
        system.views.erase(found);
        return true;
    }

    TemporalFrameState BeginTemporalFrame(const TemporalFrameDesc& desc) {
        if (!desc.view.IsValid() ||
            !desc.frame.IsValid() ||
            desc.view.viewId != desc.frame.viewId ||
            !CheckOwnerThread()) {
            return BuildRejectedFrame(
                desc,
                desc.view.IsValid()
                    ? TemporalHistoryResetReason::InvalidFrameToken
                    : TemporalHistoryResetReason::InvalidViewLease);
        }

        TemporalFrameStateSystem& system = System();
        const auto found = system.views.find(desc.view.viewId.value);
        if (found == system.views.end() ||
            !found->second.lease.Matches(desc.view)) {
            return BuildRejectedFrame(
                desc,
                TemporalHistoryResetReason::InvalidViewLease);
        }
        TemporalFrameStateStore& store = found->second.frame;
        if (store.current.publication.IsValid()) {
            if (store.current.publication == desc.frame) {
                return store.current;
            }
            if (desc.frame.frameIndex <= store.current.frameIndex ||
                desc.frame.generation <=
                    store.current.publication.generation) {
                return BuildRejectedFrame(
                    desc,
                    TemporalHistoryResetReason::InvalidFrameToken);
            }
        }

        TemporalFrameState next{};
        next.view = desc.view;
        next.publication = desc.frame;
        next.purpose = desc.purpose;
        next.cameraSourceObjectId = desc.cameraSourceObjectId;
        next.cameraRevision = desc.cameraRevision;
        next.historyConfigurationKey = desc.historyConfigurationKey;
        next.frameIndex = desc.frame.frameIndex;
        next.renderWidth = (std::max)(1u, desc.renderWidth);
        next.renderHeight = (std::max)(1u, desc.renderHeight);
        next.outputWidth = (std::max)(1u, desc.outputWidth);
        next.outputHeight = (std::max)(1u, desc.outputHeight);
        next.resetReason = ResolveResetReason(desc, store);
        next.resetHistory = next.resetReason != TemporalHistoryResetReason::None;
        next.historyValid =
            !next.resetHistory &&
            store.hasCommittedHistory;
        next.temporalResolveAllowed =
            desc.temporalResolveAllowed &&
            !(desc.camera && desc.camera->IsOrthographic()) &&
            !desc.forceHistoryReset &&
            next.resetReason != TemporalHistoryResetReason::InvalidInput;
        next.jitterEnabled = desc.jitterEnabled && next.temporalResolveAllowed;
        next.jitterPhase =
            static_cast<uint32_t>(desc.frame.frameIndex & 0xffffu);
        const MATH::Vec4 jitter = MakeJitterParams(
            next.jitterPhase,
            next.renderWidth,
            next.renderHeight,
            next.jitterEnabled);

        const bool previousCameraValid =
            !next.resetHistory &&
            store.hasCommittedCamera &&
            store.committedCamera.valid;
        if (desc.camera != nullptr) {
            next.camera = BuildCameraData(
                *desc.camera,
                &store.committedCamera,
                previousCameraValid,
                next.renderWidth,
                next.renderHeight,
                next.outputWidth,
                next.outputHeight,
                jitter);
        }

        if (next.resetHistory) {
            store.hasCommittedCamera = false;
            store.hasCommittedHistory = false;
        }
        store.pendingExplicitReset = false;
        store.pendingResetReason = TemporalHistoryResetReason::None;
        store.current = next;
        store.currentFrameCompleted = false;
        store.currentFrameResetRequested = false;
        return store.current;
    }

    TemporalFrameState GetLatestTemporalFrameState(RenderViewId viewId) {
        if (viewId.value == 0 || !CheckOwnerThread()) {
            return {};
        }
        const TemporalFrameStateSystem& system = System();
        const auto found = system.views.find(viewId.value);
        return found != system.views.end()
            ? found->second.frame.current
            : TemporalFrameState{};
    }

    bool TryGetTemporalFrameState(
        TemporalViewLease lease,
        RenderFrameToken expected,
        TemporalFrameState& outState) {

        outState = {};
        if (!lease.IsValid() ||
            !expected.IsValid() ||
            lease.viewId != expected.viewId ||
            !CheckOwnerThread()) {
            return false;
        }
        const TemporalFrameStateSystem& system = System();
        const auto found = system.views.find(lease.viewId.value);
        if (found == system.views.end() ||
            !found->second.lease.Matches(lease) ||
            !found->second.frame.current.publication.Matches(expected)) {
            return false;
        }
        outState = found->second.frame.current;
        return true;
    }

    bool ResetTemporalFrameHistory(
        TemporalViewLease lease,
        TemporalHistoryResetReason reason) {

        if (!lease.IsValid() || !CheckOwnerThread()) {
            return false;
        }
        TemporalFrameStateSystem& system = System();
        const auto found = system.views.find(lease.viewId.value);
        if (found == system.views.end() ||
            !found->second.lease.Matches(lease)) {
            return false;
        }
        TemporalFrameStateStore& store = found->second.frame;
        store.pendingExplicitReset = true;
        store.pendingResetReason =
            reason == TemporalHistoryResetReason::None
                ? TemporalHistoryResetReason::ExplicitReset
                : reason;
        store.hasCommittedCamera = false;
        store.hasCommittedHistory = false;
        if (!store.currentFrameCompleted) {
            store.currentFrameResetRequested = true;
            store.current.historyValid = false;
            store.current.resetHistory = true;
            store.current.temporalResolveAllowed = false;
            store.current.resetReason = store.pendingResetReason;
            store.current.camera.previousValid = false;
        }
        return true;
    }

    bool CompleteTemporalFrame(
        TemporalViewLease lease,
        RenderFrameToken frame,
        bool historyWritten) {

        if (!lease.IsValid() ||
            !frame.IsValid() ||
            lease.viewId != frame.viewId ||
            !CheckOwnerThread()) {
            return false;
        }
        TemporalFrameStateSystem& system = System();
        const auto found = system.views.find(lease.viewId.value);
        if (found == system.views.end() ||
            !found->second.lease.Matches(lease) ||
            !found->second.frame.current.publication.Matches(frame)) {
            return false;
        }
        TemporalFrameStateStore& store = found->second.frame;
        if (store.currentFrameCompleted) {
            return false;
        }
        store.currentFrameCompleted = true;
        store.current.completed = true;
        if (store.currentFrameResetRequested) {
            store.current.aborted = true;
            store.hasCommittedCamera = false;
            store.hasCommittedHistory = false;
            return false;
        }
        store.hasCommittedCamera = store.current.camera.valid;
        store.current.aborted = false;
        store.hasCommittedHistory =
            historyWritten && store.current.camera.valid;
        if (store.hasCommittedCamera) {
            store.committedCamera = store.current.camera;
            store.committedRenderWidth = store.current.renderWidth;
            store.committedRenderHeight = store.current.renderHeight;
            store.committedOutputWidth = store.current.outputWidth;
            store.committedOutputHeight = store.current.outputHeight;
            store.committedPurpose = store.current.purpose;
            store.committedCameraSourceObjectId =
                store.current.cameraSourceObjectId;
            store.committedHistoryConfigurationKey =
                store.current.historyConfigurationKey;
        }
        return true;
    }

    bool AbortTemporalFrame(
        TemporalViewLease lease,
        RenderFrameToken frame,
        TemporalHistoryResetReason reason) {

        if (!lease.IsValid() ||
            !frame.IsValid() ||
            lease.viewId != frame.viewId ||
            !CheckOwnerThread()) {
            return false;
        }
        TemporalFrameStateSystem& system = System();
        const auto found = system.views.find(lease.viewId.value);
        if (found == system.views.end() ||
            !found->second.lease.Matches(lease) ||
            !found->second.frame.current.publication.Matches(frame)) {
            return false;
        }
        TemporalFrameStateStore& store = found->second.frame;
        if (store.currentFrameCompleted) {
            return false;
        }
        store.currentFrameCompleted = true;
        store.currentFrameResetRequested = true;
        store.current.completed = true;
        store.current.aborted = true;
        store.hasCommittedCamera = false;
        store.hasCommittedHistory = false;
        const TemporalHistoryResetReason abortReason =
            reason == TemporalHistoryResetReason::None
                ? TemporalHistoryResetReason::PreviousFrameIncomplete
                : reason;
        if (!store.pendingExplicitReset) {
            store.pendingExplicitReset = true;
            store.pendingResetReason = abortReason;
        }
        store.current.historyValid = false;
        store.current.resetHistory = true;
        store.current.temporalResolveAllowed = false;
        store.current.resetReason = store.pendingResetReason;
        store.current.camera.previousValid = false;
        return true;
    }

    void ShutdownTemporalFrameStateSystem() {
        TemporalFrameStateSystem& system = System();
        {
            std::scoped_lock lock(system.ownerMutex);
            if (!system.ownerThreadAssigned) {
                return;
            }
            if (system.ownerThread != std::this_thread::get_id()) {
                return;
            }
        }
        system.views.clear();
        {
            std::scoped_lock lock(system.ownerMutex);
            system.ownerThread = {};
            system.ownerThreadAssigned = false;
        }
    }

    uint32_t GetTemporalFrameStateViewCount() {
        if (!CheckOwnerThread()) {
            return 0;
        }
        return static_cast<uint32_t>(System().views.size());
    }

} // namespace HIKARI::RENDER3D::TEMPORAL
