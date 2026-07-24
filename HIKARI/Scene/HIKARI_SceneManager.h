#pragma once
#include <memory>
#include "HIKARI_IScene.h"

namespace HIKARI {

    class SceneManager {
    public:
        SceneManager() = default;
        ~SceneManager();
        void ChangeScene(std::unique_ptr<IScene> next, bool callOnEnterNext = true, bool callOnExitCurrent = true);
        void Update(float dt);
        void Render();
        void RenderImGui();
        std::unique_ptr<IScene> TakeCurrentScene();

        IScene* GetCurrentScene();

        const IScene* GetCurrentScene() const;

    private:
        void CommitPendingScene();

    private:
        std::unique_ptr<IScene> current_;
        std::unique_ptr<IScene> pending_;
        bool pendingCallOnEnter_ = true;
        bool pendingCallOnExitCurrent_ = true;
    };

} // namespace HIKARI
