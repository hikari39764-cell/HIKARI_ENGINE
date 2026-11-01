#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "Vfx/Transition/HIKARI_TransitionVisualState.h"

namespace HIKARI {

class SceneManager;

enum class SceneTransitionOutcome : uint32_t {
    Pending = 1,
    Completed,
    Failed,
    Cancelled,
};

struct SceneTransitionResult {
    uint64_t requestId = 0;
    uint64_t userData = 0;
    SceneTransitionOutcome outcome = SceneTransitionOutcome::Pending;
    std::string targetSceneAssetGuid;
    std::string message;
};

struct SceneTransitionRequest {
    std::string targetSceneAssetGuid{};
    std::string targetSpawnPointId{};
    std::string transitionProfileId{"noise_wipe"};
    bool useTransition = true;
    // Keep the destination at the fully covered transition-in boundary
    // until ReleaseTransitionIn() is called. This lets a game project run
    // modal work (for example an opening dialogue) over a stable black
    // frame before the authored transition reveals gameplay.
    bool holdTransitionIn = false;
    uint64_t userData = 0u;
};

class SceneTransitionBus {
public:
    enum class TransitionState {
        Idle,
        TransitionOut,
        SwitchingScene,
        TransitionIn,
    };

    explicit SceneTransitionBus(SceneManager& sceneManager);

    bool RequestTransition(const SceneTransitionRequest& request);
    uint64_t RequestTrackedTransition(const SceneTransitionRequest& request);
    // Retains the most recent eight admitted requests across World switches.
    bool ReadResult(uint64_t requestId, SceneTransitionResult& outResult) const;
    // Cancellation is allowed before the destination has been opened.
    bool CancelRequest(uint64_t requestId);
    bool ReleaseTransitionIn();
    void Update(float dt);
    bool IsTransitioning() const;
    TransitionState GetState() const;
    TransitionVisualState GetVisualState() const;

private:
    void Finish(SceneTransitionOutcome outcome, std::string_view message = {});
    SceneManager& sceneManager_;
    std::array<SceneTransitionResult, 8> results_{};
    uint64_t lastRequestId_ = 0;
    uint64_t activeRequestId_ = 0;
    bool switchInProgress_ = false;
    std::optional<SceneTransitionRequest> pendingRequest_{};
    TransitionState state_ = TransitionState::Idle;
    float timer_ = 0.0f;
    float transitionOutDuration_ = 0.2f;
    float transitionInDuration_ = 0.2f;
    float cameraReadyWaitSeconds_ = 0.0f;
    float cameraReadyTimeout_ = 1.25f;
    bool waitForCameraReady_ = false;
    bool transitionInReleased_ = true;
    std::string activeTransitionProfileId_{"noise_wipe"};
};

} // namespace HIKARI
