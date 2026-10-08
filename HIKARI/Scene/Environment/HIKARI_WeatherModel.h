#pragma once

#include <cstdint>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"

// 天気プリセット（.hweather）の値と、天気の遷移・地表状態の時間変化。
// 時刻や地点には依らない「空と地表の状態」だけを扱う。
namespace HIKARI::ENVIRONMENT {

enum class PrecipitationType : uint8_t {
    None,
    Rain,
    Snow,
};

struct WeatherParameters {
    // 積雲層の被覆率と雲型（0 = 層雲、0.5 = 積雲、1 = 積乱雲）。
    float cloudCoverage = 0.35f;
    float cloudType = 0.5f;
    // 雲の消散係数の倍率。
    float cloudDensity = 1.0f;
    // 0..1。大気のエアロゾル（霞・黄砂）の増加量。
    float haze = 0.0f;
    // 地表付近の霧の消散係数 [1/m] と高さ減衰 [1/m]。
    float fogDensity = 0.0f;
    float fogHeightFalloff = 0.05f;
    PrecipitationType precipitation = PrecipitationType::None;
    float precipitationIntensity = 0.0f;
    // 風速 [m/s] と風が吹いてくる方位（北 0、東 90）。
    float windSpeed = 4.0f;
    float windFromDegrees = 270.0f;
    float lightningPerMinute = 0.0f;
    // この天気へ切り替える時の既定の遷移時間 [s]。
    float transitionSeconds = 20.0f;

    bool operator==(const WeatherParameters&) const = default;
};

inline constexpr TEXT::EnumName<PrecipitationType> kPrecipitationNames[] = {
    {PrecipitationType::None, "None"},
    {PrecipitationType::Rain, "Rain"},
    {PrecipitationType::Snow, "Snow"},
};

// 天気の項目の表。資産の読み書き・正規化・エディターの項目表示が共有する。
inline constexpr auto kWeatherFields = [](auto&& field) {
    field("cloudCoverage", &WeatherParameters::cloudCoverage, JSONFIELDS::Range{0.0, 1.0});
    field("cloudType", &WeatherParameters::cloudType, JSONFIELDS::Range{0.0, 1.0});
    field("cloudDensity", &WeatherParameters::cloudDensity, JSONFIELDS::Range{0.0, 8.0});
    field("haze", &WeatherParameters::haze, JSONFIELDS::Range{0.0, 1.0});
    field("fogDensity", &WeatherParameters::fogDensity, JSONFIELDS::Range{0.0, 0.5});
    field("fogHeightFalloff", &WeatherParameters::fogHeightFalloff, JSONFIELDS::Range{0.0, 1.0});
    field("precipitation", &WeatherParameters::precipitation, kPrecipitationNames);
    field("precipitationIntensity", &WeatherParameters::precipitationIntensity, JSONFIELDS::Range{0.0, 1.0});
    field("windSpeed", &WeatherParameters::windSpeed, JSONFIELDS::Range{0.0, 60.0});
    field("windFromDegrees", &WeatherParameters::windFromDegrees, JSONFIELDS::Range{0.0, 360.0});
    field("lightningPerMinute", &WeatherParameters::lightningPerMinute, JSONFIELDS::Range{0.0, 120.0});
    field("transitionSeconds", &WeatherParameters::transitionSeconds, JSONFIELDS::Range{0.0, 3600.0});
};

WeatherParameters NormalizeWeatherParameters(WeatherParameters weather) noexcept;
void ReadWeatherParameters(const nlohmann::json& node, WeatherParameters& weather);
nlohmann::json WriteWeatherParameters(const WeatherParameters& weather);

// t = 0 で from、1 で to。風向は最短の角度で補間し、降水の種類は強度が 0 になる中点で切り替える。
WeatherParameters BlendWeather(const WeatherParameters& from, const WeatherParameters& to, float t) noexcept;

// 地表の濡れ・水たまり・積雪。天気そのものではなく、時間をかけて追従する。
struct SurfaceWeatherState {
    float wetness = 0.0f;
    float puddles = 0.0f;
    float snowCover = 0.0f;

    bool operator==(const SurfaceWeatherState&) const = default;
};

SurfaceWeatherState AdvanceSurfaceWeather(const SurfaceWeatherState& state, const WeatherParameters& weather,
                                          float deltaSeconds) noexcept;

} // namespace HIKARI::ENVIRONMENT
