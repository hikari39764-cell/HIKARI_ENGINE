#pragma once

#include <string>

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
    EDITOR::EnvironmentAssetEditor& Assets() { return assets_; }
    EDITOR::RenderStyleEditor& Style() { return style_; }

private:
    void DrawPreview(const ENVIRONMENT::SceneEnvironmentBinding& binding, DocumentSceneBase& scene);

    EDITOR::EnvironmentAssetEditor assets_;
    EDITOR::RenderStyleEditor style_;
    // 編集中に時刻を流す試し再生。シーン文書と Play には影響しない。
    bool previewTimeFlow_ = false;
    float previewHoursPerSecond_ = 1.0f;
    std::string previewError_;
};

} // namespace HIKARI
