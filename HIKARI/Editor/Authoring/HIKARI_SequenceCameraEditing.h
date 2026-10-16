#pragma once
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::EDITOR {
// UI と操作入口が使う素材編集。分割は評価曲線を変更せず、同一素材の再生区間だけを分ける。
// 解決済みの曲線を同じ参照原本へ戻す。非対象軸と回転は保持する。
void ApplyResolvedShotPoseCurves(SEQUENCER::ShotCamera& camera, const SEQUENCER::ShotCamera& resolved,
                                 const std::array<ScalarCurve, 4>& curves);
uint64_t SplitSequenceShot(CinematicSequence& sequence, uint64_t shotId, float sequenceTime);
uint64_t DuplicateSequenceShot(CinematicSequence& sequence, uint64_t shotId);
} // namespace HIKARI::EDITOR
