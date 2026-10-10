#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Editor/Documents/HIKARI_RenderStyleEditorDocument.h"
#include "Editor/Quality/HIKARI_GlobalPostEditor.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

// 描画の様式（.hrenderstyle）の資産を編集する。エンジン内蔵の様式は読み取り専用で、編集する時はプロジェクトへ複製する。
// 後処理の層が選ぶプロファイルのパラメーターは GlobalPostEditor が編集する。未保存の下書きは様式と後処理の store へ渡し、
// 保存前からシーンへ映す。画面と AI 操作は同じ文書を使う。
class RenderStyleEditor {
public:
    // 資産の内容を描く。複製を作った時は、シーンの参照を切り替えるためにその ID を返す。
    std::string DrawStyle(const std::string& id, DocumentSceneBase& scene);
    // 下書きを store へ渡す。毎フレームと AI の編集後に呼ぶ。
    void PublishDrafts(DocumentSceneBase& scene);

    RenderStyleEditorDocument& Document() { return document_; }
    const RenderStyleEditorDocument& Document() const { return document_; }
    GlobalPostEditor& GlobalPost() { return globalPost_; }
    const GlobalPostEditor& GlobalPost() const { return globalPost_; }

private:
    // 後処理の層の一覧と、選んだ層のプロファイル。層か内蔵プロファイルの複製で style を変えたら true。
    bool DrawPostLayers(RENDERSTYLE::RenderStyleAsset& style, DocumentSceneBase& scene);

    RenderStyleEditorDocument document_;
    GlobalPostEditor globalPost_;
    uint64_t publishedRevision_ = 0;
    size_t selectedLayer_ = 0;
    std::string status_;
};

// 内蔵とプロジェクトの描画の様式から一つを選ぶ。
bool DrawRenderStylePicker(const AssetDatabase& database, std::string& id);
std::string ResolveRenderStyleLabel(std::string_view id, const AssetDatabase& database);

} // namespace HIKARI::EDITOR
