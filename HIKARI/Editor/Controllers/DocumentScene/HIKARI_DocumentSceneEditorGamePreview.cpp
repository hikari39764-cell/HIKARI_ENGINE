#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI {

    bool DocumentSceneEditorController::SaveRenderQualityProfile(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        std::string errorMessage{};
        if (!RENDER3D::SaveRenderQualityProfile(
                scene.GetAssetDatabase().GetProjectRoot(),
                RENDER3D::GetRenderQualitySettings(),
                &errorMessage)) {
            viewportDropMessage_ = errorMessage;
            return false;
        }

        renderQualitySavePending_ = false;
        return true;
#else
        (void)scene;
        return false;
#endif
    }

    void DocumentSceneEditorController::ToggleGamePreview(
        DocumentSceneBase& scene,
        EDITOR::EditorPlaySession& playSession) {
#if defined(HIKARI_WITH_EDITOR)
        if (playSession.IsRunning() ||
            playSession.GetState() == EDITOR::EditorPlayState::Starting) {
            playSession.RequestStop();
            viewportDropMessage_ = playSession.GetStatusMessage();
            return;
        }

        if (!PrepareGamePreview(scene)) {
            return;
        }
        playSession.RequestEmbeddedStart();
        viewportDropMessage_ = playSession.GetStatusMessage();
#else
        (void)scene;
        (void)playSession;
#endif
    }

    void DocumentSceneEditorController::LaunchWindowedGamePreview(
        DocumentSceneBase& scene,
        EDITOR::EditorPlaySession& playSession) {
#if defined(HIKARI_WITH_EDITOR)
        if (playSession.IsRunning() ||
            playSession.GetState() == EDITOR::EditorPlayState::Starting) {
            viewportDropMessage_ = "Stop the active Play session first.";
            return;
        }
        if (!PrepareGamePreview(scene)) {
            return;
        }
        playSession.RequestWindowedStart();
        viewportDropMessage_ = playSession.GetStatusMessage();
#else
        (void)scene;
        (void)playSession;
#endif
    }

    bool DocumentSceneEditorController::PrepareGamePreview(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        cinematicsWorkspaceController_.PrepareForRuntimePlay();
        const AssetGuid& sceneGuid = scene.GetCurrentSceneAssetGuid();
        if (!sceneGuid.IsValid()) {
            viewportDropMessage_ =
                "Play requires the current scene to be saved as an asset";
            return false;
        }

        scene.GetSceneDocument().environment = scene.GetSceneEnvironment();
        if (!scene.SaveCurrentSceneDocument()) {
            viewportDropMessage_ = "Could not save the current scene for Play";
            return false;
        }

        context_.sceneDirty = false;
        scene.SetUnsavedSceneChanges(false);
        if (!SaveRenderQualityProfile(scene)) {
            return false;
        }
        return true;
#else
        (void)scene;
        return false;
#endif
    }

} // namespace HIKARI
