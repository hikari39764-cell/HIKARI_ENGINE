#pragma once

#include "HIKARI/Project/FluidTypes.h"
#include "HIKARI/Project/GameProjectSdk.h"

namespace HIKARI {

class WorldView;

// Retainable, non-owning World identity; all calls require its owner thread.
// Pushes VFX fluid smoke (fluid layers) with obstacles, impulses and forces. Submit from IGameSystem::PreRender,
// after the frame's motion is final (it also runs in the editing/paused viewport). Each (owner, nonzero instanceId)
// is one interaction; Submit creates or replaces it for THIS frame only and omission on the next frame expires it.
// A deleted or SuspendedHidden owner, rebuilt/destroyed World or disabled system cannot leave an interaction behind.
// Authored obstacles on objects and bones use FluidObstacleComponent instead; no call persists a Scene document.
class FluidView {
public:
    FluidView() = default;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    Submit(GameProjectObjectHandle owner, uint64_t instanceId,
           const GameProjectFluidInteraction& interaction) const noexcept;
    // Idempotent for an existing World/object, including a missing submission.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Remove(GameProjectObjectHandle owner,
                                                              uint64_t instanceId) const noexcept;

private:
    friend class WorldView;
    explicit FluidView(GameProjectWorldHandle world) noexcept : world_(world) {}
    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
