#pragma once
#include <json.hpp>
#include "Scene/Sequencer/Tracks/HIKARI_SequenceTextTrack.h"

namespace HIKARI::SEQUENCER {
// 演出の "tracks" の中の type "text" を読み書きする。壊れた文字段は黙って直さず、読み込みを失敗させる。
void ReadSequenceTextJson(const nlohmann::json& sequence, SequenceTextTrack& out);
void WriteSequenceTextJson(const SequenceTextTrack& track, nlohmann::json& tracks);
} // namespace HIKARI::SEQUENCER
