#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Render3D/Settings/HIKARI_RenderQualityProfileStore.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI {

bool DocumentSceneEditorController::SaveRenderQualityProfile(DocumentSceneBase& scene) {
    std::string errorMessage{};
    if (!RENDER3D::SaveRenderQualityProfile(scene.GetAssetDatabase().GetProjectRoot(),
                                            RENDER3D::GetRenderQualitySettings(), &errorMessage)) {
        viewportDropMessage_ = errorMessage;
        return false;
    }

    renderQualitySavePending_ = false;
    return true;
}

void DocumentSceneEditorController::ToggleGamePreview(DocumentSceneBase& scene,
                                                      EDITOR::EditorPlaySession& playSession) {
    if (playSession.IsRunning() || playSession.GetState() == EDITOR::EditorPlayState::Starting) {
        playSession.RequestStop();
        viewportDropMessage_ = playSession.GetStatusMessage();
        return;
    }

    if (!PrepareGamePreview(scene)) {
        return;
    }
    playSession.RequestEmbeddedStart();
    viewportDropMessage_ = playSession.GetStatusMessage();
}

void DocumentSceneEditorController::LaunchWindowedGamePreview(DocumentSceneBase& scene,
                                                              EDITOR::EditorPlaySession& playSession) {
    if (playSession.IsRunning() || playSession.GetState() == EDITOR::EditorPlayState::Starting) {
        viewportDropMessage_ = "Stop the active Play session first.";
        return;
    }
    if (!PrepareGamePreview(scene)) {
        return;
    }
    playSession.RequestWindowedStart();
    viewportDropMessage_ = playSession.GetStatusMessage();
}

bool DocumentSceneEditorController::PrepareGamePreview(DocumentSceneBase& scene) {
    CinematicsWorkspace().PrepareForRuntimePlay(scene);
    const AssetGuid& sceneGuid = scene.GetCurrentSceneAssetGuid();
    if (!sceneGuid.IsValid()) {
        viewportDropMessage_ = "Play requires the current scene to be saved as an asset";
        return false;
    }

    // Play は編集中の文書から世界を作り直すので、開始前の保存は未保存の変更を残すためだけに行う。
    // 変更の無い場面と画質を書き直すと、手で書いた小数の表記が変わり、既定値の画質ファイルが増える。
    if (context_.sceneDirty || scene.HasUnsavedSceneChanges()) {
        if (!scene.SaveCurrentSceneDocument()) {
            viewportDropMessage_ = "Could not save the current scene for Play";
            return false;
        }
        context_.sceneDirty = false;
        scene.SetUnsavedSceneChanges(false);
    }
    return !renderQualitySavePending_ || SaveRenderQualityProfile(scene);
}

} // namespace HIKARI
