#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

#include <algorithm>
#include <cmath>

#include "Render3D/Core/HIKARI_Camera3D.h"

namespace HIKARI::RENDER3D::TEMPORAL {

    namespace {
        struct TemporalFrameStateStore {
            TemporalFrameState current{};
            TemporalCameraData previousCamera{};
            uint32_t previousRenderWidth = 0;
            uint32_t previousRenderHeight = 0;
            uint32_t previousOutputWidth = 0;
            uint32_t previousOutputHeight = 0;
            bool hasPreviousCamera = false;
            bool pendingExplicitReset = true;
            TemporalHistoryResetReason pendingResetReason =
                TemporalHistoryResetReason::FirstFrame;
        };

        TemporalFrameStateStore& Store() {
            static TemporalFrameStateStore store{};
            return store;
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
            if (!store.hasPreviousCamera) {
                return TemporalHistoryResetReason::FirstFrame;
            }
            if (desc.forceHistoryReset) {
                return TemporalHistoryResetReason::ExplicitReset;
            }
            if (store.pendingExplicitReset) {
                return store.pendingResetReason;
            }
            if (desc.cameraCut) {
                return TemporalHistoryResetReason::CameraCut;
            }
            if (desc.renderWidth != store.previousRenderWidth ||
                desc.renderHeight != store.previousRenderHeight ||
                desc.outputWidth != store.previousOutputWidth ||
                desc.outputHeight != store.previousOutputHeight) {
                return TemporalHistoryResetReason::Resize;
            }
            return TemporalHistoryResetReason::None;
        }
    }

    const char* ToString(TemporalHistoryResetReason reason) {
        switch (reason) {
        case TemporalHistoryResetReason::None: return "none";
        case TemporalHistoryResetReason::FirstFrame: return "first-frame";
        case TemporalHistoryResetReason::Resize: return "resize";
        case TemporalHistoryResetReason::CameraCut: return "camera-cut";
        case TemporalHistoryResetReason::InvalidInput: return "invalid-input";
        case TemporalHistoryResetReason::ExplicitReset: return "explicit-reset";
        default: return "unknown";
        }
    }

    TemporalFrameState BeginTemporalFrame(const TemporalFrameDesc& desc) {
        TemporalFrameStateStore& store = Store();

        TemporalFrameState next{};
        next.frameIndex = desc.frameIndex;
        next.renderWidth = (std::max)(1u, desc.renderWidth);
        next.renderHeight = (std::max)(1u, desc.renderHeight);
        next.outputWidth = (std::max)(1u, desc.outputWidth);
        next.outputHeight = (std::max)(1u, desc.outputHeight);
        next.resetReason = ResolveResetReason(desc, store);
        next.resetHistory = next.resetReason != TemporalHistoryResetReason::None;
        next.historyValid = !next.resetHistory && store.hasPreviousCamera;
        next.temporalResolveAllowed =
            desc.temporalResolveAllowed &&
            !desc.forceHistoryReset &&
            next.resetReason != TemporalHistoryResetReason::InvalidInput;
        next.jitterEnabled = desc.jitterEnabled && next.temporalResolveAllowed;
        next.jitterPhase = static_cast<uint32_t>(desc.frameIndex & 0xffffu);
        const MATH::Vec4 jitter = MakeJitterParams(
            next.jitterPhase,
            next.renderWidth,
            next.renderHeight,
            next.jitterEnabled);

        const bool previousCameraValid =
            next.historyValid && store.previousCamera.valid;
        if (desc.camera != nullptr) {
            next.camera = BuildCameraData(
                *desc.camera,
                &store.previousCamera,
                previousCameraValid,
                next.renderWidth,
                next.renderHeight,
                next.outputWidth,
                next.outputHeight,
                jitter);
        }

        if (next.camera.valid) {
            store.previousCamera = next.camera;
            store.previousRenderWidth = next.renderWidth;
            store.previousRenderHeight = next.renderHeight;
            store.previousOutputWidth = next.outputWidth;
            store.previousOutputHeight = next.outputHeight;
            store.hasPreviousCamera = true;
        }
        store.pendingExplicitReset = false;
        store.pendingResetReason = TemporalHistoryResetReason::None;
        store.current = next;
        return store.current;
    }

    const TemporalFrameState& GetCurrentTemporalFrameState() {
        return Store().current;
    }

    void ResetTemporalFrameHistory(TemporalHistoryResetReason reason) {
        TemporalFrameStateStore& store = Store();
        store.pendingExplicitReset = true;
        store.pendingResetReason =
            reason == TemporalHistoryResetReason::None
                ? TemporalHistoryResetReason::ExplicitReset
                : reason;
    }

} // namespace HIKARI::RENDER3D::TEMPORAL
