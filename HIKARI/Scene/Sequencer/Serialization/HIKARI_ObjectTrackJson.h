#pragma once

#include <json.hpp>

#include "Scene/Sequencer/Tracks/HIKARI_ObjectTrack.h"

namespace HIKARI::SEQUENCER {
// tracks 配列の type "object" を物体轨として読み書きする。壊れた値は例外で知らせる（演出全体を読まない）。
void ReadObjectTrackJson(const nlohmann::json& sequence, std::vector<ObjectTrack>& out);
void WriteObjectTrackJson(const std::vector<ObjectTrack>& tracks, nlohmann::json& outTracks);
} // namespace HIKARI::SEQUENCER
