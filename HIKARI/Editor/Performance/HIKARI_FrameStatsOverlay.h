#pragma once

#include "imgui.h"

namespace HIKARI::EDITOR {

// 帧率と CPU / GPU のフレーム時間を小さな札にして前面に描く（view.stats）。札の pivot（0..1、(1,0) なら右上の角）を
// anchor に合わせる。値は指標の登録表から読むので、性能審計の窓と同じ数字になる。
void DrawFrameStatsOverlay(const ImVec2& anchor, const ImVec2& pivot);

} // namespace HIKARI::EDITOR
