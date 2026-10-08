#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Editor/Documents/HIKARI_PostProfileEditorDocument.h"

namespace HIKARI {
class DocumentSceneBase;
namespace POST {
class PostAssetStore;
}
} // namespace HIKARI

namespace HIKARI::EDITOR {

// 全体後処理のプロファイル。描画の様式の後処理の層が後処理プロファイル資産を選び、プロジェクトのプロファイルは
// パラメーターを編集して保存する。エンジン内蔵のプロファイルは読み取り専用で、編集する時はプロジェクトへ複製する。
class GlobalPostEditor {
public:
    // 後処理プロファイルの選択（描画の様式の後処理の層）。preview があれば閉じた時の表示をそれにする。
    bool DrawProfilePicker(std::string& profileId, const AssetDatabase& database, const char* preview = nullptr) const;
    // 選んだプロファイルのパラメーター。複製を作った時はその ID を返す（allowCopy の時だけ）。
    std::string DrawSelectedProfile(const std::string& profileId, DocumentSceneBase& scene, bool allowCopy);
    // 画面と AI 操作は同じプロファイル文書を編集する。
    PostProfileEditorDocument& Document() { return document_; }
    // 内蔵やほかのプロファイルを元に、プロジェクトのプロファイル資産を作って保存する。
    bool CreateProjectCopy(POST::PostProfileAsset source, AssetDatabase& database, std::string& message);
    // 未保存の草稿を描画へ渡す。保存済みなら資産の内容で描画する。
    void PublishDraft(POST::PostAssetStore& store);

private:
    std::string DrawBuiltinProfile(const std::string& profileId, AssetDatabase& database,
                                   const POST::PostAssetStore& store, bool allowCopy);
    void DrawProjectProfile(const AssetGuid& guid, AssetDatabase& database, POST::PostAssetStore& store);
    void DrawEffectPicker(const AssetDatabase& database);
    void DrawParameters(const POST::PostEffectDefinition& effect, const POST::PostProfileAsset& profile, bool editable);

    PostProfileEditorDocument document_;
    uint64_t publishedRevision_ = 0;
    std::string status_;
};

// 内蔵とプロジェクトの後処理プロファイルの表示名。
std::string ResolvePostProfileLabel(std::string_view id, const AssetDatabase& database);

} // namespace HIKARI::EDITOR
