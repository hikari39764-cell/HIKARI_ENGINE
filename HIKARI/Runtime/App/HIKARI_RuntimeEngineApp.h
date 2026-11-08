#pragma once

#include <memory>
#include <vector>

#include "Project/HIKARI_ProjectModules.h"
#include "Runtime/App/HIKARI_IHostApplication.h"
#include "Runtime/Project/HIKARI_GameProjectModuleLibrary.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectSessionRuntimeExtension.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneManager.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

namespace HIKARI {
class DocumentSceneBase;
}

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
    std::vector<InstalledModuleStatus>& GetInstalledModules() noexcept { return installedModuleStatus_; }

private:
    bool ComposeInstalledModules(DocumentSceneBase& scene);

    GameProjectModuleLibrary gameProjectModule_{};
    // 依存が先の順。Shutdown で場景を壊した後、逆順に解放する。
    std::vector<std::unique_ptr<GameProjectModuleLibrary>> installedModules_{};
    std::vector<InstalledModuleStatus> installedModuleStatus_{};
    GameProjectSessionRuntimeExtension gameProjectSessionExtension_;
    SceneManager sceneManager_{};
    SceneTransitionBus sceneTransitionBus_;
    bool standaloneRuntimeEntryFailed_ = false;
};

} // namespace HIKARI::RUNTIME
