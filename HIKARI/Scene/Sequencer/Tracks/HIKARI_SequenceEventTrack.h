#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace HIKARI::SEQUENCER {
// 事件轨：名前の付いた時間の範囲。引擎は何も実行しない。ゲームのプロジェクトが名前で読み
// （SDK の SequenceView::GetEventClips）、Play でも編集器のプレビューでも同じ時刻に自分の効果を動かす。
// 同じ名前を何度も置いてよい（稲妻のように繰り返す出来事）。
struct SequenceEventClip {
    uint64_t id = 1u;
    std::string name = "事件";
    float startSeconds = 0.0f;
    float durationSeconds = 0.5f;
};
struct SequenceEventTrack {
    std::vector<SequenceEventClip> clips;
};
inline constexpr size_t kMaximumSequenceEvents = 128u;
// 名前は SDK の固定長の欄（終端を含め 64 バイト）に収まる長さまで。
inline constexpr size_t kMaximumSequenceEventNameBytes = 63u;
bool ValidateSequenceEventTrack(const SequenceEventTrack& track, std::string& error);
float GetSequenceEventContentEnd(const SequenceEventTrack& track) noexcept;
} // namespace HIKARI::SEQUENCER
