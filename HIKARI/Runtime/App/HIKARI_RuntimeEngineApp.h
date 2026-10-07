#pragma once

#include "Runtime/App/HIKARI_IHostApplication.h"
#include "Runtime/Project/HIKARI_GameProjectModuleLibrary.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectSessionRuntimeExtension.h"
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
    void Shutdown() override;
    bool ShouldExitHost() const override;
    int GetExitCode() const override;

    IScene* GetCurrentScene();
    const IScene* GetCurrentScene() const override;

private:
    GameProjectModuleLibrary gameProjectModule_{};
    GameProjectSessionRuntimeExtension gameProjectSessionExtension_;
    SceneManager sceneManager_{};
    SceneTransitionBus sceneTransitionBus_;
    bool standaloneRuntimeEntryFailed_ = false;
};

} // namespace HIKARI::RUNTIME
