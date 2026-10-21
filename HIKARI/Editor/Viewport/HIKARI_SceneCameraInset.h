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
    // 最後に描いた時の主ビューの露出（log2）。
    float exposureLog2_ = 0.0f;
    // 最後に描いた機位の姿勢と画角。動かした・合わせた時に描き直す（文書の版は場面を開いた時しか進まない）。
    MATH::Vec3 drawnPosition_{};
    MATH::Vec3 drawnTarget_{};
    float drawnFovY_ = 0.0f;
    // 露出か姿勢が変わるたびに進める描き直しの版。
    uint64_t contentRevision_ = 0;
    bool submitted_ = false;
};

} // namespace HIKARI::EDITOR
