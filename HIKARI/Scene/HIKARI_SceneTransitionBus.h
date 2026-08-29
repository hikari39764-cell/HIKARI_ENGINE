#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace HIKARI {

    struct TransitionVisualState {
        bool active = false;
        std::string profileId{};
        float progress = 0.0f;
        bool isTransitionIn = false;
        float outDuration = 0.0f; // 追加: outDuration メンバー
        float inDuration = 0.0f;  // 追加: inDuration メンバー（必要に応じて）
    };

    class SceneManager;

    struct SceneTransitionRequest {
        std::string targetSceneAssetGuid{};
        std::string targetSpawnPointId{};
        std::string transitionProfileId{ "Default" };
        bool useTransition = true;
        bool preserveGameplayState = false;
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
        bool ReleaseTransitionIn();
        void Update(float dt);
        bool IsTransitioning() const;
        TransitionState GetState() const;
        bool WasLastSceneLoadedFromCache() const;
        size_t GetCachedSceneCount() const;
        std::vector<std::string> GetCachedSceneIds() const;
        TransitionVisualState GetVisualState() const;

    private:
        SceneManager& sceneManager_;
        std::optional<SceneTransitionRequest> pendingRequest_{};
        TransitionState state_ = TransitionState::Idle;
        float timer_ = 0.0f;
        float transitionOutDuration_ = 0.2f;
        float transitionInDuration_ = 0.2f;
        float cameraReadyWaitSeconds_ = 0.0f;
        float cameraReadyTimeout_ = 1.25f;
        bool waitForCameraReady_ = false;
        bool transitionInReleased_ = true;
        bool lastSceneLoadedFromCache_ = false;
        std::string activeTransitionProfileId_{ "noise_wipe" };
    };

} // namespace HIKARI
