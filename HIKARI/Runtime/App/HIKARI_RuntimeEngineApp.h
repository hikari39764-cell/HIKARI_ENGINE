#pragma once

#include "Runtime/App/HIKARI_IHostApplication.h"
#include "Runtime/Project/HIKARI_GameProjectModuleLibrary.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneManager.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

namespace HIKARI::RUNTIME {

    class RuntimeEngineApp final : public IHostApplication {
    public:
        RuntimeEngineApp();

        bool Initialize() override;
        void Update(float deltaTime) override;
        void Render() override;
        void RenderHostUi() override;
        void Shutdown() override;
        bool ShouldExitHost() const override;

        IScene* GetCurrentScene();
        const IScene* GetCurrentScene() const override;

    private:
        GameProjectModuleLibrary gameProjectModule_{};
        SceneManager sceneManager_{};
        SceneTransitionBus sceneTransitionBus_;
        bool standaloneRuntimeEntryFailed_ = false;
    };

} // namespace HIKARI::RUNTIME
