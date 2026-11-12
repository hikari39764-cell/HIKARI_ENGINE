#pragma once

#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectFluidApiVersion = 1u;
// Per World and frame. Each fluid smoke domain uses at most 32 of the ones that reach it.
inline constexpr uint32_t kGameProjectFluidMaximumInteractions = 64u;
// Channels separate what reaches which fluid domain. A domain listens on one channel (set on the VFX fluid domain
// layer: Default, Channel 1-7, All or None); a submission goes to the set of channels in its `channels` mask (bit i =
// channel i, bit 0 = Default) and reaches a domain when the two overlap. Use them to keep overlapping domains apart,
// for example fuel for a fire domain that shares the arena with a long-lived fog domain.
inline constexpr uint32_t kGameProjectFluidDefaultChannel = 1u;
inline constexpr uint32_t kGameProjectFluidAllChannels = 0xffu;

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
    // radius at `strength` m/s (open-boundary domains only; an incompressible closed box cannot expand). A sink
    // removes smoke only as fast as it draws volume in, and smoke flowing in keeps its density, so a sink inside thick
    // smoke never thins it; `absorption` fades the smoke inside the radius directly. FluidView::GetRemovedSmoke
    // reports how much a sink or absorbing Force removed.
    Force = 2u,
    // Adds smoke while submitted: `strength` density per second of game time at the centre (0–10000, squared
    // falloff to zero at the radius extents.x), so one frame adds strength × that frame's game time. `direction` is
    // the velocity the new smoke is pushed towards in m/s (zero leaves the flow alone) and extents.y its temperature
    // (heat makes it rise and glow, as for authored fluid sources; 0 for cold smoke). Independent of the per-domain
    // limit on authored fluid sources, so bullets, trails and impact puffs can emit anywhere each frame. Each frame's
    // amount is kept even when the simulation runs no substep in that frame (it goes into the next substeps), and a moving
    // Emit (the same (owner, instanceId) as the previous frame) is spread along the segment it moved since then, so a fast
    // trail stays continuous; a move faster than 60 m/s within one frame counts as a teleport and is not spread.
    Emit = 3u,
    // Adds fuel instead of smoke, otherwise exactly like Emit (amount, falloff, velocity, extents.y temperature,
    // spreading along the moved segment). Only domains with combustion receive it; others ignore it. Fuel burns where
    // the temperature is above the ignition temperature of the domain, so give it a temperature above that to light
    // it, or add it cold and heat it later with an Emit (a detonation). A sink or absorbing Force removes fuel like
    // smoke.
    Fuel = 4u,
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
    // Impulse / Force / Emit / Fuel: x radius of influence, strongest at the centre and zero at the radius (squared
    // falloff); Emit / Fuel: y temperature. x is 0.001–1000 m, y and z 0–1000 m.
    GameProjectFloat3 extents{0.5f, 0.5f, 0.5f};
    // Impulse / Force: world direction; zero expands from position (see the kinds). Emit / Fuel: world velocity
    // in m/s.
    GameProjectFloat3 direction{};
    float strength = 0.0f; // see the kinds; within ±10000
    // Force only (0 for the other kinds): smoke, heat and fuel inside the radius fade at this rate per second
    // (0–1000 1/s, strongest at the centre, squared falloff), with or without a direction. 0 leaves smoke alone.
    float absorption = 0.0f;
    // Channels this submission reaches (bits within kGameProjectFluidAllChannels). 0 means the Default channel.
    uint32_t channels = 0u;
};

static_assert(sizeof(GameProjectFluidInteraction) == 80u);
static_assert(std::is_trivially_copyable_v<GameProjectFluidInteraction>);

} // namespace HIKARI
