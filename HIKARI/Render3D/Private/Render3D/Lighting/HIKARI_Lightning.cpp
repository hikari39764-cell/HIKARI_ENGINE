#include "Render3D/Lighting/HIKARI_Lightning.h"

#include <cmath>
#include <cstddef>
#include <iterator>

#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI::RENDER3D::LIGHTNING {

namespace {
// 帰還雷撃のピークの光度 [cd]。数 km 先の地表で数十 lux（夜の景色が一瞬昼のように見え、昼の雷雨ではかすか）。
constexpr float kPeakIntensity = 2.0e9f;
// 帰還雷撃の時刻 [s] と強さ。一つの落雷は数十 ms おきに数回光り、各回は 20 ms ほどで消える。
constexpr float kStrokeTimes[] = {0.0f, 0.05f, 0.12f};
constexpr float kStrokeStrengths[] = {1.0f, 0.6f, 0.8f};
constexpr float kStrokeDecaySeconds = 0.02f;
} // namespace

LightningLight ResolveLightningLight(const SceneEnvironment& environment, const MATH::Vec3& viewer) noexcept {
    const EnvironmentLightingFrame& frame = environment.frame;
    LightningLight light{};
    if (!(frame.lightningFlash > 0.0f) || !std::isfinite(frame.lightningAge) ||
        !std::isfinite(frame.lightningDistance)) {
        return light;
    }
    float pulse = 0.0f;
    for (size_t stroke = 0; stroke < std::size(kStrokeTimes); ++stroke) {
        const float since = frame.lightningAge - kStrokeTimes[stroke];
        if (since >= 0.0f) {
            pulse += kStrokeStrengths[stroke] * std::exp(-since / kStrokeDecaySeconds);
        }
    }
    light.active = pulse > 1.0e-3f;
    light.position = {viewer.x + frame.lightningDirection.x * frame.lightningDistance,
                      environment.clouds.baseAltitude + 1000.0f,
                      viewer.z + frame.lightningDirection.y * frame.lightningDistance};
    // 青白い光（雲の段の HikariCloudLightningRadiance と同じ色）。
    light.color = {0.85f, 0.9f, 1.0f};
    light.intensity = kPeakIntensity * pulse;
    return light;
}

} // namespace HIKARI::RENDER3D::LIGHTNING
