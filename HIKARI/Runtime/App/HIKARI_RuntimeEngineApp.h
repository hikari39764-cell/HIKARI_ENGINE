#pragma once

#include "Runtime/App/HIKARI_IHostApplication.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneManager.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

namespace HIKARI::RUNTIME {

    class IGameProjectModule;

    class RuntimeEngineApp final : public IHostApplication {
    public:
        explicit RuntimeEngineApp(
            IGameProjectModule& gameProjectModule);

        bool Initialize() override;
        void Update(float deltaTime) override;
        void Render() override;
        void RenderHostUi() override;
        void Shutdown() override;

        IScene* GetCurrentScene();
        const IScene* GetCurrentScene() const;

    private:
        IGameProjectModule* gameProjectModule_ = nullptr;
        SceneManager sceneManager_{};
        SceneTransitionBus sceneTransitionBus_;
    };

} // namespace HIKARI::RUNTIME
