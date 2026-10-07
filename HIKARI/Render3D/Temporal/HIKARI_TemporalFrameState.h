#pragma once

#include <cstdint>
#include <type_traits>

#include "Render3D/Color/HIKARI_ExposureState.h"
#include "Render3D/Core/HIKARI_RenderFrameToken.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

namespace HIKARI::RENDER3D::TEMPORAL {

inline constexpr uint32_t kTemporalMaximumViewCount = 16u;

struct TemporalViewLease {
    RenderViewId viewId{};
    uint64_t generation = 0;

    bool IsValid() const noexcept { return viewId.value != 0u && generation != 0u; }

    bool Matches(TemporalViewLease expected) const noexcept {
        return IsValid() && expected.IsValid() && viewId == expected.viewId && generation == expected.generation;
    }

    bool operator==(const TemporalViewLease&) const noexcept = default;
};

static_assert(std::is_standard_layout_v<TemporalViewLease>);
static_assert(std::is_trivially_copyable_v<TemporalViewLease>);

// Exact receipt for one active GPU Temporal resource publication. The
// generation prevents a released/recreated bundle from accepting stale
// target pointers even when the RenderViewId is reused.
struct TemporalResourceFrame {
    TemporalViewLease view{};
    RenderFrameToken publication{};
    uint64_t resourceGeneration = 0;
    uint64_t deviceGeneration = 0;

    bool IsValid() const noexcept {
        return view.IsValid() && publication.IsValid() && view.viewId == publication.viewId &&
               resourceGeneration != 0u && deviceGeneration != 0u;
    }

    bool Matches(TemporalResourceFrame expected) const noexcept {
        return IsValid() && expected.IsValid() && view.Matches(expected.view) &&
               publication.Matches(expected.publication) && resourceGeneration == expected.resourceGeneration &&
               deviceGeneration == expected.deviceGeneration;
    }
};

static_assert(std::is_standard_layout_v<TemporalResourceFrame>);
static_assert(std::is_trivially_copyable_v<TemporalResourceFrame>);

enum class TemporalHistoryResetReason : uint32_t {
    None = 0,
    FirstFrame = 1,
    Resize = 2,
    CameraCut = 3,
    InvalidInput = 4,
    ExplicitReset = 5,
    ProjectionChanged = 6,
    ConfigurationChanged = 7,
    ViewPurposeChanged = 8,
    CameraSourceChanged = 9,
    FrameDiscontinuity = 10,
    PreviousFrameIncomplete = 11,
    InvalidViewLease = 12,
    InvalidFrameToken = 13,
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
    TemporalViewLease view{};
    RenderFrameToken publication{};
    RenderViewPurpose purpose = RenderViewPurpose::Game;
    uint64_t cameraSourceObjectId = 0;
    uint64_t cameraRevision = 0;
    uint64_t historyConfigurationKey = 0;
    uint64_t frameIndex = 0;
    uint32_t renderWidth = 1;
    uint32_t renderHeight = 1;
    uint32_t outputWidth = 1;
    uint32_t outputHeight = 1;

    bool historyValid = false;
    bool resetHistory = true;
    bool completed = false;
    bool aborted = false;
    bool temporalResolveAllowed = true;
    bool jitterEnabled = false;
    uint32_t jitterPhase = 0;
    TemporalHistoryResetReason resetReason = TemporalHistoryResetReason::FirstFrame;
    TemporalCameraData camera{};
};

static_assert(std::is_standard_layout_v<TemporalFrameState>);
static_assert(std::is_trivially_copyable_v<TemporalFrameState>);

struct TemporalFrameDesc {
    TemporalViewLease view{};
    RenderFrameToken frame{};
    RenderViewPurpose purpose = RenderViewPurpose::Game;
    const Camera3D* camera = nullptr;
    uint64_t cameraSourceObjectId = 0;
    uint64_t cameraRevision = 0;
    uint32_t renderWidth = 1;
    uint32_t renderHeight = 1;
    uint32_t outputWidth = 1;
    uint32_t outputHeight = 1;
    bool cameraCut = false;
    bool projectionChanged = false;
    bool forceHistoryReset = false;
    bool temporalResolveAllowed = true;
    bool jitterEnabled = false;
    // Stable key for inputs that change the meaning or layout of history.
    // The default pipeline currently includes AA mode and DLSS quality.
    uint64_t historyConfigurationKey = 0;
};

static_assert(std::is_standard_layout_v<TemporalFrameDesc>);
static_assert(std::is_trivially_copyable_v<TemporalFrameDesc>);

struct TemporalInputs {
    TemporalResourceFrame resourceFrame{};
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

    bool MatchesResourceFrame() const noexcept {
        return resourceFrame.IsValid() && frame.view.Matches(resourceFrame.view) &&
               frame.publication.Matches(resourceFrame.publication);
    }
};

const char* ToString(TemporalHistoryResetReason reason);

bool AcquireTemporalView(RenderViewId viewId, TemporalViewLease& outLease);
// CPU-state primitive used by the Temporal resource-domain coordinator.
// Callers that may own GPU Temporal resources must use the combined
// ReleaseTemporalView entry in HIKARI_TemporalResourceSystem.h.
bool CanReleaseTemporalFrameViewState(TemporalViewLease lease);
bool ReleaseTemporalFrameViewState(TemporalViewLease lease);
TemporalFrameState BeginTemporalFrame(const TemporalFrameDesc& desc);
TemporalFrameState GetLatestTemporalFrameState(RenderViewId viewId);
bool TryGetTemporalFrameState(TemporalViewLease lease, RenderFrameToken expected, TemporalFrameState& outState);
bool ResetTemporalFrameHistory(TemporalViewLease lease, TemporalHistoryResetReason reason);
bool CompleteTemporalFrame(TemporalViewLease lease, RenderFrameToken frame, bool historyWritten);
bool AbortTemporalFrame(TemporalViewLease lease, RenderFrameToken frame, TemporalHistoryResetReason reason);
void ShutdownTemporalFrameStateSystem();
uint32_t GetTemporalFrameStateViewCount();

} // namespace HIKARI::RENDER3D::TEMPORAL
