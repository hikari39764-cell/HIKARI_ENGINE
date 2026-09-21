#pragma once
#include "Scene/Sequencer/Tracks/HIKARI_ShotPresentation.h"
#include <functional>

namespace HIKARI::EDITOR {
using CameraMotionCurveEditor = std::function<bool(SEQUENCER::PresentationParameter&, float, const std::string&)>;
bool DrawCameraMotionStack(std::vector<SEQUENCER::CameraMotionEffect>& effects, float seconds,
    const CameraMotionCurveEditor& curveEditor = {}, bool allowSpeedDriven = true);
}
