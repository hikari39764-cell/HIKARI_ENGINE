#pragma once

#include <cstdint>

#include <d3d12.h>

#include "Render3D/HIKARI_Math3D.h"

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

    struct TemporalTextureView {
        ID3D12Resource* resource = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE srv{};
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        uint32_t width = 0;
        uint32_t height = 0;
        D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
        bool valid = false;
    };

    struct TemporalInputs {
        TemporalFrameState frame{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneDepthSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv{};
        bool hasSceneDepth = false;
        bool hasSceneColor = false;

        TemporalTextureView sceneDepth{};
        TemporalTextureView sceneColor{};
        // Dense, unjittered current-to-previous motion in render pixels:
        // previousUv = currentUv + motionPixels / renderSize.
        TemporalTextureView motionVectors{};
        TemporalTextureView motionMetadata{};
        TemporalTextureView historyColorRead{};
        TemporalTextureView historyColorWrite{};
        TemporalTextureView historyDepthRead{};
        TemporalTextureView historyDepthWrite{};
        TemporalTextureView exposure{};
        TemporalTextureView reactiveMask{};
        TemporalTextureView transparencyMask{};
        TemporalTextureView invalidDepthMotionMask{};
        TemporalTextureView debugOutput{};
    };

    const char* ToString(TemporalHistoryResetReason reason);

    TemporalFrameState BeginTemporalFrame(const TemporalFrameDesc& desc);
    const TemporalFrameState& GetCurrentTemporalFrameState();
    void ResetTemporalFrameHistory(TemporalHistoryResetReason reason);

} // namespace HIKARI::RENDER3D::TEMPORAL
