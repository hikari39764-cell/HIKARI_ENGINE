#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/CameraLensTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectCameraViewSnapshotVersion = 1u;
inline constexpr uint32_t kGameProjectCameraViewSnapshotV1Size = 216u;
inline constexpr uint32_t kGameProjectWorldToScreenResultVersion = 1u;
inline constexpr uint32_t kGameProjectWorldToScreenResultV1Size = 40u;

enum class GameProjectPrimaryViewSource : uint32_t {
    Game = 0u,
    EditorScene = 1u,
    EditorCameraPreview = 2u,
    EditorSequencePreview = 3u,
};

enum class GameProjectPresentationTarget : uint32_t {
    BackBuffer = 0u,
    EditorViewport = 1u,
};

enum class GameProjectScreenCoordinateSpace : uint32_t {
    PhysicalOutputPixels = 0u,
    UiLogicalPixels = 1u,
};

// Top-left-origin pixel-edge rectangle. right/bottom are exclusive edges.
struct GameProjectScreenRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

// Camera position and basis from the rendered frame. FOV is owned by lens and
// aspect ratio by the snapshot so neither value can disagree across records.
struct GameProjectCameraViewPose {
    GameProjectFloat3 eye{};
    GameProjectFloat3 target{};
    GameProjectFloat3 up{0.0f, 1.0f, 0.0f};
};

// Latest successfully presented primary view. Every field describes the same
// frame; dimensions are in pixels and its scene rectangles exclude letterbox.
struct GameProjectCameraViewSnapshot {
    uint32_t structSize = kGameProjectCameraViewSnapshotV1Size;
    uint32_t version = kGameProjectCameraViewSnapshotVersion;
    uint64_t frameIndex = 0u;
    uint64_t worldIdentity = 0u;
    uint64_t primaryViewId = 0u;
    GameProjectPrimaryViewSource source = GameProjectPrimaryViewSource::Game;
    GameProjectPresentationTarget presentationTarget = GameProjectPresentationTarget::BackBuffer;
    GameProjectCameraLens lens{};
    GameProjectCameraViewPose pose{};
    float aspectRatio = 1.0f;
    uint32_t sceneRenderWidth = 0u;
    uint32_t sceneRenderHeight = 0u;
    uint32_t outputPhysicalWidth = 0u;
    uint32_t outputPhysicalHeight = 0u;
    uint32_t uiLogicalWidth = 0u;
    uint32_t uiLogicalHeight = 0u;
    GameProjectScreenRect sceneContentPhysical{};
    GameProjectScreenRect sceneContentLogical{};
};

enum class GameProjectWorldToScreenFlag : uint32_t {
    InFront = 1u << 0u,
    WithinDepthRange = 1u << 1u,
    InsideSceneContent = 1u << 2u,
};

constexpr GameProjectWorldToScreenFlag operator|(GameProjectWorldToScreenFlag lhs,
                                                 GameProjectWorldToScreenFlag rhs) noexcept {
    return static_cast<GameProjectWorldToScreenFlag>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr uint32_t GameProjectWorldToScreenFlags(GameProjectWorldToScreenFlag flags) noexcept {
    return static_cast<uint32_t>(flags);
}

struct GameProjectWorldToScreenResult {
    uint32_t structSize = kGameProjectWorldToScreenResultV1Size;
    uint32_t version = kGameProjectWorldToScreenResultVersion;
    uint64_t frameIndex = 0u;
    GameProjectScreenCoordinateSpace coordinateSpace = GameProjectScreenCoordinateSpace::PhysicalOutputPixels;
    uint32_t flags = 0u;
    float x = 0.0f;
    float y = 0.0f;
    float viewDepth = 0.0f;
    uint32_t reserved = 0u;
};

// Pure CPU projection using a completed-view snapshot. Coordinates originate
// at the selected presentation's top-left; a point outside the depth/scene
// range is still projected when mathematically finite and reported in flags.
HIKARI_GAME_PROJECT_SDK_API bool ProjectWorldToScreen(const GameProjectCameraViewSnapshot& snapshot,
                                                      GameProjectFloat3 worldPosition,
                                                      GameProjectScreenCoordinateSpace coordinateSpace,
                                                      GameProjectWorldToScreenResult& outResult) noexcept;

static_assert(std::is_standard_layout_v<GameProjectScreenRect>);
static_assert(std::is_trivially_copyable_v<GameProjectScreenRect>);
static_assert(sizeof(GameProjectCameraViewPose) == 36u);
static_assert(std::is_standard_layout_v<GameProjectCameraViewPose>);
static_assert(std::is_trivially_copyable_v<GameProjectCameraViewPose>);
static_assert(std::is_standard_layout_v<GameProjectCameraViewSnapshot>);
static_assert(std::is_trivially_copyable_v<GameProjectCameraViewSnapshot>);
static_assert(sizeof(GameProjectCameraViewSnapshot) == kGameProjectCameraViewSnapshotV1Size);
static_assert(offsetof(GameProjectCameraViewSnapshot, lens) == 40u);
static_assert(offsetof(GameProjectCameraViewSnapshot, pose) == 120u);
static_assert(offsetof(GameProjectCameraViewSnapshot, aspectRatio) == 156u);
static_assert(offsetof(GameProjectCameraViewSnapshot, sceneContentPhysical) == 184u);
static_assert(std::is_standard_layout_v<GameProjectWorldToScreenResult>);
static_assert(std::is_trivially_copyable_v<GameProjectWorldToScreenResult>);
static_assert(sizeof(GameProjectWorldToScreenResult) == kGameProjectWorldToScreenResultV1Size);
static_assert(offsetof(GameProjectWorldToScreenResult, x) == 24u);
static_assert(offsetof(GameProjectWorldToScreenResult, reserved) == 36u);

} // namespace HIKARI
