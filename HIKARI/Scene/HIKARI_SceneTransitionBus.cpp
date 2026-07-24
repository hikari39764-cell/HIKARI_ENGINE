#include "HIKARI_SceneTransitionBus.h"

#include <algorithm>

#include "HIKARI_RuntimeSceneContext.h"
#include "HIKARI_IScene.h"
#include "HIKARI_SceneManager.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Vfx/Transition/HIKARI_TransitionProfile.h"

namespace HIKARI {

    SceneTransitionBus::SceneTransitionBus(SceneManager& sceneManager)
        : sceneManager_(sceneManager) {
    }

    bool SceneTransitionBus::RequestTransition(const SceneTransitionRequest& request) {
        if (!IsValidAssetGuid(request.targetSceneAssetGuid)) {
            return false;
        }

        pendingRequest_ = request;
        activeTransitionProfileId_ = request.transitionProfileId;
        transitionOutDuration_ = 0.2f;
        transitionInDuration_ = 0.2f;
        TransitionProfile profile{};
        if (TransitionProfile::LoadById(request.transitionProfileId, profile)) {
            transitionOutDuration_ = (std::max)(0.0f, profile.outDuration);
            transitionInDuration_ = (std::max)(0.0f, profile.inDuration);
        }
        state_ = request.useTransition ? TransitionState::TransitionOut : TransitionState::SwitchingScene;
        timer_ = 0.0f;
        return true;
    }

    void SceneTransitionBus::Update(float dt) {
        if (!pendingRequest_.has_value()) {
            state_ = TransitionState::Idle;
            timer_ = 0.0f;
            return;
        }

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
                RuntimeSceneContext::SetPendingSceneEntry(pendingRequest_->targetSceneAssetGuid, pendingRequest_->targetSpawnPointId);
                IScene* currentScene = sceneManager_.GetCurrentScene();
                DocumentSceneBase* documentScene = dynamic_cast<DocumentSceneBase*>(currentScene);
                const bool opened = documentScene &&
                    documentScene->OpenSceneAssetNow(AssetGuid{ pendingRequest_->targetSceneAssetGuid });
                lastSceneLoadedFromCache_ = false;

                if (!opened) {
                    state_ = TransitionState::Idle;
                    pendingRequest_.reset();
                    break;
                }

                if (pendingRequest_->useTransition) {
                    state_ = TransitionState::TransitionIn;
                    timer_ = 0.0f;
                } else {
                    state_ = TransitionState::Idle;
                    pendingRequest_.reset();
                }
                break;
            }

            break;
        }

        case TransitionState::TransitionIn:
            timer_ += (std::max)(0.0f, dt);
            if (timer_ >= transitionInDuration_) {
                state_ = TransitionState::Idle;
                timer_ = 0.0f;
                pendingRequest_.reset();
            }
            break;
        }
    }

    bool SceneTransitionBus::IsTransitioning() const {
        return state_ != TransitionState::Idle || pendingRequest_.has_value();
    }

    SceneTransitionBus::TransitionState SceneTransitionBus::GetState() const {
        return state_;
    }

    bool SceneTransitionBus::WasLastSceneLoadedFromCache() const {
        return lastSceneLoadedFromCache_;
    }

    size_t SceneTransitionBus::GetCachedSceneCount() const {
        return 0;
    }

    std::vector<std::string> SceneTransitionBus::GetCachedSceneIds() const {
        return {};
    }

    TransitionVisualState SceneTransitionBus::GetVisualState() const {
        TransitionVisualState visual{};
        visual.profileId = activeTransitionProfileId_;

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
