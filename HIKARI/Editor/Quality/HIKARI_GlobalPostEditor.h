#pragma once

#include <cstdint>
#include <string>

#include "Editor/Documents/HIKARI_PostProfileEditorDocument.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {
class DocumentSceneBase;
namespace POST {
class PostAssetStore;
}
} // namespace HIKARI

namespace HIKARI::EDITOR {

// 環境の全体後処理。後処理プロファイル資産を選び、プロジェクトのプロファイルはパラメーターを編集して保存する。
// エンジン内蔵のプロファイルは読み取り専用で、編集する時はプロジェクトへ複製する。
class GlobalPostEditor {
public:
    void Draw(ScenePostSettings& post, DocumentSceneBase& scene);
    // 画面と AI 操作は同じプロファイル文書を編集する。
    PostProfileEditorDocument& Document() { return document_; }
    // 内蔵やほかのプロファイルを元に、プロジェクトのプロファイル資産を作って保存する。
    bool CreateProjectCopy(POST::PostProfileAsset source, AssetDatabase& database, std::string& message);
    // 未保存の草稿を描画へ渡す。保存済みなら資産の内容で描画する。
    void PublishDraft(POST::PostAssetStore& store);

private:
    void DrawProfilePicker(ScenePostSettings& post, const AssetDatabase& database) const;
    void DrawBuiltinProfile(ScenePostSettings& post, AssetDatabase& database, const POST::PostAssetStore& store);
    void DrawProjectProfile(const AssetGuid& guid, AssetDatabase& database, POST::PostAssetStore& store);
    bool OpenProjectProfile(const AssetGuid& guid, AssetDatabase& database);
    void DrawEffectPicker(const AssetDatabase& database);
    void DrawDocumentToolbar(AssetDatabase& database);
    void DrawParameters(const POST::PostEffectDefinition& effect, const POST::PostProfileAsset& profile, bool editable);

    PostProfileEditorDocument document_;
    uint64_t publishedRevision_ = 0;
    std::string status_;
};

} // namespace HIKARI::EDITOR
