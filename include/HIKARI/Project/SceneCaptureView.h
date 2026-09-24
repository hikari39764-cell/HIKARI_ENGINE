#pragma once

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/SceneCaptureTypes.h"

namespace HIKARI {

struct GameProjectSdkAccess;

// Retainable non-owning World capability. All calls are owner-thread only.
// Requests survive until Destroy or World teardown. Published screen
// content is one frame behind its secondary render so a captured view can
// never sample a target while it is being written. Up to
// kGameProjectSceneCaptureMaximumActiveCount independent captures may be
// active at once.
// Disabling or a transient render failure freezes the last good frame;
// Destroy retires the material binding and restores the authored material.
class SceneCaptureView {
public:
    SceneCaptureView() = default;
    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;

    HIKARI_GAME_PROJECT_SDK_API GameProjectSceneCaptureHandle
    Create(const GameProjectSceneCaptureDesc& desc) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Update(GameProjectSceneCaptureHandle capture,
                                            const GameProjectSceneCaptureDesc& desc) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool Destroy(GameProjectSceneCaptureHandle capture) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool BindMaterial(
        const GameProjectSceneCaptureMaterialBindingDesc& binding) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool BindMaterialRegion(
        const GameProjectSceneCaptureMaterialRegionBindingDesc& binding) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ClearMaterialBinding(GameProjectSceneCaptureHandle capture,
                                                          GameProjectObjectHandle modelObject) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool GetSnapshot(GameProjectSceneCaptureHandle capture,
                                                 GameProjectSceneCaptureSnapshot& outSnapshot) const noexcept;

private:
    friend struct GameProjectSdkAccess;
    explicit SceneCaptureView(GameProjectWorldHandle world) noexcept : world_(world) {}
    GameProjectWorldHandle world_{};
};

} // namespace HIKARI
