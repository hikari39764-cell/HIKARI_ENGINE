#pragma once

namespace HIKARI {

// 画質ウィンドウ。プロジェクトの画質設定（解像度・パイプライン・影の技術設定）を編集する。
// 画面の見た目（露出・後処理・SSAO など）は環境プロファイルが持ち、環境ウィンドウで編集する。
class QualityPanel {
public:
    // 画質設定が変わったら true（保存はエディターが行う）。
    bool Draw();
};

} // namespace HIKARI
