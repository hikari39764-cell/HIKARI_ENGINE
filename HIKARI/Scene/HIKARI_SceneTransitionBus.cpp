#include "HIKARI_SceneTransitionBus.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "HIKARI_RuntimeSceneContext.h"
#include "Core/HIKARI_Logger.h"
#include "HIKARI_IScene.h"
#include "HIKARI_SceneManager.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Transition/HIKARI_TransitionProfile.h"

namespace HIKARI {

    SceneTransitionBus::SceneTransitionBus(SceneManager& sceneManager)
        : sceneManager_(sceneManager) {
    }

    bool SceneTransitionBus::RequestTransition(const SceneTransitionRequest& request) {
        return RequestTrackedTransition(request) != 0;
    }

    uint64_t SceneTransitionBus::RequestTrackedTransition(const SceneTransitionRequest& request) {
        World* world = RuntimeSceneContext::GetCurrentWorld();
        if (world == nullptr || !world->IsOwnerThread() ||
            IsTransitioning() ||
            !IsValidAssetGuid(request.targetSceneAssetGuid) ||
            !SERVICES::IsRuntimeSceneGuidAllowed(
                request.targetSceneAssetGuid) ||
            request.targetSpawnPointId.size() >= 64u ||
            request.transitionProfileId.size() > 128u ||
            lastRequestId_ == (std::numeric_limits<uint64_t>::max)()) {
            return 0;
        }
#if defined(HIKARI_WITH_EDITOR)
        const RuntimePlayStateService* playState =
            world->Services().Find<RuntimePlayStateService>();
        if (playState == nullptr || !playState->IsActive()) {
            return 0;
        }
#endif

        pendingRequest_ = request;
        if (pendingRequest_->transitionProfileId.empty()) {
            pendingRequest_->transitionProfileId = "noise_wipe";
        }
        activeTransitionProfileId_ =
            pendingRequest_->transitionProfileId;
        transitionOutDuration_ = 0.2f;
        transitionInDuration_ = 0.2f;
        waitForCameraReady_ = false;
        transitionInReleased_ = !request.holdTransitionIn;
        cameraReadyTimeout_ = 1.25f;
        cameraReadyWaitSeconds_ = 0.0f;
        TransitionProfile profile{};
        if (TransitionProfile::LoadById(
                pendingRequest_->transitionProfileId,
                profile)) {
            transitionOutDuration_ = (std::max)(0.0f, profile.outDuration);
            transitionInDuration_ = (std::max)(0.0f, profile.inDuration);
            waitForCameraReady_ = profile.waitForCameraReady;
            cameraReadyTimeout_ = profile.cameraReadyTimeout;
        }
        state_ = request.useTransition ? TransitionState::TransitionOut : TransitionState::SwitchingScene;
        timer_ = 0.0f;
        activeRequestId_ = ++lastRequestId_;
        results_[(activeRequestId_ - 1) % results_.size()] = {
            activeRequestId_, request.userData, SceneTransitionOutcome::Pending,
            request.targetSceneAssetGuid, {}};
        return activeRequestId_;
    }

    bool SceneTransitionBus::ReadResult(uint64_t requestId, SceneTransitionResult& outResult) const {
        outResult = {};
        if (requestId == 0) requestId = lastRequestId_;
        if (requestId == 0) return false;
        const auto& result = results_[(requestId - 1) % results_.size()];
        if (result.requestId != requestId) return false;
        outResult = result;
        return true;
    }

    void SceneTransitionBus::Finish(SceneTransitionOutcome outcome, std::string_view message) {
        if (activeRequestId_ != 0) {
            auto& result = results_[(activeRequestId_ - 1) % results_.size()];
            result.outcome = outcome;
            result.message = message;
        }
        activeRequestId_ = 0;
        pendingRequest_.reset();
        state_ = TransitionState::Idle;
        timer_ = 0.0f;
        cameraReadyWaitSeconds_ = 0.0f;
        transitionInReleased_ = true;
        switchInProgress_ = false;
    }

    bool SceneTransitionBus::CancelRequest(uint64_t requestId) {
        if (requestId == 0 || requestId != activeRequestId_ ||
            state_ == TransitionState::TransitionIn || switchInProgress_) return false;
        RuntimeSceneContext::ClearPendingSceneEntry();
        Finish(SceneTransitionOutcome::Cancelled, "Cancelled before opening the destination scene.");
        return true;
    }

    void SceneTransitionBus::Update(float dt) {
        if (!pendingRequest_.has_value()) {
            state_ = TransitionState::Idle;
            timer_ = 0.0f;
            return;
        }
        World* world = RuntimeSceneContext::GetCurrentWorld();
        if (world == nullptr || !world->IsOwnerThread()) {
            Finish(SceneTransitionOutcome::Cancelled, "The runtime World is no longer available.");
            return;
        }
#if defined(HIKARI_WITH_EDITOR)
        const auto* playState = world->Services().Find<RuntimePlayStateService>();
        if (playState == nullptr || !playState->IsActive()) {
            RuntimeSceneContext::ClearPendingSceneEntry();
            Finish(SceneTransitionOutcome::Cancelled, "Play stopped before the transition completed.");
            return;
        }
#endif
        if (!std::isfinite(dt)) dt = 0.0f;

        switch (state_) {
        case TransitionState::Idle:
            state_ = pendingRequest_->useTransition ? TransitionState::TransitionOut : TransitionState::SwitchingScene;
            timer_ = 0.0f;
            break;

        case TransitionState::TransitionOut:
            timer_ += (std::max)(0.0f, dt);
            if (timer_ >= transitionOutDuration_) {
                state_ = TransitionState::SwitchingScene;
                timer_ = 0.0f;
            }
            break;

        case TransitionState::SwitchingScene: {
            if (!pendingRequest_->targetSceneAssetGuid.empty()) {
                // Scene Asset GUID は現在の DocumentScene に直接渡す。
                RuntimeSceneContext::SetPendingSceneEntry(
                    pendingRequest_->targetSceneAssetGuid,
                    pendingRequest_->targetSpawnPointId,
                    pendingRequest_->userData);
                IScene* currentScene = sceneManager_.GetCurrentScene();
                DocumentSceneBase* documentScene = dynamic_cast<DocumentSceneBase*>(currentScene);
                bool opened = false;
                switchInProgress_ = true;
                try {
                    opened = documentScene && documentScene->OpenSceneAssetNow(
                        AssetGuid{pendingRequest_->targetSceneAssetGuid});
                } catch (const std::exception& exception) {
                    HIKARI_LOG_ERROR(std::string("[SceneTransition] scene load failed: ") + exception.what());
                } catch (...) {
                    HIKARI_LOG_ERROR("[SceneTransition] scene load threw an unknown exception.");
                }
                switchInProgress_ = false;
                if (!opened) {
                    RuntimeSceneContext::ClearPendingSceneEntry();
                    Finish(SceneTransitionOutcome::Failed,
                        "Scene loading or World construction failed. See the scene load log for details.");
                    break;
                }

                if (pendingRequest_->useTransition) {
                    state_ = TransitionState::TransitionIn;
                    timer_ = 0.0f;
                    cameraReadyWaitSeconds_ = 0.0f;
                } else {
                    Finish(SceneTransitionOutcome::Completed);
                }
                break;
            }

            break;
        }

        case TransitionState::TransitionIn:
            if (!transitionInReleased_) {
                break;
            }
            if (waitForCameraReady_ &&
                !RuntimeSceneContext::IsCameraReady() &&
                cameraReadyWaitSeconds_ < cameraReadyTimeout_) {
                cameraReadyWaitSeconds_ += (std::max)(0.0f, dt);
                break;
            }
            timer_ += (std::max)(0.0f, dt);
            if (timer_ >= transitionInDuration_) {
                Finish(SceneTransitionOutcome::Completed);
            }
            break;
        }
    }

    bool SceneTransitionBus::ReleaseTransitionIn() {
        if (!pendingRequest_.has_value() ||
            !pendingRequest_->holdTransitionIn ||
            transitionInReleased_) {
            return false;
        }
        if (state_ != TransitionState::SwitchingScene &&
            state_ != TransitionState::TransitionIn) {
            return false;
        }
        // Destination systems may release during the synchronous scene switch
        // before the bus assigns TransitionIn. Latch it across that boundary.
        transitionInReleased_ = true;
        timer_ = 0.0f;
        cameraReadyWaitSeconds_ = 0.0f;
        return true;
    }

    bool SceneTransitionBus::IsTransitioning() const {
        return state_ != TransitionState::Idle || pendingRequest_.has_value();
    }

    SceneTransitionBus::TransitionState SceneTransitionBus::GetState() const {
        return state_;
    }

    TransitionVisualState SceneTransitionBus::GetVisualState() const {
        TransitionVisualState visual{};
        visual.profileId = activeTransitionProfileId_;
        visual.outDuration = transitionOutDuration_;
        visual.inDuration = transitionInDuration_;

        switch (state_) {
        case TransitionState::TransitionOut:
            visual.active = true;
            visual.isTransitionIn = false;
            if (transitionOutDuration_ > 0.0f) {
                visual.progress = (std::min)(1.0f, (std::max)(0.0f, timer_ / transitionOutDuration_));
            } else {
                visual.progress = 1.0f;
            }
            break;
        case TransitionState::SwitchingScene:
            visual.active = true;
            visual.isTransitionIn = false;
            visual.progress = 1.0f;
            break;
        case TransitionState::TransitionIn:
            visual.active = true;
            visual.isTransitionIn = true;
            if (transitionInDuration_ > 0.0f) {
                visual.progress = (std::min)(1.0f, (std::max)(0.0f, timer_ / transitionInDuration_));
            } else {
                visual.progress = 1.0f;
            }
            break;
        case TransitionState::Idle:
        default:
            visual.active = false;
            visual.progress = 0.0f;
            visual.isTransitionIn = false;
            break;
        }

        return visual;
    }

} // namespace HIKARI
