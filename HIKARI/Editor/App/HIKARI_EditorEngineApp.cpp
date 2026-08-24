#include "Editor/App/HIKARI_EditorEngineApp.h"

#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

    bool EditorEngineApp::Initialize() {
        return runtimeApp_.Initialize();
    }

    void EditorEngineApp::Update(float deltaTime) {
        runtimeApp_.Update(deltaTime);
    }

    void EditorEngineApp::Render() {
        runtimeApp_.Render();
    }

    void EditorEngineApp::RenderHostUi() {
        runtimeApp_.RenderHostUi();
        if (!SERVICES::IsEditorHost() ||
            !SERVICES::IsEditorUIEnabled()) {
            return;
        }

        if (auto* documentScene =
                dynamic_cast<DocumentSceneBase*>(
                    runtimeApp_.GetCurrentScene())) {
            documentSceneEditorController_.Draw(
                *documentScene,
                editorPlaySession_);
        }
    }

    void EditorEngineApp::UpdateHostSessions() {
        if (auto* documentScene =
                dynamic_cast<DocumentSceneBase*>(
                    runtimeApp_.GetCurrentScene())) {
            if (SERVICES::ConsumeRuntimeSessionEndRequest()) {
                editorPlaySession_.RequestStop(
                    PlayStopReason::WindowClose);
            }
            editorPlaySession_.Update(*documentScene);
        }
    }

    void EditorEngineApp::RequestHostStop() {
        editorPlaySession_.RequestStop(
            PlayStopReason::Escape);
    }

    bool EditorEngineApp::IsWindowedHostSessionRunning() const {
        return editorPlaySession_.IsWindowedRunning();
    }

    bool EditorEngineApp::IsHostTransitioning() const {
        return editorPlaySession_.IsTransitioning();
    }

    void EditorEngineApp::Shutdown() {
        editorPlaySession_.Shutdown(
            dynamic_cast<DocumentSceneBase*>(
                runtimeApp_.GetCurrentScene()));
        runtimeApp_.Shutdown();
    }

} // namespace HIKARI::EDITOR
