#include "Scene/Environment/HIKARI_CelestialModel.h"

#include <cmath>
#include <numbers>

namespace HIKARI::ENVIRONMENT {
namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0;
constexpr double kRadToDeg = 180.0 / std::numbers::pi;
// 1970-01-01 00:00 UTC のユリウス日。
constexpr double kUnixEpochJulianDay = 2440587.5;
constexpr double kJ2000JulianDay = 2451545.0;

double WrapDegrees(double value) noexcept {
    value = std::fmod(value, 360.0);
    return value < 0.0 ? value + 360.0 : value;
}

bool IsLeapYear(int64_t year) noexcept { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

uint32_t DaysInMonth(int64_t year, uint32_t month) noexcept {
    constexpr uint32_t kDays[12]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return month == 2u && IsLeapYear(year) ? 29u : kDays[month - 1u];
}

// 1970-01-01 からの日数（グレゴリオ暦、負の日も扱う）。
int64_t DaysFromCivil(int64_t year, uint32_t month, uint32_t day) noexcept {
    year -= month <= 2u ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yearOfEra = year - era * 400;
    const int64_t dayOfYear = (153 * (month > 2u ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

void CivilFromDays(int64_t days, int32_t& outYear, uint32_t& outMonth, uint32_t& outDay) noexcept {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t dayOfEra = days - era * 146097;
    const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t monthIndex = (5 * dayOfYear + 2) / 153;
    outDay = static_cast<uint32_t>(dayOfYear - (153 * monthIndex + 2) / 5 + 1);
    outMonth = static_cast<uint32_t>(monthIndex < 10 ? monthIndex + 3 : monthIndex - 9);
    outYear = static_cast<int32_t>(yearOfEra + era * 400 + (outMonth <= 2u ? 1 : 0));
}

struct Equatorial {
    double rightAscension = 0.0; // radians
    double declination = 0.0;    // radians
};

struct HorizonBasis {
    MATH::Vec3 east{};
    MATH::Vec3 north{};
    MATH::Vec3 up{0.0f, 1.0f, 0.0f};
};

HorizonBasis BuildHorizonBasis(float northYawDegrees) noexcept {
    const double yaw = static_cast<double>(northYawDegrees) * kDegToRad;
    const float s = static_cast<float>(std::sin(yaw));
    const float c = static_cast<float>(std::cos(yaw));
    return {{c, 0.0f, -s}, {-s, 0.0f, -c}, {0.0f, 1.0f, 0.0f}};
}

MATH::Vec3 ToWorld(const HorizonBasis& basis, double east, double north, double up) noexcept {
    const auto e = static_cast<float>(east);
    const auto n = static_cast<float>(north);
    const auto u = static_cast<float>(up);
    return MATH::Normalize(basis.east * e + basis.north * n + basis.up * u);
}

// 時角と赤緯から地平座標（東・北・天頂成分）へ。
MATH::Vec3 HourAngleToWorld(const HorizonBasis& basis, double hourAngle, double declination, double latitude) noexcept {
    const double east = -std::cos(declination) * std::sin(hourAngle);
    const double north =
        std::sin(declination) * std::cos(latitude) - std::cos(declination) * std::cos(hourAngle) * std::sin(latitude);
    const double up =
        std::sin(declination) * std::sin(latitude) + std::cos(declination) * std::cos(hourAngle) * std::cos(latitude);
    return ToWorld(basis, east, north, up);
}

// NOAA / Meeus の簡略式。誤差は 0.01 度程度。
Equatorial ComputeSun(double centuries, double obliquity) noexcept {
    const double meanLongitude = WrapDegrees(280.46646 + centuries * (36000.76983 + centuries * 0.0003032));
    const double meanAnomaly = (357.52911 + centuries * (35999.05029 - 0.0001537 * centuries)) * kDegToRad;
    const double center = std::sin(meanAnomaly) * (1.914602 - centuries * (0.004817 + 0.000014 * centuries)) +
                          std::sin(2.0 * meanAnomaly) * (0.019993 - 0.000101 * centuries) +
                          std::sin(3.0 * meanAnomaly) * 0.000289;
    const double omega = (125.04 - 1934.136 * centuries) * kDegToRad;
    const double longitude = (meanLongitude + center - 0.00569 - 0.00478 * std::sin(omega)) * kDegToRad;
    return {std::atan2(std::cos(obliquity) * std::sin(longitude), std::cos(longitude)),
            std::asin(std::sin(obliquity) * std::sin(longitude))};
}

// 主要項だけの月の位置。誤差は約 1 度で、表示用途には十分。
Equatorial ComputeMoon(double daysFromJ2000, double obliquity) noexcept {
    const double meanLongitude = 218.316 + 13.176396 * daysFromJ2000;
    const double meanAnomaly = (134.963 + 13.064993 * daysFromJ2000) * kDegToRad;
    const double latitudeArgument = (93.272 + 13.229350 * daysFromJ2000) * kDegToRad;
    const double longitude = (meanLongitude + 6.289 * std::sin(meanAnomaly)) * kDegToRad;
    const double latitude = 5.128 * std::sin(latitudeArgument) * kDegToRad;
    const double rightAscension = std::atan2(
        std::sin(longitude) * std::cos(obliquity) - std::tan(latitude) * std::sin(obliquity), std::cos(longitude));
    const double declination = std::asin(std::sin(latitude) * std::cos(obliquity) +
                                         std::cos(latitude) * std::sin(obliquity) * std::sin(longitude));
    return {rightAscension, declination};
}

double AngleBetween(const Equatorial& a, const Equatorial& b) noexcept {
    const double cosine =
        std::sin(a.declination) * std::sin(b.declination) +
        std::cos(a.declination) * std::cos(b.declination) * std::cos(a.rightAscension - b.rightAscension);
    return std::acos(cosine < -1.0 ? -1.0 : cosine > 1.0 ? 1.0 : cosine);
}

} // namespace

bool IsValidCalendarTime(const CalendarTime& time) noexcept {
    return time.month >= 1u && time.month <= 12u && time.day >= 1u && time.day <= DaysInMonth(time.year, time.month) &&
           std::isfinite(time.localHours) && time.localHours >= 0.0 && time.localHours < 24.0;
}

CalendarTime AdvanceCalendarTime(const CalendarTime& time, double hours) noexcept {
    if (!IsValidCalendarTime(time) || !std::isfinite(hours)) {
        return time;
    }
    const double total = time.localHours + hours;
    const double dayShift = std::floor(total / 24.0);
    CalendarTime advanced{};
    CivilFromDays(DaysFromCivil(time.year, time.month, time.day) + static_cast<int64_t>(dayShift), advanced.year,
                  advanced.month, advanced.day);
    advanced.localHours = total - dayShift * 24.0;
    if (advanced.localHours >= 24.0) {
        advanced.localHours = 0.0;
    }
    return advanced;
}

CelestialState ComputeCelestialState(const CalendarTime& time, const GeoLocation& location) noexcept {
    CelestialState state{};
    if (!IsValidCalendarTime(time) || !std::isfinite(location.latitudeDegrees) ||
        !std::isfinite(location.longitudeDegrees) || !std::isfinite(location.utcOffsetHours)) {
        return state;
    }
    const double utcHours = time.localHours - location.utcOffsetHours;
    const double julianDay =
        static_cast<double>(DaysFromCivil(time.year, time.month, time.day)) + kUnixEpochJulianDay + utcHours / 24.0;
    const double daysFromJ2000 = julianDay - kJ2000JulianDay;
    const double centuries = daysFromJ2000 / 36525.0;
    const double obliquity =
        (23.0 + (26.0 + (21.448 - centuries * (46.815 + centuries * (0.00059 - centuries * 0.001813))) / 60.0) / 60.0 +
         0.00256 * std::cos((125.04 - 1934.136 * centuries) * kDegToRad)) *
        kDegToRad;
    const double greenwichSidereal = WrapDegrees(280.46061837 + 360.98564736629 * daysFromJ2000 +
                                                 centuries * centuries * (0.000387933 - centuries / 38710000.0));
    const double localSidereal = WrapDegrees(greenwichSidereal + location.longitudeDegrees) * kDegToRad;
    const double latitude = location.latitudeDegrees * kDegToRad;
    const HorizonBasis basis = BuildHorizonBasis(location.northYawDegrees);

    const Equatorial sun = ComputeSun(centuries, obliquity);
    const Equatorial moon = ComputeMoon(daysFromJ2000, obliquity);
    state.sunDirection = HourAngleToWorld(basis, localSidereal - sun.rightAscension, sun.declination, latitude);
    state.moonDirection = HourAngleToWorld(basis, localSidereal - moon.rightAscension, moon.declination, latitude);
    state.sunAltitudeDegrees = static_cast<float>(std::asin(MATH::Dot(state.sunDirection, basis.up)) * kRadToDeg);
    state.moonAltitudeDegrees = static_cast<float>(std::asin(MATH::Dot(state.moonDirection, basis.up)) * kRadToDeg);

    // 位相角 = 180 度 - 離角。等級は Allen の位相曲線（満月 -12.73 等）。
    const double phaseAngle = std::numbers::pi - AngleBetween(sun, moon);
    const double phaseDegrees = phaseAngle * kRadToDeg;
    state.moonIlluminatedFraction = static_cast<float>((1.0 + std::cos(phaseAngle)) * 0.5);
    state.moonBrightness =
        static_cast<float>(std::pow(10.0, -0.4 * (0.026 * phaseDegrees + 4.0e-9 * std::pow(phaseDegrees, 4.0))));

    // 赤道座標の基底（春分点・赤経 90 度・天の北極）をワールドへ写す。
    state.celestialToWorldX = HourAngleToWorld(basis, localSidereal, 0.0, latitude);
    state.celestialToWorldY = HourAngleToWorld(basis, localSidereal - std::numbers::pi * 0.5, 0.0, latitude);
    state.celestialToWorldZ = HourAngleToWorld(basis, 0.0, std::numbers::pi * 0.5, latitude);
    return state;
}

MATH::Vec3 HorizonDirectionToWorld(float azimuthDegrees, float elevationDegrees, float northYawDegrees) noexcept {
    const double azimuth = static_cast<double>(azimuthDegrees) * kDegToRad;
    const double elevation = static_cast<double>(elevationDegrees) * kDegToRad;
    return ToWorld(BuildHorizonBasis(northYawDegrees), std::cos(elevation) * std::sin(azimuth),
                   std::cos(elevation) * std::cos(azimuth), std::sin(elevation));
}

} // namespace HIKARI::ENVIRONMENT
