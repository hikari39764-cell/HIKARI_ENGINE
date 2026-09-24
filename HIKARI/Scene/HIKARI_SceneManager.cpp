#include "HIKARI_SceneManager.h"

namespace HIKARI {

SceneManager::~SceneManager() = default;

void SceneManager::ChangeScene(std::unique_ptr<IScene> next, bool callOnEnterNext, bool callOnExitCurrent) {
    pending_ = std::move(next);
    hasPendingSceneChange_ = true;
    pendingCallOnEnter_ = callOnEnterNext;
    pendingCallOnExitCurrent_ = callOnExitCurrent;
}

void SceneManager::Update(float dt) {
    CommitPendingScene();
    if (current_) {
        current_->Update(dt);
    }
}

void SceneManager::Render() {
    if (current_) {
        current_->Render();
    }
}

void SceneManager::RenderImGui() {
    if (current_) {
        current_->RenderImGui();
    }
}

IScene* SceneManager::GetCurrentScene() { return current_.get(); }

const IScene* SceneManager::GetCurrentScene() const { return current_.get(); }

void SceneManager::CommitPendingScene() {
    if (!hasPendingSceneChange_) {
        return;
    }

    if (current_ && pendingCallOnExitCurrent_) {
        current_->OnExit();
    }

    current_ = std::move(pending_);

    if (current_ && pendingCallOnEnter_) {
        current_->OnEnter();
    }

    hasPendingSceneChange_ = false;
    pendingCallOnEnter_ = true;
    pendingCallOnExitCurrent_ = true;
}

} // namespace HIKARI
