#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Editor/Environment/HIKARI_EnvironmentAssetEditor.h"
#include "Editor/RenderStyle/HIKARI_RenderStyleEditor.h"
#include "Scene/Environment/HIKARI_SceneEnvironmentBinding.h"

namespace HIKARI {

class DocumentSceneBase;

struct EnvironmentPanelResult {
    bool bindingChanged = false;
    bool openLightingBakeRequested = false;
    bool editActive = false;
};

// 環境ウィンドウ。シーンの参照（プロファイル・天気・描画の様式・初期時刻・地点）は文書の編集として返し、
// プロファイル・天気・様式の資産は資産文書で、時刻と天気の試し再生は実行時の状態だけで扱う。
class EnvironmentPanel {
public:
    // binding はシーン文書の環境の参照の編集用の複製。
    EnvironmentPanelResult Draw(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene);
    // 布景の「灯光与画面」の窓：画面预设（A/B 比較・存为预设）、光照模式、常用の光と大気と画づくり。
    EnvironmentPanelResult DrawLookDev(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene);
    EDITOR::EnvironmentAssetEditor& Assets() { return assets_; }
    EDITOR::RenderStyleEditor& Style() { return style_; }
    // 前のフレームで窓に焦点があり、最後に編集したのが資産文書ならその文書（元に戻すをそこへ振り分ける）。
    // 最後の編集がシーンの参照なら nullptr（シーンの撤销）。
    EDITOR::EditorAssetDocument* FocusedDocument() const { return focused_ ? lastEdited_ : nullptr; }
    // この窓が編集する資産文書（環境プロファイル・天気・描画の様式・後処理）。全部保存と未保存の一覧に使う。
    std::array<EDITOR::EditorAssetDocument*, 4> Documents();
    bool HasUnsavedDocuments() const;

private:
    void DrawPreview(const ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene);
    // 光照模式（美術布景／昼夜時間）の切り替えと、昼夜なら時刻・日付・地点。
    void DrawLightingMode(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene,
                          EnvironmentPanelResult& result);
    void DrawLightingControls(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene,
                              EnvironmentPanelResult& result);
    void DrawPresets(ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene,
                     EnvironmentPanelResult& result);
    // 窓を閉じる前に、下書きを描画へ渡し、焦点と最後に編集した文書を記録する。
    void FinishWindow(DocumentSceneBase& scene, EnvironmentPanelResult& result, bool bindingActive);
    void TrackEditedDocuments();

    EDITOR::EnvironmentAssetEditor assets_;
    EDITOR::RenderStyleEditor style_;
    // 編集中に時刻を流す試し再生。シーン文書と Play には影響しない。
    bool previewTimeFlow_ = false;
    float previewHoursPerSecond_ = 1.0f;
    std::string previewError_;
    // 画面预设の A/B 比較の相手（直前に選んでいたプロファイル）。
    std::string compareProfileId_;
    EDITOR::EditorAssetDocument* lastEdited_ = nullptr;
    std::array<uint64_t, 4> seenRevisions_{};
    bool focused_ = false;
};

} // namespace HIKARI
