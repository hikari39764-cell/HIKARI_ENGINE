#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

#include <algorithm>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Upscaling/HIKARI_StreamlineInternal.h"

namespace HIKARI::RENDER3D::UPSCALING {

#if defined(HIKARI_WITH_STREAMLINE)
    namespace {
        sl::float4x4 ToStreamlineMatrix(const MATH::Mat4& matrix) {
            // HIKARI stores column-vector matrices as m[column][row]. Streamline
            // consumes row-vector matrices, so this copy provides the transpose.
            sl::float4x4 result{};
            for (uint32_t row = 0; row < 4; ++row) {
                result[row] = sl::float4(
                    matrix.m[row][0],
                    matrix.m[row][1],
                    matrix.m[row][2],
                    matrix.m[row][3]);
            }
            return result;
        }

        sl::Constants BuildConstants(const TEMPORAL::TemporalFrameState& frame) {
            sl::Constants constants{};
            const TEMPORAL::TemporalCameraData& camera = frame.camera;
            constants.cameraViewToClip = ToStreamlineMatrix(camera.proj);
            constants.clipToCameraView =
                ToStreamlineMatrix(MATH::Inverse(camera.proj));

            const MATH::Mat4 clipToPrev =
                camera.previousValid && !frame.resetHistory
                    ? camera.prevUnjitteredViewProj * camera.invUnjitteredViewProj
                    : MATH::Mat4::Identity();
            constants.clipToPrevClip = ToStreamlineMatrix(clipToPrev);
            constants.prevClipToClip =
                ToStreamlineMatrix(MATH::Inverse(clipToPrev));

            constants.jitterOffset = sl::float2(camera.jitter.x, camera.jitter.y);
            constants.mvecScale = sl::float2(
                1.0f / static_cast<float>((std::max)(1u, frame.renderWidth)),
                1.0f / static_cast<float>((std::max)(1u, frame.renderHeight)));
            constants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);

            const MATH::Mat4 invView = MATH::Inverse(camera.view);
            const MATH::Vec3 right = MATH::Normalize({
                invView.m[0][0], invView.m[0][1], invView.m[0][2] });
            const MATH::Vec3 up = MATH::Normalize({
                invView.m[1][0], invView.m[1][1], invView.m[1][2] });
            const MATH::Vec3 forward = MATH::Normalize({
                -invView.m[2][0], -invView.m[2][1], -invView.m[2][2] });
            constants.cameraPos = sl::float3(
                camera.cameraPos.x,
                camera.cameraPos.y,
                camera.cameraPos.z);
            constants.cameraRight = sl::float3(right.x, right.y, right.z);
            constants.cameraUp = sl::float3(up.x, up.y, up.z);
            constants.cameraFwd = sl::float3(forward.x, forward.y, forward.z);
            constants.cameraNear = camera.nearZ;
            constants.cameraFar = camera.farZ;
            constants.cameraFOV = camera.fovYRad;
            constants.cameraAspectRatio = camera.aspect;
            constants.depthInverted = sl::Boolean::eFalse;
            constants.cameraMotionIncluded = sl::Boolean::eTrue;
            constants.motionVectors3D = sl::Boolean::eFalse;
            constants.reset = frame.resetHistory
                ? sl::Boolean::eTrue
                : sl::Boolean::eFalse;
            constants.orthographicProjection = sl::Boolean::eFalse;
            constants.motionVectorsDilated = sl::Boolean::eFalse;
            constants.motionVectorsJittered = sl::Boolean::eFalse;
            return constants;
        }
    }
#endif

    bool BeginStreamlineFrame(uint64_t frameIndex) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        INTERNAL::ResetFrameStats(state, frameIndex);
        state.reflex.stats.frameIndex = frameIndex;
        state.reflex.stats.sleepCalled = false;
        state.reflex.stats.markerMask = 0;
        state.frameGeneration.stats.frameIndex = frameIndex;
        state.frameGeneration.stats.requested =
            state.frameGeneration.settings.enabled;
        state.frameGeneration.stats.inputsReady = false;
        state.frameGeneration.stats.tagsSubmitted = false;
#if !defined(HIKARI_WITH_STREAMLINE)
        return false;
#else
        state.frameToken = nullptr;
        if (!state.stats.initialized || !state.stats.deviceAttached) {
            return false;
        }

        const uint32_t tokenIndex = static_cast<uint32_t>(frameIndex);
        if (!INTERNAL::RecordResult(
                state,
                "slGetNewFrameToken",
                slGetNewFrameToken(state.frameToken, &tokenIndex)) ||
            state.frameToken == nullptr) {
            return false;
        }
        state.stats.frameTokenReady = true;
        return true;
#endif
    }

    bool SubmitStreamlineFrameConstants(
        const TEMPORAL::TemporalFrameState& frame,
        StreamlineDlssMode mode) {
        INTERNAL::StreamlineState& state = INTERNAL::GetState();
        state.stats.mode = mode;
        state.stats.dlssRequested = mode != StreamlineDlssMode::Off;
        state.stats.renderWidth = frame.renderWidth;
        state.stats.renderHeight = frame.renderHeight;
        state.stats.outputWidth = frame.outputWidth;
        state.stats.outputHeight = frame.outputHeight;
#if !defined(HIKARI_WITH_STREAMLINE)
        return false;
#else
        if (state.frameToken == nullptr ||
            state.stats.frameIndex != frame.frameIndex ||
            !frame.camera.valid) {
            return false;
        }

        const sl::Constants constants = BuildConstants(frame);
        if (!INTERNAL::RecordResult(
                state,
                "slSetConstants",
                slSetConstants(constants, *state.frameToken, state.viewport))) {
            return false;
        }
        state.stats.constantsSubmitted = true;
        if (!state.stats.retryPending) {
            state.stats.status = StreamlineRuntimeStatus::Ready;
        }
        return true;
#endif
    }

} // namespace HIKARI::RENDER3D::UPSCALING
