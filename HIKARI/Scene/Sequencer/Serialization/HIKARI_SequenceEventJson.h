#pragma once
#include <json.hpp>
#include "Scene/Sequencer/Tracks/HIKARI_SequenceEventTrack.h"

namespace HIKARI::SEQUENCER {
// 演出の "tracks" の中の type "event" を読み書きする。壊れた事件段は黙って直さず、読み込みを失敗させる。
void ReadSequenceEventJson(const nlohmann::json& sequence, SequenceEventTrack& out);
void WriteSequenceEventJson(const SequenceEventTrack& track, nlohmann::json& tracks);
} // namespace HIKARI::SEQUENCER
