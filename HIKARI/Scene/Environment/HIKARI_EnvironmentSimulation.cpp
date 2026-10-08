#include "Scene/Environment/HIKARI_EnvironmentSimulation.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::ENVIRONMENT {
namespace {

constexpr double kMaximumTimeScale = 100000.0;
// 閃光の減衰 [1/s]。約 0.25 秒で見えなくなる。
constexpr float kLightningDecayPerSecond = 4.0f;

double SanitizeTimeScale(double timeScale) noexcept {
    return std::isfinite(timeScale) ? std::clamp(timeScale, 0.0, kMaximumTimeScale) : 0.0;
}

// xorshift64*。スナップショットに状態を持ち、復元後も同じ列を返す。
double NextUnit(uint64_t& state) noexcept {
    state ^= state >> 12u;
    state ^= state << 25u;
    state ^= state >> 27u;
    return static_cast<double>((state * 0x2545F4914F6CDD1Dull) >> 11u) * (1.0 / 9007199254740992.0);
}

// 風が吹いてくる方位と風速から、風下へ向かうワールド xz の速度 [m/s]。
MATH::Vec2 WindVelocity(const WeatherParameters& weather, float northYawDegrees) noexcept {
    const MATH::Vec3 downwind = HorizonDirectionToWorld(weather.windFromDegrees + 180.0f, 0.0f, northYawDegrees);
    return {downwind.x * weather.windSpeed, downwind.z * weather.windSpeed};
}

bool IsValidLocation(const GeoLocation& location) noexcept {
    return std::isfinite(location.latitudeDegrees) && std::abs(location.latitudeDegrees) <= 90.0 &&
           std::isfinite(location.longitudeDegrees) && std::abs(location.longitudeDegrees) <= 180.0 &&
           std::isfinite(location.utcOffsetHours) && std::abs(location.utcOffsetHours) <= 14.0 &&
           std::isfinite(location.northYawDegrees);
}

} // namespace

void EnvironmentSimulation::Reset(const CalendarTime& time, const GeoLocation& location,
                                  const WeatherParameters& weather, uint64_t weatherId, double timeScale) {
    snapshot_ = {};
    if (IsValidCalendarTime(time)) {
        snapshot_.time = time;
    }
    if (IsValidLocation(location)) {
        snapshot_.location = location;
    }
    snapshot_.timeScale = SanitizeTimeScale(timeScale);
    const WeatherParameters normalized = NormalizeWeatherParameters(weather);
    snapshot_.weather = {normalized, normalized, 0.0f, 0.0f, weatherId};
}

void EnvironmentSimulation::SetTime(const CalendarTime& time) {
    if (IsValidCalendarTime(time)) {
        snapshot_.time = time;
    }
}

void EnvironmentSimulation::SetLocation(const GeoLocation& location) {
    if (IsValidLocation(location)) {
        snapshot_.location = location;
    }
}

void EnvironmentSimulation::SetTimeScale(double timeScale) { snapshot_.timeScale = SanitizeTimeScale(timeScale); }

void EnvironmentSimulation::RequestWeather(const WeatherParameters& target, uint64_t targetId, float seconds) {
    const WeatherParameters normalized = NormalizeWeatherParameters(target);
    const float duration = std::isfinite(seconds) && seconds >= 0.0f ? seconds : normalized.transitionSeconds;
    snapshot_.weather = {CurrentWeather(), normalized, duration, 0.0f, targetId};
}

void EnvironmentSimulation::Advance(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return;
    }
    snapshot_.time = AdvanceCalendarTime(snapshot_.time, deltaSeconds * snapshot_.timeScale / 3600.0);
    WeatherTransition& weather = snapshot_.weather;
    weather.elapsedSeconds = (std::min)(weather.elapsedSeconds + deltaSeconds, weather.durationSeconds);
    const WeatherParameters current = CurrentWeather();
    snapshot_.surface = AdvanceSurfaceWeather(snapshot_.surface, current, deltaSeconds);
    const MATH::Vec2 wind = WindVelocity(current, static_cast<float>(snapshot_.location.northYawDegrees));
    snapshot_.windDisplacementX += static_cast<double>(wind.x) * deltaSeconds;
    snapshot_.windDisplacementZ += static_cast<double>(wind.y) * deltaSeconds;
    snapshot_.cloudSeconds += deltaSeconds;

    snapshot_.lightningFlash = (std::max)(0.0f, snapshot_.lightningFlash - kLightningDecayPerSecond * deltaSeconds);
    // ポアソン過程として、このフレームに 1 回以上落ちる確率を使う。
    const double strikesPerSecond = static_cast<double>(current.lightningPerMinute) / 60.0;
    if (strikesPerSecond > 0.0 &&
        NextUnit(snapshot_.random) < 1.0 - std::exp(-strikesPerSecond * static_cast<double>(deltaSeconds))) {
        snapshot_.lightningFlash = 1.0f;
        // 落雷は視点から 2〜12 km のどこか（近すぎる落雷は雷鳴の演出側が受け持つ）。
        lightningAzimuthDegrees_ = static_cast<float>(NextUnit(snapshot_.random) * 360.0);
        lightningDistance_ = static_cast<float>(2000.0 + NextUnit(snapshot_.random) * 10000.0);
    }
}

WeatherParameters EnvironmentSimulation::CurrentWeather() const noexcept {
    const WeatherTransition& weather = snapshot_.weather;
    const float progress = weather.durationSeconds > 0.0f ? weather.elapsedSeconds / weather.durationSeconds : 1.0f;
    // 端を滑らかにして、遷移の開始・終了で変化率が跳ばないようにする。
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    return BlendWeather(weather.from, weather.to, eased);
}

EnvironmentFrameState EnvironmentSimulation::GetFrameState() const {
    const WeatherTransition& weather = snapshot_.weather;
    EnvironmentFrameState frame{};
    frame.time = snapshot_.time;
    frame.celestial = ComputeCelestialState(snapshot_.time, snapshot_.location);
    frame.weather = CurrentWeather();
    frame.surface = snapshot_.surface;
    frame.weatherTargetId = weather.targetId;
    frame.weatherTransitionProgress =
        weather.durationSeconds > 0.0f ? weather.elapsedSeconds / weather.durationSeconds : 1.0f;
    frame.lightningFlash = snapshot_.lightningFlash;
    frame.lightningAge = (1.0f - snapshot_.lightningFlash) / kLightningDecayPerSecond;
    frame.lightningAzimuthDegrees = lightningAzimuthDegrees_;
    frame.lightningDistance = lightningDistance_;
    frame.windVelocity = WindVelocity(frame.weather, static_cast<float>(snapshot_.location.northYawDegrees));
    frame.windDisplacementX = snapshot_.windDisplacementX;
    frame.windDisplacementZ = snapshot_.windDisplacementZ;
    frame.cloudSeconds = snapshot_.cloudSeconds;
    return frame;
}

bool EnvironmentSimulation::Restore(const EnvironmentSnapshot& snapshot) {
    const WeatherTransition& weather = snapshot.weather;
    const bool valid = IsValidCalendarTime(snapshot.time) && IsValidLocation(snapshot.location) &&
                       std::isfinite(snapshot.timeScale) && snapshot.timeScale >= 0.0 &&
                       std::isfinite(weather.durationSeconds) && weather.durationSeconds >= 0.0f &&
                       std::isfinite(weather.elapsedSeconds) && weather.elapsedSeconds >= 0.0f &&
                       weather.elapsedSeconds <= weather.durationSeconds && snapshot.random != 0u &&
                       std::isfinite(snapshot.lightningFlash);
    if (!valid) {
        return false;
    }
    snapshot_ = snapshot;
    snapshot_.timeScale = SanitizeTimeScale(snapshot.timeScale);
    snapshot_.weather.from = NormalizeWeatherParameters(weather.from);
    snapshot_.weather.to = NormalizeWeatherParameters(weather.to);
    snapshot_.lightningFlash = std::clamp(snapshot.lightningFlash, 0.0f, 1.0f);
    return true;
}

} // namespace HIKARI::ENVIRONMENT
