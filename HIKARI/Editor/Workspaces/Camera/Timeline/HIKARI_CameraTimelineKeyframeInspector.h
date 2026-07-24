#pragma once

#include "Editor/Workspaces/Camera/Timeline/HIKARI_CameraTimelineSelection.h"
#include "Scene/HIKARI_CinematicSequence.h"

namespace HIKARI::EDITOR {

bool DrawCameraTimelineKeyframeInspector(
    CinematicSequence &sequence,
    const CameraTimelineKeyframeSelection &selection, bool editingAllowed,
    bool snapEnabled, float snapFramesPerSecond);

bool DrawCameraTimelineKeyframeInspector(
    CinematicSequence &sequence,
    const std::vector<CameraTimelineKeyframeSelection> &selections,
    bool editingAllowed, bool snapEnabled, float snapFramesPerSecond);

} // namespace HIKARI::EDITOR
