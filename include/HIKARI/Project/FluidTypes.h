#pragma once

#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectFluidApiVersion = 1u;
// Per World and frame. Each fluid smoke domain uses at most 32 of the ones that reach it.
inline constexpr uint32_t kGameProjectFluidMaximumInteractions = 64u;

// What a submission does to the VFX fluid smoke layers it reaches.
enum class GameProjectFluidInteractionKind : uint32_t {
    // A solid shape. Smoke flows around it and is pushed and dragged by its motion. Its velocity comes from the
    // previous frame's submission of the same (owner, instanceId), in game time (no rotation velocity); a move
    // faster than 60 m/s within one frame counts as a teleport and gives zero velocity.
    Obstacle = 0u,
    // A one-shot push, submitted once per event (while the game is paused it waits for the next substep).
    // With a direction it adds about `strength` m/s along it once. Without one it is a burst of expansion: smoke
    // flows out through the radius at `strength` m/s for 0.1 s of game time (open-boundary domains only).
    Impulse = 1u,
    // A continuous push while submitted. With a direction it accelerates smoke at `strength` m/s² along it.
    // Without one it is a steady source (positive) or sink (negative): smoke flows out through (or in through) the
    // radius at `strength` m/s (open-boundary domains only; an incompressible closed box cannot expand).
    Force = 2u,
};

// Obstacle shapes. Impulses and forces always act on a sphere of radius extents.x.
enum class GameProjectFluidShape : uint32_t { Sphere = 0u, Capsule = 1u, Box = 2u };

struct GameProjectFluidInteraction {
    uint32_t structSize = sizeof(GameProjectFluidInteraction);
    uint32_t apiVersion = kGameProjectFluidApiVersion;
    GameProjectFluidInteractionKind kind = GameProjectFluidInteractionKind::Obstacle;
    GameProjectFluidShape shape = GameProjectFluidShape::Sphere;
    GameProjectFloat3 position{};     // world metres, each axis within ±1000000
    GameProjectQuaternion rotation{}; // world, non-degenerate (normalized internally); capsule axis is local +Y
    // Sphere: x radius. Capsule: x radius, y half length of the axis segment between the caps. Box: half extents.
    // Impulse / Force: x radius of influence, strongest at the centre and zero at the radius (squared falloff).
    // x is 0.001–1000 m, y and z 0–1000 m.
    GameProjectFloat3 extents{0.5f, 0.5f, 0.5f};
    GameProjectFloat3 direction{}; // Impulse / Force: world direction; zero expands from position (see the kinds)
    float strength = 0.0f;         // see the kinds; within ±10000
    uint32_t reserved[2] = {};
};

static_assert(sizeof(GameProjectFluidInteraction) == 80u);
static_assert(std::is_trivially_copyable_v<GameProjectFluidInteraction>);

} // namespace HIKARI
