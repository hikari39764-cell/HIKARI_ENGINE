#include "HIKARI_GameDocumentScene.h"

namespace HIKARI {

    GameDocumentScene::GameDocumentScene(std::string sceneId)
        : DocumentSceneBase(std::move(sceneId)) {
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
