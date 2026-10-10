#pragma once

namespace HIKARI {
struct SceneEnvironment;
}

namespace HIKARI::EDITOR {

// 光と画面に関わる描画機能（GI・光追の反射と影・接触影）が今の画面に効いているかと、効いていない理由を並べる。
// 値は直近に描いたフレームのもの。布景の光と画面の窓の上に出す。
void DrawRenderFeatureStatus(const SceneEnvironment& environment);

} // namespace HIKARI::EDITOR
