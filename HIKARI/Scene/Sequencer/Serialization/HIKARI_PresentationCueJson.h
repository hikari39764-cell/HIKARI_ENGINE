#pragma once
#include <json.hpp>
#include "Scene/Sequencer/Tracks/HIKARI_PresentationCueTrack.h"

namespace HIKARI::SEQUENCER {
    // Invalid authoring data is rejected, never silently converted to another cue.
    void ReadPresentationCueJson(const nlohmann::json& sequence, PresentationCueTrack& out);
    void WritePresentationCueJson(const PresentationCueTrack& track, nlohmann::json& tracks);
}
