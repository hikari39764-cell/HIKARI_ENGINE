#pragma once

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/LocalFogTypes.h"

namespace HIKARI {

class WorldView;

// Retainable, non-owning World identity; all calls require its owner thread.
// Submit from IGameSystem::PreRender (also runs in the editing/paused viewport).
// Each (owner, nonzero instanceId) is an independent box. Submit creates or
// replaces it for THIS frame only; omission on the next frame expires it.
// A deleted owner, rebuilt/destroyed World or disabled system cannot leak fog.
// Project code owns phase progression: do not advance noiseOffset when paused;
// a preview toggle can simply skip Submit. No call persists a Scene document.
class LocalFogView {
public:
    LocalFogView() = default;
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Submit(GameProjectObjectHandle owner, uint64_t instanceId,
                                                              const GameProjectLocalFogVolume& volume) const noexcept;
    // Idempotent for an existing World/object, including a missing submission.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult Remove(GameProjectObjectHandle owner,
                                                              uint64_t instanceId) const noexcept;

private:
    friend class WorldView;
    explicit LocalFogView(GameProjectWorldHandle world) noexcept : world_(world) {}
    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
