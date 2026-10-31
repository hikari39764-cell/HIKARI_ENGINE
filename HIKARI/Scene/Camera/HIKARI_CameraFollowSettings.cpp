#include "Scene/Camera/HIKARI_CameraFollowSettings.h"
#include <algorithm>
#include <utility>

namespace HIKARI::CAMERA {
void CameraFollowSettings::Normalize() {
    *this = JSONFIELDS::Sanitize(*this, kCameraFollowFields);
    maximumPitchDegrees = (std::max)(maximumPitchDegrees, minimumPitchDegrees);
    initialPitchDegrees = std::clamp(initialPitchDegrees, minimumPitchDegrees, maximumPitchDegrees);
    maximumDistance = (std::max)(maximumDistance, minimumDistance);
    initialDistance = std::clamp(initialDistance, minimumDistance, maximumDistance);
    // 画面上の二つの狙いは左右の順に置き、近すぎる時は中心を保って離す。
    if (screenCompositionPrimaryAnchorX > screenCompositionSecondaryAnchorX) {
        std::swap(screenCompositionPrimaryAnchorX, screenCompositionSecondaryAnchorX);
    }
    if (screenCompositionSecondaryAnchorX - screenCompositionPrimaryAnchorX < 0.04f) {
        const float center =
            std::clamp((screenCompositionPrimaryAnchorX + screenCompositionSecondaryAnchorX) * 0.5f, 0.07f, 0.93f);
        screenCompositionPrimaryAnchorX = center - 0.02f;
        screenCompositionSecondaryAnchorX = center + 0.02f;
    }
    // 強制の範囲は猶予の範囲より少し広く。
    screenCompositionHardZoneHalfWidth =
        (std::max)(screenCompositionHardZoneHalfWidth, screenCompositionSoftZoneHalfWidth + 0.01f);
    screenCompositionHardZoneHalfHeight =
        (std::max)(screenCompositionHardZoneHalfHeight, screenCompositionSoftZoneHalfHeight + 0.01f);
    screenCompositionMaximumShoulderAngleDegrees =
        (std::max)(screenCompositionMaximumShoulderAngleDegrees, screenCompositionPreferredShoulderAngleDegrees);
}
} // namespace HIKARI::CAMERA
