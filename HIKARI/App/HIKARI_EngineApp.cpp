#include "HIKARI_EngineApp.h"

#include <memory>
#include <string>

#include "HIKARI_Services.h"
#include "Scene/Scenes/HIKARI_GameDocumentScene.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI {

    EngineApp::EngineApp()
        : sceneTransitionBus_(sceneManager_) {
    }

    bool EngineApp::Initialize() {
        HIKARI_LOG_INFO("EngineApp initialization started.");

        // 起動 Scene は Scene Asset GUID から DocumentSceneBase が解決する。
        std::unique_ptr<IScene> initialScene = std::make_unique<GameDocumentScene>("StartupSceneAsset");

        RuntimeSceneContext::SetTransitionBus(&sceneTransitionBus_);
        sceneManager_.ChangeScene(std::move(initialScene));
        HIKARI_LOG_INFO("Initial document scene created.");
        HIKARI_LOG_INFO("EngineApp initialization completed.");
        return true;
    }

    void EngineApp::Update(float dt) {
        sceneManager_.Update(dt);
        sceneTransitionBus_.Update(dt);
        const TransitionVisualState visualState = sceneTransitionBus_.GetVisualState();
        if (visualState.active) {
            POST::PostSystem::SetTransitionState(visualState);
        } else {
            POST::PostSystem::ClearTransitionState();
        }
    }

    void EngineApp::Render() {
        sceneManager_.Render();
    }

    void EngineApp::RenderImGui() {
#if defined(HIKARI_ENABLE_IMGUI)
        sceneManager_.RenderImGui();

        if (SERVICES::ArePortableObjectToolsEnabled()) {
            if (auto* docScene = dynamic_cast<DocumentSceneBase*>(sceneManager_.GetCurrentScene())) {
                portableObjectToolsPanel_.Draw(*docScene);
            }
            return;
        }

#if defined(HIKARI_WITH_EDITOR)
        if (!SERVICES::IsEditorHost() || !SERVICES::IsEditorUIEnabled()) {
            return;
        }

        if (auto* docScene = dynamic_cast<DocumentSceneBase*>(sceneManager_.GetCurrentScene())) {
            documentSceneEditorController_.Draw(*docScene, editorPlaySession_);
        }
#endif
#endif
    }

    void EngineApp::UpdatePlaySessions() {
#if defined(HIKARI_WITH_EDITOR)
        if (auto* docScene =
                dynamic_cast<DocumentSceneBase*>(sceneManager_.GetCurrentScene())) {
            editorPlaySession_.Update(*docScene);
        }
#endif
    }

    void EngineApp::RequestPlayStop() {
#if defined(HIKARI_WITH_EDITOR)
        editorPlaySession_.RequestStop(EDITOR::PlayStopReason::Escape);
#endif
    }

    bool EngineApp::IsWindowedPlayRunning() const {
#if defined(HIKARI_WITH_EDITOR)
        return editorPlaySession_.IsWindowedRunning();
#else
        return false;
#endif
    }

    bool EngineApp::IsPlayTransitioning() const {
#if defined(HIKARI_WITH_EDITOR)
        return editorPlaySession_.IsTransitioning();
#else
        return false;
#endif
    }

    void EngineApp::Shutdown() {
#if defined(HIKARI_WITH_EDITOR)
        editorPlaySession_.Shutdown(
            dynamic_cast<DocumentSceneBase*>(sceneManager_.GetCurrentScene()));
#endif
        RuntimeSceneContext::SetTransitionBus(nullptr);
        sceneManager_.ChangeScene(nullptr);
        sceneManager_.Update(0.0f);
    }

} // namespace HIKARI
