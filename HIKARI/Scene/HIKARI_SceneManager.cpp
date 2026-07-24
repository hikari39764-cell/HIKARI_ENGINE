#include "HIKARI_SceneManager.h"

namespace HIKARI {

    SceneManager::~SceneManager() = default;

    void SceneManager::ChangeScene(std::unique_ptr<IScene> next, bool callOnEnterNext, bool callOnExitCurrent) {
        pending_ = std::move(next);
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

    IScene* SceneManager::GetCurrentScene() {
        return current_.get();
    }

    const IScene* SceneManager::GetCurrentScene() const {
        return current_.get();
    }

    std::unique_ptr<IScene> SceneManager::TakeCurrentScene() {
        return std::move(current_);
    }

    void SceneManager::CommitPendingScene() {
        if (!pending_) {
            return;
        }

        if (current_ && pendingCallOnExitCurrent_) {
            current_->OnExit();
        }

        current_ = std::move(pending_);

        if (current_ && pendingCallOnEnter_) {
            current_->OnEnter();
        }

        pendingCallOnEnter_ = true;
        pendingCallOnExitCurrent_ = true;
    }

} // namespace HIKARI
