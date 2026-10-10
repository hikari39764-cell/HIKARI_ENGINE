#pragma once

#include <cstdint>

#include "Core/Math/HIKARI_Math3D.h"

// 日付・時刻・地点から太陽と月の方向、月相、星空の向きを求める。
// ワールド座標は右手系 Y 上。北偏角 0 のとき東 = +X、北 = -Z。
namespace HIKARI::ENVIRONMENT {

struct GeoLocation {
    double latitudeDegrees = 35.68;
    double longitudeDegrees = 139.69;
    double utcOffsetHours = 9.0;
    // ワールドの -Z から北までの回転（Y 軸、度）。
    float northYawDegrees = 0.0f;

    bool operator==(const GeoLocation&) const = default;
};

// 地点の現地時刻（utcOffsetHours 基準）。
struct CalendarTime {
    int32_t year = 2026;
    uint32_t month = 6;
    uint32_t day = 21;
    double localHours = 10.0;

    bool operator==(const CalendarTime&) const = default;
};

struct CelestialState {
    // 太陽・月へ向かう単位方向（ワールド空間）。
    MATH::Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    MATH::Vec3 moonDirection{0.0f, -1.0f, 0.0f};
    float sunAltitudeDegrees = 90.0f;
    float moonAltitudeDegrees = -90.0f;
    // 0 = 新月、1 = 満月。
    float moonIlluminatedFraction = 0.0f;
    // 満月を 1 とする月の明るさ（位相角による等級の変化）。
    float moonBrightness = 0.0f;
    // 天球（赤道座標）からワールドへの回転の列ベクトル。星空の向きに使う。
    MATH::Vec3 celestialToWorldX{1.0f, 0.0f, 0.0f};
    MATH::Vec3 celestialToWorldY{0.0f, 1.0f, 0.0f};
    MATH::Vec3 celestialToWorldZ{0.0f, 0.0f, 1.0f};
};

bool IsValidCalendarTime(const CalendarTime& time) noexcept;
// 時刻を進める。日付の繰り上がり・繰り下がり（閏年を含む）を処理する。
CalendarTime AdvanceCalendarTime(const CalendarTime& time, double hours) noexcept;
CelestialState ComputeCelestialState(const CalendarTime& time, const GeoLocation& location) noexcept;
// 地平座標の方位（北 0、東 90、度）と高度（度）をワールドの単位方向へ写す。
MATH::Vec3 HorizonDirectionToWorld(float azimuthDegrees, float elevationDegrees, float northYawDegrees) noexcept;
// HorizonDirectionToWorld の逆。ワールドの方向を地平座標の方位（0 以上 360 未満）と高度へ。
void WorldDirectionToHorizon(const MATH::Vec3& direction, float northYawDegrees, float& azimuthDegrees,
                             float& elevationDegrees) noexcept;

} // namespace HIKARI::ENVIRONMENT
