#pragma once

#include <cstdint>

#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI {
    class Camera3D;
}

namespace HIKARI::RENDER3D::TEMPORAL {

    enum class TemporalHistoryResetReason : uint32_t {
        None = 0,
        FirstFrame,
        Resize,
        CameraCut,
        InvalidInput,
        ExplicitReset,
    };

    struct TemporalCameraData {
        MATH::Mat4 view{};
        MATH::Mat4 proj{};
        MATH::Mat4 viewProj{};
        MATH::Mat4 invViewProj{};

        MATH::Mat4 unjitteredViewProj{};
        MATH::Mat4 invUnjitteredViewProj{};
        MATH::Mat4 prevViewProj{};
        MATH::Mat4 prevUnjitteredViewProj{};
        MATH::Mat4 clipToPrevClip{};

        MATH::Vec4 cameraPos{};
        MATH::Vec4 prevCameraPos{};
        MATH::Vec4 jitter{};     // xy=current pixels, zw=current NDC
        MATH::Vec4 prevJitter{}; // xy=previous pixels, zw=previous NDC
        MATH::Vec4 screenParams{};
        MATH::Vec4 outputParams{};

        float nearZ = 0.0f;
        float farZ = 0.0f;
        float fovYRad = 0.0f;
        float aspect = 1.0f;

        bool valid = false;
        bool previousValid = false;
        bool jittered = false;
    };

    struct TemporalFrameState {
        uint64_t frameIndex = 0;
        uint32_t renderWidth = 1;
        uint32_t renderHeight = 1;
        uint32_t outputWidth = 1;
        uint32_t outputHeight = 1;

        bool historyValid = false;
        bool resetHistory = true;
        bool temporalResolveAllowed = true;
        bool jitterEnabled = false;
        uint32_t jitterPhase = 0;
        TemporalHistoryResetReason resetReason = TemporalHistoryResetReason::FirstFrame;
        TemporalCameraData camera{};
    };

    struct TemporalFrameDesc {
        const Camera3D* camera = nullptr;
        uint64_t frameIndex = 0;
        uint32_t renderWidth = 1;
        uint32_t renderHeight = 1;
        uint32_t outputWidth = 1;
        uint32_t outputHeight = 1;
        bool cameraCut = false;
        bool forceHistoryReset = false;
        bool temporalResolveAllowed = true;
        bool jitterEnabled = false;
    };

    struct TemporalInputs {
        TemporalFrameState frame{};
        COLOR::ExposureFrameState exposureState{};
        RenderTextureView sceneDepth{};
        RenderTextureView sceneColor{};
        // Dense, unjittered current-to-previous motion in render pixels:
        // previousUv = currentUv + motionPixels / renderSize.
        RenderTextureView motionVectors{};
        RenderTextureView motionMetadata{};
        RenderTextureView historyColorRead{};
        RenderTextureView historyColorWrite{};
        RenderTextureView historyDepthRead{};
        RenderTextureView historyDepthWrite{};
        RenderTextureView exposure{};
        RenderTextureView reactiveMask{};
        RenderTextureView transparencyMask{};
        RenderTextureView invalidDepthMotionMask{};
        RenderTextureView debugOutput{};
    };

    const char* ToString(TemporalHistoryResetReason reason);

    TemporalFrameState BeginTemporalFrame(const TemporalFrameDesc& desc);
    const TemporalFrameState& GetCurrentTemporalFrameState();
    void ResetTemporalFrameHistory(TemporalHistoryResetReason reason);

} // namespace HIKARI::RENDER3D::TEMPORAL
