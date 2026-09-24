#pragma once

#include "Render3D/Text/HIKARI_SignedDistanceField.h"

namespace HIKARI::GAME_UI {

// SDK-internal compatibility adapter. The shared Text renderer owns the
// SDF algorithm used by both Game UI and world-space text.
inline bool BuildGameUiSignedDistanceField(const uint8_t* coverage, int coverageWidth, int coverageHeight, int padding,
                                           uint8_t onEdgeValue, float pixelDistanceScale,
                                           std::vector<uint8_t>& outPixels, int& outWidth, int& outHeight) {
    return TEXT_RENDERING::BuildSignedDistanceField(coverage, coverageWidth, coverageHeight, padding, onEdgeValue,
                                                    pixelDistanceScale, outPixels, outWidth, outHeight);
}

} // namespace HIKARI::GAME_UI
