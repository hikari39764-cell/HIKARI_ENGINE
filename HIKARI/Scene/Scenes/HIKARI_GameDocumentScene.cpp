#include "HIKARI_GameDocumentScene.h"

#include "Core/HIKARI_Logger.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"

namespace HIKARI {

    GameDocumentScene::GameDocumentScene(std::string sceneId)
        : DocumentSceneBase(std::move(sceneId)) {
    }

    void GameDocumentScene::OnEnter() {
        DocumentSceneBase::OnEnter();
        if (!SERVICES::IsStandaloneGameHost()) {
            return;
        }
        if (!BeginRuntimePlay()) {
            HIKARI_LOG_ERROR(
                "[RuntimeLifecycle] standalone game scene failed "
                "to enter Runtime Play state.");
        }
    }

	// シーンの名前を取得する。シーンの名前は、シーンドキュメントの sceneName フィールドから取得される。
    const char* GameDocumentScene::GetSceneName() const {
        return GetSceneDocument().sceneName.c_str();
    }

	// ImGui を使ったエディタ UI の描画を行う。ゲームプレイ中は、シーンのオブジェクトや環境設定を編集できないようにするため、ImGui の描画は DocumentSceneBase に任せる
    void GameDocumentScene::RenderImGui() {
        DocumentSceneBase::RenderImGui();
    }

} // namespace HIKARI
