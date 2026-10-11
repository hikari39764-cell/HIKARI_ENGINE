#pragma once

#include "Editor/Viewport/HIKARI_SceneViewportSelectionService.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Scene/HIKARI_SceneObjectId.h"

struct ImDrawList;

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::EDITOR {

// 機位の小窓：選んだ機位から見た画面を主视口の右下に小さく出し、物を動かしながらその機位の構図を確かめる。
// 場面の文書、機位、借りている主ビューの露出が変わった時だけ描き直す（止まっている間は GPU を使わない）。
class SceneCameraInset {
public:
    // この帧に出す機位を決め、小窓の領域を返す（出さない時は幅 0）。この領域は選択や手柄の入力から除く。
    SceneViewportRect Layout(const DocumentSceneBase& scene, SceneObjectId camera, const SceneViewportRect& viewport);
    // Layout で決めた機位の画面を描く。出さない帧は描画の依頼を取り下げる。
    void Draw(const DocumentSceneBase& scene, ImDrawList& draw);

private:
    SceneObjectId camera_{};
    SceneViewportRect rect_{};
    Camera3D view_{};
    // 最後に描いた時の主ビューの露出（log2）と、それが変わるたびに増やす版。
    float exposureLog2_ = 0.0f;
    uint64_t exposureRevision_ = 0;
    bool submitted_ = false;
};

} // namespace HIKARI::EDITOR
