#pragma once

#include <cstdint>

#include "Scene/Environment/HIKARI_CelestialModel.h"
#include "Scene/Environment/HIKARI_WeatherModel.h"

// 時刻と天気の決定的なシミュレーション。資産・描画・World を持たず、
// 同じ入力列からは同じ状態を返す（存档はスナップショットの保存と復元）。
namespace HIKARI::ENVIRONMENT {

struct WeatherTransition {
    WeatherParameters from{};
    WeatherParameters to{};
    float durationSeconds = 0.0f;
    float elapsedSeconds = 0.0f;
    uint64_t targetId = 0u;

    bool operator==(const WeatherTransition&) const = default;
};

struct EnvironmentSnapshot {
    CalendarTime time{};
    GeoLocation location{};
    // 1 で実時間、60 で実時間 1 分が 1 時間。0 で停止。
    double timeScale = 0.0;
    WeatherTransition weather{};
    SurfaceWeatherState surface{};
    uint64_t random = 0x9E3779B97F4A7C15ull;
    float lightningFlash = 0.0f;
    // 雲を流す風の移動量の積分 [m]（ワールド xz）と雲の発達の実時間 [s]。風向や風速が変わっても雲は跳ばない。
    double windDisplacementX = 0.0;
    double windDisplacementZ = 0.0;
    double cloudSeconds = 0.0;

    bool operator==(const EnvironmentSnapshot&) const = default;
};

// 1 フレーム分の解決済みの状態。
struct EnvironmentFrameState {
    CalendarTime time{};
    CelestialState celestial{};
    WeatherParameters weather{};
    SurfaceWeatherState surface{};
    uint64_t weatherTargetId = 0u;
    float weatherTransitionProgress = 1.0f;
    // 0..1。落雷の閃光（発生時に 1、短時間で減衰）。
    float lightningFlash = 0.0f;
    // 最後の落雷の経過時間 [s] と、視点からの水平の向き（北から時計回り [deg]）と距離 [m]。
    float lightningAge = 0.0f;
    float lightningAzimuthDegrees = 0.0f;
    float lightningDistance = 0.0f;
    // 風の速度 [m/s] と移動量 [m]（ワールド xz）、雲の発達の実時間 [s]。
    MATH::Vec2 windVelocity{};
    double windDisplacementX = 0.0;
    double windDisplacementZ = 0.0;
    double cloudSeconds = 0.0;
};

class EnvironmentSimulation {
public:
    void Reset(const CalendarTime& time, const GeoLocation& location, const WeatherParameters& weather,
               uint64_t weatherId, double timeScale);
    void SetTime(const CalendarTime& time);
    void SetTimeScale(double timeScale);
    // 現在の補間結果から target へ遷移する。seconds が負なら target.transitionSeconds を使う。
    void RequestWeather(const WeatherParameters& target, uint64_t targetId, float seconds = -1.0f);
    // deltaSeconds は実時間。時刻は timeScale 倍で進み、天気と地表は実時間で進む。
    void Advance(float deltaSeconds);

    EnvironmentFrameState GetFrameState() const;
    const EnvironmentSnapshot& GetSnapshot() const noexcept { return snapshot_; }
    bool Restore(const EnvironmentSnapshot& snapshot);

private:
    WeatherParameters CurrentWeather() const noexcept;

    EnvironmentSnapshot snapshot_{};
    // 最後の落雷の向き [deg] と距離 [m]。閃光は 1 秒足らずで消えるのでスナップショットには入れない。
    float lightningAzimuthDegrees_ = 0.0f;
    float lightningDistance_ = 0.0f;
};

} // namespace HIKARI::ENVIRONMENT
