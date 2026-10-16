#include "Scene/Environment/HIKARI_WeatherModel.h"
#include "Core/Math/HIKARI_ScalarMath.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::ENVIRONMENT {
namespace {

// 濡れ・水たまり・積雪の追従速度 [1/s]。強度 1 の雨で約 20 秒で濡れ、晴れて約 4 分で乾く。
constexpr float kWettingRate = 0.05f;
constexpr float kDryingRate = 0.004f;
constexpr float kPuddleFillRate = 0.01f;
constexpr float kPuddleDrainRate = 0.002f;
constexpr float kSnowFallRate = 0.004f;
constexpr float kSnowMeltRate = 0.001f;
constexpr float kRainMeltRate = 0.006f;

float LerpDegrees(float from, float to, float t) noexcept {
    float delta = std::fmod(to - from + 540.0f, 360.0f) - 180.0f;
    float result = std::fmod(from + delta * t, 360.0f);
    return result < 0.0f ? result + 360.0f : result;
}

float Approach(float value, float target, float rate, float deltaSeconds) noexcept {
    const float step = rate * deltaSeconds;
    return value < target ? (std::min)(value + step, target) : (std::max)(value - step, target);
}

} // namespace

WeatherParameters NormalizeWeatherParameters(WeatherParameters weather) noexcept {
    weather = JSONFIELDS::Sanitize(weather, kWeatherFields);
    if (!TEXT::IsNamedEnum(kPrecipitationNames, weather.precipitation)) {
        weather.precipitation = PrecipitationType::None;
    }
    if (weather.precipitation == PrecipitationType::None) {
        weather.precipitationIntensity = 0.0f;
    }
    if (weather.windFromDegrees >= 360.0f) {
        weather.windFromDegrees = 0.0f;
    }
    return weather;
}

void ReadWeatherParameters(const nlohmann::json& node, WeatherParameters& weather) {
    if (node.is_object()) {
        JSONFIELDS::ReadLenient(node, weather, kWeatherFields);
    }
    weather = NormalizeWeatherParameters(weather);
}

nlohmann::json WriteWeatherParameters(const WeatherParameters& weather) {
    nlohmann::json node = nlohmann::json::object();
    JSONFIELDS::Write(node, NormalizeWeatherParameters(weather), kWeatherFields);
    return node;
}

WeatherParameters BlendWeather(const WeatherParameters& from, const WeatherParameters& to, float t) noexcept {
    t = std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 1.0f;
    WeatherParameters blended{};
    blended.cloudCoverage = MATH::Lerp(from.cloudCoverage, to.cloudCoverage, t);
    blended.cloudType = MATH::Lerp(from.cloudType, to.cloudType, t);
    blended.cloudDensity = MATH::Lerp(from.cloudDensity, to.cloudDensity, t);
    blended.haze = MATH::Lerp(from.haze, to.haze, t);
    blended.fogDensity = MATH::Lerp(from.fogDensity, to.fogDensity, t);
    blended.fogHeightFalloff = MATH::Lerp(from.fogHeightFalloff, to.fogHeightFalloff, t);
    if (from.precipitation == to.precipitation) {
        blended.precipitation = to.precipitation;
        blended.precipitationIntensity = MATH::Lerp(from.precipitationIntensity, to.precipitationIntensity, t);
    } else if (t < 0.5f) {
        blended.precipitation = from.precipitation;
        blended.precipitationIntensity = from.precipitationIntensity * (1.0f - 2.0f * t);
    } else {
        blended.precipitation = to.precipitation;
        blended.precipitationIntensity = to.precipitationIntensity * (2.0f * t - 1.0f);
    }
    blended.windSpeed = MATH::Lerp(from.windSpeed, to.windSpeed, t);
    blended.windFromDegrees = LerpDegrees(from.windFromDegrees, to.windFromDegrees, t);
    blended.lightningPerMinute = MATH::Lerp(from.lightningPerMinute, to.lightningPerMinute, t);
    blended.transitionSeconds = to.transitionSeconds;
    return blended;
}

SurfaceWeatherState AdvanceSurfaceWeather(const SurfaceWeatherState& state, const WeatherParameters& weather,
                                          float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return state;
    }
    const float rain = weather.precipitation == PrecipitationType::Rain
                           ? std::clamp(weather.precipitationIntensity, 0.0f, 1.0f)
                           : 0.0f;
    const float snow = weather.precipitation == PrecipitationType::Snow
                           ? std::clamp(weather.precipitationIntensity, 0.0f, 1.0f)
                           : 0.0f;
    SurfaceWeatherState next = state;
    next.wetness = rain > 0.0f ? Approach(state.wetness, 1.0f, kWettingRate * rain, deltaSeconds)
                               : Approach(state.wetness, 0.0f, kDryingRate, deltaSeconds);
    next.puddles = rain > 0.0f && next.wetness > 0.6f
                       ? Approach(state.puddles, rain, kPuddleFillRate * rain, deltaSeconds)
                       : Approach(state.puddles, 0.0f, kPuddleDrainRate, deltaSeconds);
    next.snowCover = snow > 0.0f   ? Approach(state.snowCover, 1.0f, kSnowFallRate * snow, deltaSeconds)
                     : rain > 0.0f ? Approach(state.snowCover, 0.0f, kRainMeltRate * rain, deltaSeconds)
                                   : Approach(state.snowCover, 0.0f, kSnowMeltRate, deltaSeconds);
    return next;
}

} // namespace HIKARI::ENVIRONMENT
