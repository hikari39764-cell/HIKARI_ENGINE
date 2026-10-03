#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ActionTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectDepthOfFieldSettingsVersion = 1u;
inline constexpr uint32_t kGameProjectDepthOfFieldSettingsV1Size = 36u;
inline constexpr uint32_t kGameProjectCameraLensVersion = 1u;
inline constexpr uint32_t kGameProjectCameraLensV1Size = 80u;
inline constexpr uint32_t kGameProjectCameraDepthOfFieldRequestVersion = 1u;
inline constexpr uint32_t kGameProjectCameraDepthOfFieldRequestV1Size = 72u;
inline constexpr uint32_t kGameProjectCameraFocusRequestVersion = 1u;
inline constexpr uint32_t kGameProjectCameraFocusRequestV1Size = 80u;

enum class GameProjectCameraProjectionKind : uint32_t {
    Perspective = 0u,
    Orthographic = 1u,
};

enum class GameProjectCameraLensStage : uint32_t {
    Authored = 0u,
    Control = 1u,
    Rendered = 2u,
};

// Authored optical depth-of-field values. Distances and transitions use world
// units measured along camera view-Z. Blur radii are reference pixels at 1080p.
struct GameProjectDepthOfFieldSettings {
    uint32_t structSize = kGameProjectDepthOfFieldSettingsV1Size;
    uint32_t version = kGameProjectDepthOfFieldSettingsVersion;
    uint8_t enabled = 0u;
    uint8_t reserved[3] = {0u, 0u, 0u};
    float focusDistance = 10.0f;
    float focusRange = 2.0f;
    float nearTransition = 5.0f;
    float farTransition = 20.0f;
    float nearBlurRadius1080pPixels = 12.0f;
    float farBlurRadius1080pPixels = 4.0f;
};

struct GameProjectCameraLens {
    uint32_t structSize = kGameProjectCameraLensV1Size;
    uint32_t version = kGameProjectCameraLensVersion;
    // Runtime object handle of the source camera, or zero when the primary
    // view has no scene camera object.
    uint64_t sourceCameraObjectHandle = 0u;
    GameProjectCameraLensStage stage = GameProjectCameraLensStage::Authored;
    GameProjectCameraProjectionKind projection = GameProjectCameraProjectionKind::Perspective;
    float verticalFovRadians = 1.04719755f;
    float orthographicHeight = 10.0f;
    float nearClip = 0.1f;
    float farClip = 2000.0f;
    GameProjectDepthOfFieldSettings depthOfField{};
    uint32_t reserved = 0u;
};

// A full per-frame depth-of-field replacement. It never changes pose or FOV.
// cameraObjectHandle=0 targets the
// currently resolved primary camera in this World. Weight 0 is ignored by
// arbitration so it cannot suppress authored values or a lower-priority source.
struct GameProjectCameraDepthOfFieldRequest {
    uint32_t structSize = kGameProjectCameraDepthOfFieldRequestV1Size;
    uint32_t version = kGameProjectCameraDepthOfFieldRequestVersion;
    uint64_t cameraObjectHandle = 0u;
    uint64_t sourceId = 0u;
    int32_t priority = 0;
    float weight = 1.0f;
    GameProjectDepthOfFieldSettings settings{};
    uint32_t reserved = 0u;
};

enum class GameProjectCameraFocusMode : uint32_t {
    Distance = 0u,
    WorldPoint = 1u,
    WorldPlane = 2u,
};

// Per-frame focus-only request. A distance is axial view-Z, not eye-to-target
// Euclidean distance. WorldPoint uses the point's projected view-Z; WorldPlane
// intersects the final camera-forward ray with the plane through planePoint
// having planeNormal. Unused payload fields are ignored for the selected mode.
struct GameProjectCameraFocusRequest {
    uint32_t structSize = kGameProjectCameraFocusRequestV1Size;
    uint32_t version = kGameProjectCameraFocusRequestVersion;
    uint64_t cameraObjectHandle = 0u;
    uint64_t sourceId = 0u;
    int32_t priority = 0;
    GameProjectCameraFocusMode mode = GameProjectCameraFocusMode::Distance;
    float weight = 1.0f;
    float distance = 10.0f;
    GameProjectFloat3 worldPoint{};
    GameProjectFloat3 planePoint{};
    GameProjectFloat3 planeNormal{0.0f, 0.0f, 1.0f};
    uint32_t reserved = 0u;
};

static_assert(std::is_standard_layout_v<GameProjectDepthOfFieldSettings>);
static_assert(std::is_trivially_copyable_v<GameProjectDepthOfFieldSettings>);
static_assert(sizeof(GameProjectDepthOfFieldSettings) == kGameProjectDepthOfFieldSettingsV1Size);
static_assert(offsetof(GameProjectDepthOfFieldSettings, focusDistance) == 12u);
static_assert(std::is_standard_layout_v<GameProjectCameraLens>);
static_assert(std::is_trivially_copyable_v<GameProjectCameraLens>);
static_assert(sizeof(GameProjectCameraLens) == kGameProjectCameraLensV1Size);
static_assert(offsetof(GameProjectCameraLens, verticalFovRadians) == 24u);
static_assert(offsetof(GameProjectCameraLens, depthOfField) == 40u);
static_assert(offsetof(GameProjectCameraLens, reserved) == 76u);
static_assert(std::is_standard_layout_v<GameProjectCameraDepthOfFieldRequest>);
static_assert(std::is_trivially_copyable_v<GameProjectCameraDepthOfFieldRequest>);
static_assert(sizeof(GameProjectCameraDepthOfFieldRequest) == kGameProjectCameraDepthOfFieldRequestV1Size);
static_assert(offsetof(GameProjectCameraDepthOfFieldRequest, settings) == 32u);
static_assert(std::is_standard_layout_v<GameProjectCameraFocusRequest>);
static_assert(std::is_trivially_copyable_v<GameProjectCameraFocusRequest>);
static_assert(sizeof(GameProjectCameraFocusRequest) == kGameProjectCameraFocusRequestV1Size);
static_assert(offsetof(GameProjectCameraFocusRequest, worldPoint) == 40u);

} // namespace HIKARI
