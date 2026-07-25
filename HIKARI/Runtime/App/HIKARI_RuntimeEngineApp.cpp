#include "Runtime/App/HIKARI_RuntimeEngineApp.h"

#include <memory>

#include "Core/HIKARI_Logger.h"
#include "Runtime/Project/HIKARI_IGameProjectModule.h"
#include "Scene/Scenes/HIKARI_GameDocumentScene.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI::RUNTIME {

    RuntimeEngineApp::RuntimeEngineApp(
        IGameProjectModule& gameProjectModule)
        : gameProjectModule_(&gameProjectModule),
          sceneTransitionBus_(sceneManager_) {
    }

    bool RuntimeEngineApp::Initialize() {
        HIKARI_LOG_INFO("RuntimeEngineApp initialization started.");

        // 襍ｷ蜍・Scene 縺ｯ Scene Asset GUID 縺九ｉ DocumentSceneBase 縺瑚ｧ｣豎ｺ縺吶ｋ縲・
        auto initialScene =
            std::make_unique<GameDocumentScene>("StartupSceneAsset");
        if (gameProjectModule_ == nullptr) {
            HIKARI_LOG_ERROR(
                "[GameProject] no project module was composed");
            return false;
        }

        const std::string moduleId(
            gameProjectModule_->GetModuleId());
        std::vector<std::unique_ptr<IRuntimeExtension>> extensions =
            gameProjectModule_->CreateRuntimeExtensions();
        for (std::unique_ptr<IRuntimeExtension>& extension :
                extensions) {
            if (!initialScene->AddRuntimeExtension(
                    std::move(extension))) {
                HIKARI_LOG_ERROR(
                    "[GameProject] invalid or duplicate runtime extension: " +
                    moduleId);
                return false;
            }
        }
        HIKARI_LOG_INFO(
            "[GameProject] composed module=" + moduleId +
            " extensions=" +
            std::to_string(
                initialScene->GetRuntimeExtensionIds().size()));

        RuntimeSceneContext::SetTransitionBus(&sceneTransitionBus_);
        sceneManager_.ChangeScene(std::move(initialScene));
        HIKARI_LOG_INFO("Initial document scene created.");
        HIKARI_LOG_INFO("RuntimeEngineApp initialization completed.");
        return true;
    }

    void RuntimeEngineApp::Update(float deltaTime) {
        sceneManager_.Update(deltaTime);
        sceneTransitionBus_.Update(deltaTime);
        const TransitionVisualState visualState =
            sceneTransitionBus_.GetVisualState();
        if (visualState.active) {
            POST::PostSystem::SetTransitionState(visualState);
        }
        else {
            POST::PostSystem::ClearTransitionState();
        }
    }

    void RuntimeEngineApp::Render() {
        sceneManager_.Render();
    }

    void RuntimeEngineApp::RenderHostUi() {
        sceneManager_.RenderImGui();
    }

    void RuntimeEngineApp::Shutdown() {
        RuntimeSceneContext::SetTransitionBus(nullptr);
        sceneManager_.ChangeScene(nullptr);
        sceneManager_.Update(0.0f);
    }

    IScene* RuntimeEngineApp::GetCurrentScene() {
        return sceneManager_.GetCurrentScene();
    }

    const IScene* RuntimeEngineApp::GetCurrentScene() const {
        return sceneManager_.GetCurrentScene();
    }

} // namespace HIKARI::RUNTIME
